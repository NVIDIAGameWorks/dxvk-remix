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
    const int crossLength = 5;
    return (m_contentPixel.x == s_samplePosition.x && m_contentPixel.y >= s_samplePosition.y - crossLength && m_contentPixel.y <= s_samplePosition.y + crossLength) ||
           (m_contentPixel.y == s_samplePosition.y && m_contentPixel.x >= s_samplePosition.x - crossLength && m_contentPixel.x <= s_samplePosition.x + crossLength);
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

void getTriangleCandidateInfo(NEECell cell, int binIndex, out float probability, out int age, out vec3 lightIntensity)
{
  NEECandidate candidiate = cell.getCandidate(binIndex);
  float sampleThreshold = candidiate.getSampleThreshold();          
  float lastSampleThreshold = 0;
  if (binIndex > 0)
  {
    NEECandidate candidiate = cell.getCandidate(binIndex - 1);
    lastSampleThreshold = candidiate.getSampleThreshold();
  }
  probability = sampleThreshold - lastSampleThreshold;
  age = candidiate.getAge();

  // Triangle color
  vec3 triangleCenter;
  f16vec3 triangleNormal;
  NEECacheUtils.calculateTriangleLightIntensity(candidiate.getSurfaceID(), candidiate.getPrimitiveID(), triangleCenter, triangleNormal, lightIntensity);
}

void getLightCandidateInfo(NEECell cell, int binIndex, RAB_Surface surface, f16vec3 viewDirection, out float probability, out int age, out vec3 radiance, out vec3 direction)
{
  NEELightCandidate lightCandidate = cell.getLightCandidate(binIndex);
  int3 cellID = NEECache.offsetToCell(cell.m_offset);
  vec3 cellCenter = NEECache.cellToCenterPoint(cellID);

  float16_t specularRatio = calcBt709Luminance(surface.opaqueSurfaceMaterialInteraction.baseReflectivity)
    / calcBt709Luminance(surface.opaqueSurfaceMaterialInteraction.albedo + surface.opaqueSurfaceMaterialInteraction.baseReflectivity);

  cell.calculateLightCandidateNormalizedWeight(binIndex, cellCenter,
    surface.minimalSurfaceInteraction.position,
    viewDirection,
    surface.opaqueSurfaceMaterialInteraction.shadingNormal,
    specularRatio,
    surface.opaqueSurfaceMaterialInteraction.isotropicRoughness,
    probability);
  age = lightCandidate.getAge();

  MemoryPolymorphicLight memoryPolymorphicLight = lights[lightCandidate.getLightID()];
  DecodedPolymorphicLight decodedPolymorphicLight = decodePolymorphicLight(memoryPolymorphicLight);
  radiance = decodedPolymorphicLight.radiance;
  float luminance = calcBt709Luminance(radiance);
  radiance = radiance / (1e-5 + luminance) * log(1 + luminance) * 0.1;
  direction = abs(normalize(lightCandidate.getOffset()));
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

  Canvas canvas = Canvas.create(threadIndex, 3);
  ivec2 samplePixel = canvas.getSamplePixel();
  RAB_Surface sampleSurface = RAB_GetGBufferSurface(samplePixel, false);

  // Deserialize the Ray Interaction
  // Note: Re-derive the original primary ray direction.
  const Ray originalPrimaryRay = rayCreatePrimaryFromPixel(cb.camera, samplePixel);
  MinimalRayInteraction minimalRayInteraction = minimalRayInteractionReadFromGBuffer(
    originalPrimaryRay, samplePixel, PrimaryViewDirection, PrimaryConeRadius);

  // Debug views
  switch(cb.debugView)
  {
  case DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM:
  case DEBUG_VIEW_NEE_CACHE_HISTOGRAM:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      NEECell cell = NEECache.findCell(surface.minimalSurfaceInteraction.position, false, vec3(0));
      ivec3 cellID = NEECache.pointToCell(surface.minimalSurfaceInteraction.position, false, vec3(0));
      canvas.drawBackground(indirectLight);
      if (all(cellID != -1))
      {
        canvas.m_backgroundColor = lerp(canvas.m_backgroundColor, (dot(cellID, 1) & 0x1) ? vec3(abs(surface.opaqueSurfaceMaterialInteraction.shadingNormal)) : vec3(0), 0.2);
      }

      NEECell sampleCell = NEECache.findCell(sampleSurface.minimalSurfaceInteraction.position, false, vec3(0));
      if (sampleCell.isValid())
      {
        if(!canvas.isPadding())
        {
          // Get bin index
          int binIndex = canvas.getBin(NEECell.getMaxCandidateCount());
          bool isBinValid = true;

          float probability;
          int age;
          vec3 lightIntensity, direction;
          bool isLightHistogram = cb.debugView == DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM;
          if (isLightHistogram)
          {
            isBinValid = binIndex < sampleCell.getLightCandidateCount();
            getLightCandidateInfo(sampleCell, binIndex, sampleSurface, minimalRayInteraction.viewDirection, probability, age, lightIntensity, direction);
          }
          else
          {
            isBinValid = binIndex < sampleCell.getCandidateCount();
            getTriangleCandidateInfo(sampleCell, binIndex, probability, age, lightIntensity);
          }

          // Calculate histogram
          if(canvas.isCross())
          {
            canvas.drawForeground(vec3(1,0,0));
          }
          if(canvas.isLegend(2))
          {
            // Show samples
            int binIndex = canvas.getBin(NEE_CACHE_SAMPLES);
            NEESample sample = sampleCell.getSample(binIndex);
            vec3 color = isLightHistogram ? direction : sample.radiance / (calcBt709Luminance(sample.radiance) + 1e-5) * 0.7;
            float alpha = isLightHistogram ? (isBinValid ? 1.0 : 0.0) : 1.0;
            canvas.drawForeground(color, alpha);
          }
          else if(canvas.isLegend(1))
          {
            canvas.drawForeground(lightIntensity, isBinValid ? 1.0 : 0.0);
          }
          else if(canvas.isLegend(0))
          {
            canvas.drawForeground(lerp(vec3(0,1,0), vec3(82,66,44)/255.0, saturate(age / 255.0)), isBinValid ? 1.0 : 0.0);
          }
          else if(canvas.isHistogram() && canvas.isHistogramBar(probability))
          {
            // Histogram
            canvas.drawForeground(vec3(1,0.5,0), isBinValid ? 1.0 : 0.0);
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
  }
}