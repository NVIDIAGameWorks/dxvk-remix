"""
Runs a 32-bit test app against the bridge client, the 64-bit server and the Remix runtime,
with rtx.gpuResidentScene enabled, and checks the colors of the app's panels in a screenshot.

The client and server come from a regular bridge build, and the runtime from a regular runtime build (_output).
The test is skipped when they are missing.
The panel layout must match test_remix_api_gpu_resident_scene_device.cpp.
"""

import argparse
import glob
import math
import os
import shutil
import struct
import subprocess
import sys
import time

# Meson reports a test that exits with this code as skipped.
SKIP_RETURN_CODE = 77

CLIENT_DLL = "d3d9.dll"
SERVER_EXE = "NvRemixBridge.exe"
TREX_DIR = ".trex"

# The runtime files that the test does not need, which are skipped to keep the deployment small.
SKIPPED_RUNTIME_EXTENSIONS = (".pdb", ".lib", ".exp")

RTX_CONF = """rtx.gpuResidentScene = True
rtx.bloom.enable = False
rtx.autoExposure.enabled = False
"""

# The camera and panels of test_remix_api_gpu_resident_scene_device.cpp.
FOV_Y_DEGREES = 60.0
PANEL_DISTANCE = 10.0
PANEL_SPACING_X = 5.0
PANEL_SPACING_Y = 3.5
PANEL_HALF_SIZE = 1.2

# Each probe is (name, column, row, horizontal offset within the panel in panel half sizes, expectation).
PROBES = [
    ("retained instance", 0, 0, 0.0, "green"),
    ("instance moved by SceneInstanceUpdate", 1, 0, 0.0, "blue"),
    ("removed instance", 2, 0, 0.0, "dark"),
    ("material changed by UpdateMaterial", 0, 1, 0.0, "green"),
    ("frame-local instance", 1, 1, 0.0, "blue"),
    ("emissive texture, left half", 2, 1, -0.5, "red"),
    ("emissive texture, right half", 2, 1, 0.5, "blue"),
    ("re-created mesh and material", 2, 2, 0.0, "green"),
]

parser = argparse.ArgumentParser(allow_abbrev=False)
parser.add_argument("--testExe", required=True, help="File name of the test app in --executablesDir.")
parser.add_argument("--executablesDir", required=True)
parser.add_argument("--bridgeDir", required=True,
                    help="Folder with d3d9.dll and .trex\\NvRemixBridge.exe from a bridge build, usually bridge\\_output.")
parser.add_argument("--runtimeDir", required=True, help="Folder with the Remix runtime from a runtime build, usually _output.")
parser.add_argument("--artifactDir", required=True)
parser.add_argument("--screenshotFrame", type=int, default=60)
parser.add_argument("--timeout", type=int, default=600, help="Seconds before a hung run is killed.")
args = parser.parse_args()


def stop_leftover_processes(run_dir):
    # The server and the runtime's crash handler can outlive the app, which keeps the deployed files locked.
    script = ("Get-Process -ErrorAction SilentlyContinue | "
              "Where-Object {{ $_.Path -like '{}*' }} | Stop-Process -Force").format(run_dir)
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=False)


def deploy(run_dir, test_exe, client_dll, server_exe):
    trex_dir = os.path.join(run_dir, TREX_DIR)
    shutil.copytree(args.runtimeDir, trex_dir,
                    ignore=lambda _, names: [n for n in names if n.lower().endswith(SKIPPED_RUNTIME_EXTENSIONS)])
    shutil.copy2(test_exe, run_dir)
    shutil.copy2(client_dll, run_dir)
    shutil.copy2(server_exe, trex_dir)
    with open(os.path.join(trex_dir, "bridge.conf"), "w") as conf:
        conf.write("exposeRemixApi = True\n")
    with open(os.path.join(run_dir, "rtx.conf"), "w") as conf:
        conf.write(RTX_CONF)


def remove_deployed_binaries(run_dir):
    # Only the logs and the screenshot are kept, so CI does not upload the binaries with the test artifacts.
    # The runtime's files can stay locked for a while after the processes exit, so the removal is retried.
    trex_dir = os.path.join(run_dir, TREX_DIR)
    for _ in range(60):
        shutil.rmtree(trex_dir, ignore_errors=True)
        if not os.path.exists(trex_dir):
            break
        time.sleep(1)
    else:
        print("Could not remove {}".format(trex_dir), flush=True)
    for name in (args.testExe, CLIENT_DLL):
        try:
            os.remove(os.path.join(run_dir, name))
        except OSError as error:
            print("Could not remove {}: {}".format(name, error), flush=True)


