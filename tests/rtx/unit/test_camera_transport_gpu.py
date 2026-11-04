# Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
# SPDX-License-Identifier: MIT
"""Run camera transport arithmetic on Vulkan: python test_camera_transport_gpu.py.

Requires slangpy and numpy. This exercises the production Slang helpers on a GPU;
it does not replace the recursive-camera NRC or two-mirror image regressions.
"""
from pathlib import Path

import numpy as np
import slangpy as spy


def main():
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    device = spy.Device(
        type=spy.DeviceType.vulkan,
        compiler_options={"include_paths": [str(here), str(root / "src/dxvk/shaders")]},
    )
    kernel = device.create_compute_kernel(device.load_program("camera_transport.slang", ["main"]))
    results = device.create_buffer(struct_size=16, element_count=6, usage=spy.BufferUsage.unordered_access)
    gains = device.create_buffer(
        data=np.array([256.0, 256.0], dtype=np.float32),
        struct_size=4,
        usage=spy.BufferUsage.shader_resource,
    )
    kernel.dispatch(thread_count=[1, 1, 1], vars={"results": results, "gains": gains})
    values = results.to_numpy().view(np.float32).reshape(6, 4)
    np.testing.assert_array_equal(values[0], [65408, 65408, 65408, 1])
    np.testing.assert_array_equal(values[1], [0, 16352, 0, 1])
    np.testing.assert_array_equal(values[2], [1, 0, 0, 0])
    np.testing.assert_array_equal(values[3], [1, 1, 0, 0])
    np.testing.assert_array_equal(values[4], [65408, 65408, 65408, 1])
    np.testing.assert_array_equal(values[5], [0, 0.25, 1, 0])
    device.wait_for_idle()
    print("PASS: finite FP16 camera attenuation, pre-packing radiance, and specular/diffuse path history")


if __name__ == "__main__":
    main()
