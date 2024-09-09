/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RESTIR_GI_RESERVOIRS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/integrate/integrate_nee_bindings.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtxdi/DlssEnhancementFilterFunctions.slangh"
#include "rtx/algorithm/nee_cache.h"
#include "rtx/algorithm/integrator.slangh"

layout(push_constant)
ConstantBuffer<VisualizeNeeArgs> args;

bool isCrossPixel(ivec2 pixel, ivec2 crossCenter)
{
  const int crossLength = 5;
  return (pixel.x == crossCenter.x && pixel.y >= crossCenter.y - crossLength && pixel.y <= crossCenter.y + crossLength) ||
         (pixel.y == crossCenter.y && pixel.x >= crossCenter.x - crossLength && pixel.x <= crossCenter.x + crossLength);
}

struct Canvas
{
  static const int s_padding = 1;
  static const ivec2 s_contentSize = ivec2(100);
  static const ivec2 s_samplePosition = s_contentSize / 2;
  static const int s_legendHeight = 8;

  int m_rows;
  ivec2 m_cornerPixel;
  ivec2 m_contentPixel;
  vec3 m_backgroundColor;
  vec4 m_foregroundColor;

  static Canvas create(ivec2 pixel, int rows)
  {
    Canvas canvas;
    canvas.setPixelLocation(pixel);
    canvas.setLegendRows(rows);
    canvas.drawForeground(vec3(0), 0);
    canvas.drawBackground(vec3(0));
    return canvas;
  }

  static ivec2 getWindowSize()
  {
    return s_contentSize + s_padding * 2;
  }

  static void screenToLocal(ivec2 screenPixel, out ivec2 cornerPixel, out ivec2 contentPixel)
  {
    ivec2 windowSize = getWindowSize();
    ivec2 flippedPixel = ivec2(screenPixel.x, cb.camera.resolution.y - screenPixel.y);
    cornerPixel = flippedPixel / windowSize * windowSize;
    contentPixel = flippedPixel - cornerPixel;
    contentPixel -= s_padding;
  }

  static void localToScreen(ivec2 cornerPixel, ivec2 contentPixel, out ivec2 screenPixel)
  {
    contentPixel += s_padding;
    ivec2 flippedPixel = cornerPixel + contentPixel;
    screenPixel = ivec2(flippedPixel.x, cb.camera.resolution.y - flippedPixel.y);
  }

  bool isCross()
  {
    return isCrossPixel(m_contentPixel, s_samplePosition);
  }

  ivec2 getSamplePixel()
  {
    ivec2 screenPixel;
    localToScreen(m_cornerPixel, s_samplePosition, screenPixel);
    return screenPixel;
  }

  [mutating] void setPixelLocation(ivec2 pixel)
  {
    screenToLocal(pixel, m_cornerPixel, m_contentPixel);
  }

  [mutating] void setLegendRows(int rows)
  {
    m_rows = rows;
  }

  [mutating] void drawBackground(vec3 color)
  {
    m_backgroundColor = color;
  }

  vec3 getBackgroundColor()
  {
    return m_backgroundColor;
  }

  [mutating] void drawForeground(vec3 color, float alpha = 1.0f)
  {
    m_foregroundColor = vec4(color, alpha);
  }

  vec3 getFinalColor()
  {
    return lerp(m_backgroundColor, m_foregroundColor.rgb, m_foregroundColor.a);
  }

  bool isPadding()
  {
    return !(all(m_contentPixel >= ivec2(0)) && all(m_contentPixel < ivec2(s_contentSize - s_padding * 2)));
  }

  int isLegend(int row)
  {
    int pixelRow = m_contentPixel.y / s_legendHeight;
    return pixelRow == row;
  }

  int isHistogram()
  {
    int totalLegendHeight = m_rows * s_legendHeight;
    return m_contentPixel.y > totalLegendHeight;
  }

  int getBin(int binCount)
  {
    return m_contentPixel.x * binCount / s_contentSize.x;
  }

  bool isHistogramBar(float barHeight)
  {
    int totalLegendHeight = m_rows * s_legendHeight;
    return (m_contentPixel.y - totalLegendHeight) / float(s_contentSize.y - totalLegendHeight) < barHeight;
  }
}

void getTriangleCandidateInfo(NEECell cell, int binIndex, out float probability, out vec3 lightIntensity)
{
  NEECandidate candidiate = cell.getCandidate(binIndex);
  probability = candidiate.getSampleProbability();

  // Triangle color
  vec3 triangleCenter;
  f16vec3 triangleNormal;
  NEECacheUtils.calculateTriangleLightIntensity(candidiate.getSurfaceID(), candidiate.getPrimitiveID(), triangleCenter, triangleNormal, lightIntensity);
  // Multiply 0.1 to show brighter lights
  lightIntensity = log(calcBt709Luminance(lightIntensity) + 1) * 0.1;
}

