# GPU Print

GPU Print allows printing of values observed for a particular pixel on the GPU into Remix log (at info level) and debug output to help with debugging of actual values observed in shader code. It requires adding a `GpuPrint::write()` call with the data you want to have printed out. 

Usage:
1. Include `"rtx/utility/gpu_printing.slangh"` in shader file.
2. Add `GpuPrint::write(pixelIndex, data);` to your shader code with the data you want to print.
3. Enable GPU printing functionality in `Developer Settings Menu GUI \ Debug`
4. Point a mouse at a pixel of interest and press CTRL to print out the data. Alternatively, specify the pixel coordinate in GpuPrint's GUI and press CTRL.
5. See the output in the log output (printed at info level) channel and/or a debugger IDE output window.