def read_dds(path):
    """Returns (width, height, pixel), where pixel(x, y) returns the RGB values in [0, 1]."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"DDS ":
        raise RuntimeError("{} is not a DDS file".format(path))
    height, width = struct.unpack_from("<II", data, 12)
    four_cc = data[84:88]
    rgb_bit_count, r_mask, g_mask, b_mask = struct.unpack_from("<IIII", data, 88)
    offset = 128
    dxgi_format = None
    if four_cc == b"DX10":
        dxgi_format = struct.unpack_from("<I", data, 128)[0]
        offset += 20
    elif four_cc == b"\x71\x00\x00\x00":
        dxgi_format = 10  # D3DFMT_A16B16G16R16F
    elif four_cc == b"\x00\x00\x00\x00" and rgb_bit_count == 32:
        dxgi_format = 28 if r_mask == 0xFF else 87

    if dxgi_format in (28, 29):
        stride, unpack = 4, lambda o: [c / 255.0 for c in data[o:o + 3]]
    elif dxgi_format in (87, 91):
        stride, unpack = 4, lambda o: [c / 255.0 for c in data[o:o + 3][::-1]]
    elif dxgi_format == 10:
        stride, unpack = 8, lambda o: list(struct.unpack_from("<eee", data, o))
    elif dxgi_format == 2:
        stride, unpack = 16, lambda o: list(struct.unpack_from("<fff", data, o))
    elif dxgi_format == 24:
        def unpack(o):
            v = struct.unpack_from("<I", data, o)[0]
            return [(v & 0x3FF) / 1023.0, ((v >> 10) & 0x3FF) / 1023.0, ((v >> 20) & 0x3FF) / 1023.0]
        stride = 4
    else:
        raise RuntimeError("{} has an unsupported format (four CC {}, DXGI format {})".format(path, four_cc, dxgi_format))
    return width, height, lambda x, y: unpack(offset + (y * width + x) * stride)


def probe_color(width, height, pixel, column, row, offset):
    tan_half_fov = math.tan(math.radians(FOV_Y_DEGREES) * 0.5)
    aspect = width / height
    center_x = (column - 1.0) * PANEL_SPACING_X + offset * PANEL_HALF_SIZE
    center_y = (1.0 - row) * PANEL_SPACING_Y
    half_size = PANEL_HALF_SIZE * 0.3
    x0 = int(((center_x - half_size) / (PANEL_DISTANCE * tan_half_fov * aspect) * 0.5 + 0.5) * width)
    x1 = int(((center_x + half_size) / (PANEL_DISTANCE * tan_half_fov * aspect) * 0.5 + 0.5) * width)
    y0 = int((0.5 - (center_y + half_size) / (PANEL_DISTANCE * tan_half_fov) * 0.5) * height)
    y1 = int((0.5 - (center_y - half_size) / (PANEL_DISTANCE * tan_half_fov) * 0.5) * height)
    total = [0.0, 0.0, 0.0]
    count = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            for c, value in enumerate(pixel(x, y)):
                total[c] += value
            count += 1
    return [value / count for value in total]


def matches(color, expectation):
    r, g, b = color
    if expectation == "dark":
        return max(color) < 0.1
    channels = {"red": (r, g, b), "green": (g, r, b), "blue": (b, r, g)}[expectation]
    return channels[0] > 0.25 and channels[0] > 2.0 * max(channels[1], channels[2])


def check_screenshot(run_dir):
    screenshots = glob.glob(os.path.join(run_dir, "Screenshots", "rtxImagePostTonemapping*.dds"))
    if not screenshots:
        print("FAILED: The runtime did not write a screenshot of frame {}.".format(args.screenshotFrame), flush=True)
        return False
    width, height, pixel = read_dds(max(screenshots, key=os.path.getmtime))
    passed = True
    for name, column, row, offset, expectation in PROBES:
        color = probe_color(width, height, pixel, column, row, offset)
        ok = matches(color, expectation)
        passed = passed and ok
        print("{}: {} expected {}, got ({:.3f}, {:.3f}, {:.3f})".format(
            "OK" if ok else "FAILED", name, expectation, *color), flush=True)
    return passed


# Warnings of calls that the runtime rejected, which the bridge reports as successful to the app.
REJECTED_CALL_WARNINGS = ("requires rtx.gpuResidentScene", "handle not found", "are not supported")


def check_runtime_log(run_dir):
    log_path = os.path.join(run_dir, "rtx-remix", "logs", "remix-dxvk.log")
    if not os.path.exists(log_path):
        print("FAILED: The runtime did not write {}.".format(log_path), flush=True)
        return False
    with open(log_path, "r", errors="replace") as log:
        rejected = [line.rstrip() for line in log if any(warning in line for warning in REJECTED_CALL_WARNINGS)]
    for line in rejected:
        print("FAILED: The runtime rejected a call: {}".format(line), flush=True)
    return not rejected


def run(test_exe, client_dll, server_exe):
    # Windows can keep the folder of a finished run locked for a while, so each run gets a new folder,
    # and the folders of earlier runs are removed when they are no longer locked.
    artifact_dir = os.path.abspath(args.artifactDir)
    for old_run_dir in glob.glob(os.path.join(artifact_dir, "gpu_resident_scene_*")):
        stop_leftover_processes(old_run_dir)
        shutil.rmtree(old_run_dir, ignore_errors=True)
    run_dir = os.path.join(artifact_dir, "gpu_resident_scene_" + time.strftime("%Y%m%d_%H%M%S"))
    deploy(run_dir, test_exe, client_dll, server_exe)
    print("TEST: {} with the runtime".format(args.testExe), flush=True)

    # The image tests' settings, see tests/rtx/dxvk_rt_testing/run_tests.py.
    # Without synchronous shader compilation, the runtime skips ray tracing, and the screenshot, until the shaders are ready.
    env = dict(os.environ)
    env["DXVK_DUMP_SCREENSHOT_FRAME"] = str(args.screenshotFrame)
    env["DXVK_SCREENSHOT_PATH"] = os.path.join(run_dir, "Screenshots")
    env["DXVK_RTX_CONFIG_FILE"] = os.path.join(run_dir, "rtx.conf")
    env["DXVK_STATE_CACHE"] = "0"
    env["DXVK_FORCE_WINDOWED"] = "1"
    env["DXVK_WAIT_ASYNC_TEXTURES"] = "1"
    env["DXVK_TEXTURES_NEVER_DOWNGRADE"] = "1"
    env["DXVK_ASYNC_ASSET_LOADING"] = "0"
    env["DXVK_TEXTURES_SAMPLER_FEEDBACK_ENABLE"] = "0"
    env["RTX_AUTOMATION_DISABLE_BLOCKING_DIALOG_BOXES"] = "1"
    env["RTX_ASYNC_SHADER_PREWARMING"] = "0"
    env["RTX_ENABLE_ASYNC_COMPILATION"] = "0"

    # Write to a file instead of a pipe, so a server that inherited the handle cannot hold the run open.
    log_path = os.path.join(run_dir, "test.log")
    with open(log_path, "w") as log_f:
        process = subprocess.Popen([os.path.join(run_dir, args.testExe)], cwd=run_dir, env=env, stdout=log_f, stderr=log_f)
        timed_out = False
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            process.wait()
    stop_leftover_processes(run_dir)
    remove_deployed_binaries(run_dir)

    with open(log_path, "r") as log_f:
        print(log_f.read(), flush=True)
    if timed_out:
        print("FAILED: Killed after {} seconds. Logs are in {}".format(args.timeout, run_dir), flush=True)
        return False
    if process.returncode != 0:
        print("FAILED with return code {}. Logs are in {}".format(process.returncode, run_dir), flush=True)
        return False
    log_ok = check_runtime_log(run_dir)
    screenshot_ok = check_screenshot(run_dir)
    if not (log_ok and screenshot_ok):
        print("FAILED: Logs and the screenshot are in {}".format(run_dir), flush=True)
    return log_ok and screenshot_ok


if __name__ == "__main__":
    test_exe = os.path.join(args.executablesDir, args.testExe)
    client_dll = os.path.join(args.bridgeDir, CLIENT_DLL)
    server_exe = os.path.join(args.bridgeDir, TREX_DIR, SERVER_EXE)

    if not os.path.exists(test_exe):
        print("Cannot find {}".format(test_exe))
        sys.exit(1)
    missing = [path for path in (client_dll, server_exe, os.path.join(args.runtimeDir, "d3d9.dll")) if not os.path.exists(path)]
    if missing:
        print("SKIPPED: Build the bridge and the runtime first. Missing: {}".format(", ".join(missing)))
        sys.exit(SKIP_RETURN_CODE)

    os.makedirs(args.artifactDir, exist_ok=True)
    sys.exit(0 if run(test_exe, client_dll, server_exe) else 1)