vec3 logarithmicColor(float value)
{
  if (value < 10)
  {
    return vec3(1,0,0);
  }
  else if (value < 100.0)
  {
    return vec3(1,0.5,0);
  }
  else if (value < 1000.0)
  {
    return vec3(1,1,0);
  }
  else if (value < 10000.0)
  {
    return vec3(0,1,0);
  }
  else if (value < 100000.0)
  {
    return vec3(0,1,1);
  }
  else if (value < 1000000.0)
  {
    return vec3(0,0,1);
  }
  else
  {
    return vec3(1);
  }
}

void getLightCandidateInfo(NEECell cell, int binIndex, RAB_Surface surface, f16vec3 viewDirection, out float probability, out vec3 radiance, out vec3 direction)
{
  NEELightCandidate lightCandidate = cell.getLightCandidate(binIndex);

  float16_t specularRatio = calcBt709Luminance(surface.opaqueSurfaceMaterialInteraction.baseReflectivity)
    / calcBt709Luminance(surface.opaqueSurfaceMaterialInteraction.albedo + surface.opaqueSurfaceMaterialInteraction.baseReflectivity);

  const bool isSubsurface = isSubsurfaceMaterial(surface.opaqueSurfaceMaterialInteraction);

   cell.calculateLightCandidateNormalizedWeight(binIndex,
     surface.minimalSurfaceInteraction.position,
     viewDirection,
     surface.opaqueSurfaceMaterialInteraction.shadingNormal,
     specularRatio,
     surface.opaqueSurfaceMaterialInteraction.isotropicRoughness,
     isSubsurface,
     probability);

  MemoryPolymorphicLight memoryPolymorphicLight = lights[lightCandidate.getLightID()];
  DecodedPolymorphicLight decodedPolymorphicLight = decodePolymorphicLight(memoryPolymorphicLight);
  radiance = decodedPolymorphicLight.radiance;
  float luminance = calcBt709Luminance(radiance);
  radiance = radiance / (1e-5 + luminance) * log(1 + luminance) * 0.1;
  direction = abs(normalize(NEECache.getCenter() + lightCandidate.getOffset() - surface.minimalSurfaceInteraction.position));
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
{
  if (any(threadIndex >= cb.camera.resolution))
  {
    return;
  }

  const bool gBufferMiss = isGBufferMiss(threadIndex, PrimaryConeRadius);

  // Early out if the primary surface was a miss or if the indirect integrator was not selected
  // to output indirect lighting information
  if (gBufferMiss)
  {
    return;
  }

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadIndex, SharedFlags);

  RAB_Surface surface = RAB_GetGBufferSurface(threadIndex, false);
  vec3 diffuseLight = PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex].xyz;
  vec3 specularLight = PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex].xyz;
  vec3 indirectLight = (diffuseLight + specularLight);

  Canvas canvas = Canvas.create(threadIndex, 2);
  ivec2 samplePixel = canvas.getSamplePixel();
  RAB_Surface sampleSurface = RAB_GetGBufferSurface(samplePixel, false);

  // Deserialize the Ray Interaction
  // Note: Re-derive the original primary ray direction.
  const Ray originalPrimaryRay = rayCreatePrimaryFromPixel(cb.camera, samplePixel);
  MinimalRayInteraction minimalRayInteraction = minimalRayInteractionReadFromGBuffer(
    originalPrimaryRay, samplePixel, PrimaryViewDirection, PrimaryConeRadius);

  // Get visible point's cell color
  uint jitter = 0;
  int cellOffset = NEECache.pointToOffset(surface.minimalSurfaceInteraction.position, f16vec3(0), jitter);
  NEECell cell = NEECache.getCell(cellOffset);
  vec3 cellColor = vec3(uint3(cellOffset >> 10, cellOffset >> 5, cellOffset) & 31) / 31.0;

  // Debug views
  switch(cb.debugView)
  {
  case DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM:
  case DEBUG_VIEW_NEE_CACHE_HISTOGRAM:
  case DEBUG_VIEW_NEE_CACHE_ACCUMULATE_MAP:
  case DEBUG_VIEW_NEE_CACHE_HASH_MAP:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      canvas.drawBackground(indirectLight);
      canvas.m_backgroundColor = lerp(canvas.m_backgroundColor, cellColor, 0.5);

      NEECell sampleCell = NEECache.getCell(NEECache.pointToOffset(sampleSurface.minimalSurfaceInteraction.position, f16vec3(0), jitter));
      if (sampleCell.isValid())
      {
        if (!canvas.isPadding())
        {
          // Get bin index
          int binIndex = canvas.getBin(NEECell.getMaxCandidateCount());
          bool isBinValid = true;

          float probability;
          vec3 lightIntensity, direction;
          bool isLightHistogram = cb.debugView == DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM;
          vec3 histogramColor = vec3(1,0.5,0);
          if (isLightHistogram)
          {
            isBinValid = binIndex < sampleCell.getLightCandidateCount();
            getLightCandidateInfo(sampleCell, binIndex, sampleSurface, minimalRayInteraction.viewDirection, probability, lightIntensity, direction);
          }
          else
          {
            isBinValid = binIndex < sampleCell.getCandidateCount();
            getTriangleCandidateInfo(sampleCell, binIndex, probability, lightIntensity);
          }

          int slotBinIndex = canvas.getBin(32);
          if (cb.debugView == DEBUG_VIEW_NEE_CACHE_ACCUMULATE_MAP)
          {
            isBinValid = true;          
            uint2 thisValue = sampleCell.getSlotTaskValue(slotBinIndex);
            probability = log(float(thisValue.y)) / (32);
            histogramColor = logarithmicColor(thisValue.y);
          }
          else if (cb.debugView == DEBUG_VIEW_NEE_CACHE_HASH_MAP)
          {
            isBinValid = true;          
            uint2 thisValue = sampleCell.getHashSlotTaskValue(slotBinIndex);
            probability = log(float(thisValue.y)) / (32);
            histogramColor = logarithmicColor(thisValue.y);
          }

          // Calculate histogram
          if (canvas.isCross())
          {
            canvas.drawForeground(vec3(1,0,0));
          }
          if (canvas.isLegend(1))
          {
            // Show samples
            int binIndex = canvas.getBin(NEE_CACHE_SAMPLES);
            NEESample sample = sampleCell.getSample(binIndex);
            vec3 color = isLightHistogram ? (isBinValid ? direction : vec3(0.0)) : sample.radiance / (calcBt709Luminance(sample.radiance) + 1e-5) * 0.7;
            float alpha = isLightHistogram ? (isBinValid ? 1.0 : 0.0) : 1.0;
            canvas.drawForeground(color, alpha);
          }
          else if (canvas.isLegend(0))
          {
            canvas.drawForeground(lightIntensity, isBinValid ? 1.0 : 0.0);
          }
          else if (canvas.isHistogram() && canvas.isHistogramBar(probability))
          {
            // Histogram
            canvas.drawForeground(histogramColor, isBinValid ? 1.0 : 0.0);
          }
        }
        else
        {
          canvas.drawForeground(vec3(0), 0.2);
        }
      }
      storeInDebugView(ivec2(threadIndex), canvas.getFinalColor());
    }
    break;
  case DEBUG_VIEW_NEE_CACHE_TRIANGLE_CANDIDATE:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      int4 surfacePrimitiveIndex = asint(readInDebugView(ivec2(threadIndex)));
      uint2 resolution = cb.camera.resolution;
      uint2 cursorPixel = args.mouseUV * resolution;
      if (isCrossPixel(threadIndex, cursorPixel))
      {
        storeInDebugView(ivec2(threadIndex), vec3(1,1,0));
      }
      else
      {
        RAB_Surface cursorSurface = RAB_GetGBufferSurface(cursorPixel, false);

        uint jitter = 0;
        int cursorCellOffset = NEECache.pointToOffset(cursorSurface.minimalSurfaceInteraction.position, f16vec3(0), jitter);
        NEECell cursorCell = NEECache.getCell(cursorCellOffset);      
        float lightSelectionPDF = cursorCell.searchCandidate(surfacePrimitiveIndex.x, surfacePrimitiveIndex.y);

        vec3 backgroundColor = indirectLight * linearToGamma(cellColor);
        vec4 triangleColor = 0.0;
        if (lightSelectionPDF > 0.5)
        {
          triangleColor = lerp(vec4(1,1,0,0.5), vec4(0,1,0,0.5), (lightSelectionPDF - 0.5) * 2.0);
        }
        else if (lightSelectionPDF > 0.0)
        {
          triangleColor = lerp(vec4(1,0,0,0.5), vec4(1,1,0,0.5), lightSelectionPDF * 2.0);
        }
        vec3 resultColor = lerp(backgroundColor, triangleColor.rgb, triangleColor.a);
        storeInDebugView(ivec2(threadIndex), resultColor);
      }
    }
    break;
  }
}