### Optional Requirements:
1. [wine](https://www.winehq.org/) - 3.10 or newer
2. [Mingw-w64](https://www.mingw-w64.org) - compiler and headers (at least version 8.0)
3. [glslang](https://github.com/KhronosGroup/glslang) compiler

### HUD
The `DXVK_HUD` environment variable controls a HUD which can display the framerate and some stat counters. It accepts a comma-separated list of the following options:
- `devinfo`: Displays the name of the GPU and the driver version.
- `fps`: Shows the current frame rate.
- `frametimes`: Shows a frame time graph.
- `submissions`: Shows the number of command buffers submitted per frame.
- `drawcalls`: Shows the number of draw calls and render passes per frame.
- `pipelines`: Shows the total number of graphics and compute pipelines.
- `memory`: Shows the amount of device memory allocated and used.
- `gpuload`: Shows estimated GPU load. May be inaccurate.
- `version`: Shows DXVK version.
- `api`: Shows the D3D feature level used by the application.
- `compiler`: Shows shader compiler activity
- `samplers`: Shows the current number of sampler pairs used *[D3D9 Only]*
- `scale=x`: Scales the HUD by a factor of `x` (e.g. `1.5`)

Additionally, `DXVK_HUD=1` has the same effect as `DXVK_HUD=devinfo,fps`, and `DXVK_HUD=full` enables all available HUD elements.

### Frame rate limit
The `DXVK_FRAME_RATE` environment variable can be used to limit the frame rate. A value of `0` uncaps the frame rate, while any positive value will limit rendering to the given number of frames per second. Alternatively, the configuration file can be used.

### Device filter
Some applications do not provide a method to select a different GPU. In that case, DXVK can be forced to use a given device:
- `DXVK_FILTER_DEVICE_NAME="Device Name"` Selects devices with a matching Vulkan device name, which can be retrieved with tools such as `vulkaninfo`. Matches on substrings, so "VEGA" or "AMD RADV VEGA10" is supported if the full device name is "AMD RADV VEGA10 (LLVM 9.0.0)", for example. If the substring matches more than one device, the first device matched will be used.

**Note:** If the device filter is configured incorrectly, it may filter out all devices and applications will be unable to create a D3D device.

### State cache
DXVK caches pipeline state by default, so that shaders can be recompiled ahead of time on subsequent runs of an application, even if the driver's own shader cache got invalidated in the meantime. This cache is enabled by default, and generally reduces stuttering.

The following environment variables can be used to control the cache:
- `DXVK_STATE_CACHE=0` Disables the state cache.
- `DXVK_STATE_CACHE_PATH=/some/directory` Specifies a directory where to put the cache files. Defaults to the current working directory of the application.

### Debugging
The following environment variables can be used for **debugging** purposes.
- `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` Enables Vulkan debug layers. Highly recommended for troubleshooting rendering issues and driver crashes. Requires the Vulkan SDK to be installed on the host system.
- `DXVK_LOG_LEVEL=none|error|warn|info|debug` Controls message logging.
- `DXVK_LOG_PATH=/some/directory` Changes path where log files are stored. Set to `none` to disable log file creation entirely, without disabling logging.
- `DXVK_CONFIG_FILE=/xxx/dxvk.conf` Sets path to the configuration file.
- `DXVK_PERF_EVENTS=1` Enables use of the VK_EXT_debug_utils extension for translating performance event markers.