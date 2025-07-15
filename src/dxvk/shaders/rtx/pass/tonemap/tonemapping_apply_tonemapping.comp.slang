/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping_noise.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"

[[vk::binding(TONEMAPPING_APPLY_BLUE_NOISE_TEXTURE_INPUT)]]
Texture2DArray BlueNoise;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT)]]
RWTexture2D<float4> InColorBuffer;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_TONE_CURVE_INPUT)]]
Sampler1D<float> InToneCurve;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT)]]
RWTexture1D<float> InExposure;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT)]]
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<ToneMappingApplyToneMappingArgs> cb;

float lumaAverage(float3 color) 
{
  return dot(color, float3(1.f / 3.f));
}

float3 setSaturation(float3 color, float saturation) 
{
  return mix(float3(calcBt709Luminance(color)), color, saturation);
}

float3 setSaturationAverage(float3 color, float saturation) 
{
  return mix(float3(lumaAverage(color)), color, saturation);
}

float3 colorGrading(float3 inputColor) 
{
  // Apply color temperature & tint
  inputColor = inputColor * cb.colorBalance;
  
  // Adjust contrast & saturation
  inputColor = saturate((cb.contrast * (inputColor - 0.18f)) + 0.18f); // 0.18 = Mid level grey
  inputColor = setSaturation(inputColor, cb.saturation);
  
  return inputColor;
}

// Photographic Tone Reproduction for Digital Images
// http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
float3 reinhardToneMapper(float3 inputColor) 
{
  float inputLuminance = calcBt709Luminance(inputColor);
  
  // (Eq. 4) Scale the luminance down to compress bright highlights to representable LDR values
  float outputLuminance = inputLuminance / (1.0f + inputLuminance);
  
  // (Not Reinhard Operator) Make the curve S-shaped to boost shadow contrast
  // Should only activate when outputLuminance < e^gToneMappingShadowContrastEnd
  outputLuminance *= pow(min(1.0f, outputLuminance * exp(-cb.shadowContrastEnd)), cb.shadowContrast);
  
  // Apply output luminance to the input
  return inputColor * (outputLuminance / inputLuminance);  
}

// Heji and Burgess-Dawson tone curve, from
// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 filmicToneMapper(float3 inputColor) 
{
  float3 x = max(float3(0), inputColor - float3(0.004));
  x = (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
  
  return pow(x, float3(2.2));
}


// Dynamic tone mapper based on Eilertsen, Mantiuk, and Unger's paper
// *Real-time noise-aware tone mapping*. Applies a luminance-based tone 
// curve and then applies filmic saturation emulation.
float3 dynamicToneMapper(float3 inputColor) 
{
  // Calculate luminance of color, and avoid values that would result in computing the log of 0:
  float luminance = max(calcBt709Luminance(inputColor), exp2(cb.toneCurveMinStops));
  
  // Map to the space the tone curve uses:
  const float logLuminance = log2(luminance);
  const float biasedLuminance = (logLuminance - cb.toneCurveMinStops) / (cb.toneCurveMaxStops - cb.toneCurveMinStops);
  
  // We can now use the sampler to perform linear interpolation!
  // Note that we're doing this on a log-log chart of the tone curve.
  const float outLogLuminance = InToneCurve.SampleLevel(biasedLuminance, 0).r;  
  float outLuminance = exp2(outLogLuminance);
    
  // Apply the luminance shift to the color:
  float3 mappedColor = inputColor * outLuminance / luminance;
  
  return mappedColor;
}

float3 applyToneMapping(float3 color, uint2 pixelPosition)
{
  color *= getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor);

  if (cb.toneMappingEnabled != 0) 
  {
    if (cb.debugMode == 0) 
    {
      // Normal tone mapping
      color = dynamicToneMapper(color);
      
      if (cb.finalizeWithACES)
      {
        color = ACESFilm(color, cb.useLegacyACES);
      }
    } 
    else
    {
      // Debug mode
      float2 texCoord = pixelPosition / float2(imageSize(InColorBuffer));
      
      // Show tonemapped result on one side of screen
      if (texCoord.x > 0.475f) 
      {
        color = dynamicToneMapper(color);
      }
      
      // Plot the tone curve
      float histogramValue = InToneCurve.SampleLevel(texCoord.x, 0).r;
      
      // Division by 15.5876 makes it so that the bottom of the screen corresponds to a value of 1/255 on an 8-bit sRGB monitor, 
      // and the top of the screen corresponds to a value of 255/255 
      const float curveScale = 0.5; // additional scale to display a wider range of curve values
      float plotValue = 1.0 + histogramValue * curveScale / 15.5876; // 15.5876 = log_2(255^2.2)
      plotValue = 1.0f - plotValue / 1.0;  // Reverse Y coordinate to plot from bottom-up of the image
      float width = 0.01f;
      
      if (texCoord.y >= plotValue - width/2 && texCoord.y <= plotValue + width/2) 
      {
        color = mix(color, float3(1.0f, 1.0f, 1.0f), 0.5f);
      }
    }
  }
  
  if (cb.colorGradingEnabled > 0)
  {
    color = colorGrading(color);
  }

  if (cb.performSRGBConversion > 0)
  {
    color = linearToGamma(color);
  }

  // Finally, apply dithering to the output color to break up banding artifacts.
  // This is only first-order; ideally, we might be performing dithering in
  // linear space while using range information from sRGB space, but the 
  // errors here should be pretty small.
  return ditherTo8Bit(
    color,
    pixelPosition,
    cb.frameIndex,
    cb.ditherMode != ditherModeNone,
    cb.ditherMode == ditherModeSpatialTemporal
  );
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  float3 inputColor = imageLoad(InColorBuffer, threadId).xyz;

  float3 outputColor = applyToneMapping(inputColor, threadId);

  // clamp outputColor to avoid problems with NIS
  outputColor = saturate(outputColor);

  OutColorBuffer[threadId] = vec4(outputColor, 1.0f);
}