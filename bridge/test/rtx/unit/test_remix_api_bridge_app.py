"""
Runs a 32-bit test app against the bridge client and the 64-bit server,
once with bridge timeouts disabled (the default) and once with them enabled.

The client and server come from a regular bridge build, because the unit test builds do not build them.
The test is skipped when they are missing.
"""

import argparse
import os
import shutil
import subprocess
import sys

# Meson reports a test that exits with this code as skipped.
SKIP_RETURN_CODE = 77

CLIENT_DLL = "d3d9.dll"
SERVER_EXE = "NvRemixBridge.exe"
TREX_DIR = ".trex"

# Maps each run's name to its disableTimeouts value in bridge.conf.
RUNS = {
    "timeouts_disabled": "True",
    "timeouts_enabled": "False",
}

parser = argparse.ArgumentParser(allow_abbrev=False)
parser.add_argument("--testExe", required=True, help="File name of the test app in --executablesDir.")
parser.add_argument("--executablesDir", required=True)
parser.add_argument("--bridgeDir", required=True,
                    help="Folder with d3d9.dll and .trex\\NvRemixBridge.exe from a bridge build, usually bridge\\_output.")
parser.add_argument("--artifactDir", required=True)
parser.add_argument("--timeout", type=int, default=60, help="Seconds before a hung run is killed.")
args = parser.parse_args()


def deploy(run_dir, disable_timeouts, test_exe, client_dll, server_exe):
    if os.path.isdir(run_dir):
        shutil.rmtree(run_dir)
    trex_dir = os.path.join(run_dir, TREX_DIR)
    os.makedirs(trex_dir)
    shutil.copy2(test_exe, run_dir)
    shutil.copy2(client_dll, run_dir)
    shutil.copy2(server_exe, trex_dir)
    with open(os.path.join(trex_dir, "bridge.conf"), "w") as conf:
        conf.write("exposeRemixApi = True\n")
        conf.write("disableTimeouts = {}\n".format(disable_timeouts))


def stop_leftover_servers(run_dir):
    # A killed run can leave its server running, which keeps the deployed files locked.
    script = ("Get-Process NvRemixBridge -ErrorAction SilentlyContinue | "
              "Where-Object {{ $_.Path -like '{}*' }} | Stop-Process -Force").format(run_dir)
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=False)


def remove_deployed_binaries(run_dir):
    # Only the logs are kept, so CI does not upload the binaries again with the test artifacts.
    for path in (os.path.join(run_dir, args.testExe),
                 os.path.join(run_dir, CLIENT_DLL),
                 os.path.join(run_dir, TREX_DIR, SERVER_EXE)):
        try:
            os.remove(path)
        except OSError as error:
            print("Could not remove {}: {}".format(path, error), flush=True)


def run(name, disable_timeouts, test_exe, client_dll, server_exe):
    run_dir = os.path.join(os.path.abspath(args.artifactDir), name)
    deploy(run_dir, disable_timeouts, test_exe, client_dll, server_exe)
    print("TEST: {} {} (disableTimeouts = {})".format(args.testExe, name, disable_timeouts), flush=True)

    # Write to a file instead of a pipe, so a server that inherited the handle cannot hold the run open.
    log_path = os.path.join(run_dir, "test.log")
    with open(log_path, "w") as log_f:
        process = subprocess.Popen([os.path.join(run_dir, args.testExe)], cwd=run_dir, stdout=log_f, stderr=log_f)
        timed_out = False
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            process.wait()
    stop_leftover_servers(run_dir)
    remove_deployed_binaries(run_dir)

    with open(log_path, "r") as log_f:
        print(log_f.read(), flush=True)
    if timed_out:
        print("FAILED: Killed after {} seconds. Logs are in {}".format(args.timeout, run_dir), flush=True)
        return False
    if process.returncode != 0:
        print("FAILED with return code {}. Logs are in {}".format(process.returncode, run_dir), flush=True)
        return False
    return True


if __name__ == "__main__":
    test_exe = os.path.join(args.executablesDir, args.testExe)
    client_dll = os.path.join(args.bridgeDir, CLIENT_DLL)
    server_exe = os.path.join(args.bridgeDir, TREX_DIR, SERVER_EXE)

    if not os.path.exists(test_exe):
        print("Cannot find {}".format(test_exe))
        sys.exit(1)
    missing = [path for path in (client_dll, server_exe) if not os.path.exists(path)]
    if missing:
        print("SKIPPED: Build the bridge first. Missing: {}".format(", ".join(missing)))
        sys.exit(SKIP_RETURN_CODE)

    os.makedirs(args.artifactDir, exist_ok=True)
    results = [run(name, disable_timeouts, test_exe, client_dll, server_exe)
               for name, disable_timeouts in RUNS.items()]
    sys.exit(0 if all(results) else 1)
