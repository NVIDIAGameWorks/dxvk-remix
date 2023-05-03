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
#include "rtx_nis.h"
#include "dxvk_device.h"
#include "rtx.h"
#include "rtx_context.h"

#include "rtx/external/NIS/NIS_Config.h"
#include "rtx/external/NIS/DXVK_NIS_Bindings.h"
#include "rtx_render/rtx_shader_manager.h"

#include <rtx_shaders/DXVK_NIS_Main_128_float_24.h>
#include <rtx_shaders/DXVK_NIS_Main_128_float_32.h>
#include <rtx_shaders/DXVK_NIS_Main_128_half_24.h>
#include <rtx_shaders/DXVK_NIS_Main_128_half_32.h>
#include <rtx_shaders/DXVK_NIS_Main_256_float_24.h>
#include <rtx_shaders/DXVK_NIS_Main_256_float_32.h>
#include <rtx_shaders/DXVK_NIS_Main_256_half_24.h>
#include <rtx_shaders/DXVK_NIS_Main_256_half_32.h>

#include <gli/format.hpp>
#include <gli/target.hpp>
#include <gli/texture.hpp>


namespace dxvk {

  namespace {
  
#define __NIS_SHADER_PERMUTATION(__blocksize, __fp, __blockheight) \
    class NISShader_##__blocksize##_##__fp##_##__blockheight : public ManagedShader   \
    {   \
      SHADER_SOURCE(NISShader_##__blocksize##_##__fp##_##__blockheight, VK_SHADER_STAGE_COMPUTE_BIT, DXVK_NIS_Main_##__blocksize##_##__fp##_##__blockheight);    \
    \
      PUSH_CONSTANTS(NISConfig)   \
    \
      BEGIN_PARAMETER()   \
        SAMPLER(NIS_BINDING_SAMPLER_LINEAR_CLAMP)   \
        TEXTURE2D(NIS_BINDING_INPUT)    \
        RW_TEXTURE2D(NIS_BINDING_OUTPUT)   \
        TEXTURE2D(NIS_BINDING_COEF_SCALER)    \
        TEXTURE2D(NIS_BINDING_COEF_USM)   \
      END_PARAMETER()   \
    }; \
    PREWARM_SHADER_PIPELINE(NISShader_##__blocksize##_##__fp##_##__blockheight);

#define NIS_SHADER_PERMUTATION(__blocksize, __fp) \
  __NIS_SHADER_PERMUTATION(__blocksize, __fp, 24); \
  __NIS_SHADER_PERMUTATION(__blocksize, __fp, 32)

  NIS_SHADER_PERMUTATION(128, float);
  NIS_SHADER_PERMUTATION(256, float);
  NIS_SHADER_PERMUTATION(128, half);
  NIS_SHADER_PERMUTATION(256, half);

#undef NIS_SHADER_PERMUTATION

  }

  DxvkNIS::DxvkNIS(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd())
    , m_device(device) {
  }

  DxvkNIS::~DxvkNIS() {
  }

  static const Resources::Resource& getInput(const dxvk::Rc<DxvkContext>& ctx, const Resources::RaytracingOutput& rtOutput) {
    return rtOutput.m_compositeOutput.resource(Resources::AccessType::Read);
  };

  void DxvkNIS::createTexture(DxvkNIS::Texture& texture, const dxvk::Rc<DxvkContext>& ctx, const VkFormat format, void* data) {
    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = VkExtent3D { kFilterSize / 4, kPhaseCount, 1 };
    desc.numLayers = 1;
    desc.mipLevels = 1;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
      | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    desc.access = VK_ACCESS_TRANSFER_WRITE_BIT
      | VK_ACCESS_SHADER_READ_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_GENERAL;

    texture.image = ctx->getDevice()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "NIS output");

    const DxvkFormatInfo* formatInfo = imageFormatInfo(format);
    const uint32_t rowPitch = desc.extent.width * formatInfo->elementSize;
    const uint32_t layerPitch = rowPitch * desc.extent.height;

    ctx->updateImage(texture.image,
                     VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, desc.numLayers },
                     VkOffset3D { 0, 0, 0 },
                     desc.extent,
                     (void*) data, rowPitch, layerPitch);

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    texture.view = ctx->getDevice()->createImageView(texture.image, viewInfo);
  }

  void DxvkNIS::setConfig(Rc<RtxContext> ctx) {
    NISGPUArchitecture gpuArch;

    DxvkGpuVendor vendor = DxvkGpuVendor(m_device->adapter()->devicePropertiesExt().core.properties.vendorID);
    switch (vendor) {
    case DxvkGpuVendor::Amd:
      gpuArch = NISGPUArchitecture::AMD_Generic;
      break;

    case DxvkGpuVendor::Intel:
      gpuArch = NISGPUArchitecture::Intel_Generic;
      break;

    case DxvkGpuVendor::Nvidia:
      if (m_useFp16) {
        gpuArch = NISGPUArchitecture::NVIDIA_Generic_fp16;
      } else {
        gpuArch = NISGPUArchitecture::NVIDIA_Generic;
      }

      break;

    default:
      // well...
      gpuArch = NISGPUArchitecture::NVIDIA_Generic;
      break;
    }

    NISOptimizer opt(true, gpuArch);
    m_blockWidth = opt.GetOptimalBlockWidth();
    m_blockHeight = opt.GetOptimalBlockHeight();
    m_threadGroupSize = opt.GetOptimalThreadGroupSize();
    assert(m_threadGroupSize == 128 || m_threadGroupSize == 256);

    if (m_useFp16) {
      createTexture(m_coefScaleTextureFp16, ctx, VK_FORMAT_R16G16B16A16_SFLOAT, (void*) coef_scale_fp16);
      createTexture(m_coefUsmTextureFp16, ctx, VK_FORMAT_R16G16B16A16_SFLOAT, (void*) coef_usm_fp16);
    } else {
      createTexture(m_coefScaleTextureFp32, ctx, VK_FORMAT_R32G32B32A32_SFLOAT, (void*) coef_scale);
      createTexture(m_coefUsmTextureFp32, ctx, VK_FORMAT_R32G32B32A32_SFLOAT, (void*) coef_usm);
    }

    if (!m_sampler.ptr()) {
      DxvkSamplerCreateInfo samplerInfo;
      samplerInfo.magFilter = VK_FILTER_LINEAR;
      samplerInfo.minFilter = VK_FILTER_LINEAR;
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      samplerInfo.mipmapLodBias = 0.0f;
      samplerInfo.mipmapLodMin = 0.0f;
      samplerInfo.mipmapLodMax = 0.0f;
      samplerInfo.useAnisotropy = VK_FALSE;
      samplerInfo.maxAnisotropy = 1.0f;
      samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.compareToDepth = VK_FALSE;
      samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
      samplerInfo.borderColor = VkClearColorValue();
      samplerInfo.usePixelCoord = VK_FALSE;

      m_sampler = m_device->createSampler(samplerInfo);
    }
  }

  Rc<DxvkShader> DxvkNIS::getShader() {
    if (m_threadGroupSize == 128) {
      if (m_blockHeight == 24) {
        if (m_useFp16) {
          return NISShader_128_half_24::getShader();
        } else {
          return NISShader_128_float_24::getShader();
        }
      } else {
        if (m_useFp16) {
          return NISShader_128_half_32::getShader();
        } else {
          return NISShader_128_float_32::getShader();
        }
      }
    } else {
      if (m_blockHeight == 24) {
        if (m_useFp16) {
          return NISShader_256_half_24::getShader();
        } else {
          return NISShader_256_float_24::getShader();
        }
      } else {
        if (m_useFp16) {
          return NISShader_256_half_32::getShader();
        } else {
          return NISShader_256_float_32::getShader();
        }
      }
    }
  }

  void DxvkNIS::dispatch(Rc<RtxContext> ctx, const Resources::RaytracingOutput& rtOutput) {
    setConfig(ctx);

    auto& input = getInput(ctx, rtOutput);
    auto& output = rtOutput.m_finalOutput;

    VkExtent3D inputExtent = input.image->info().extent;
    VkExtent3D outputExtent = output.image->info().extent;

    NISConfig nisConfig;
    NVScalerUpdateConfig(nisConfig,
                         m_sharpness,
                         0, 0, inputExtent.width, inputExtent.height, inputExtent.width, inputExtent.height,
                         0, 0, outputExtent.width, outputExtent.height, outputExtent.width, outputExtent.height,
                         NISHDRMode::Linear);

    auto& scalerTex = m_useFp16 ? m_coefScaleTextureFp16 : m_coefScaleTextureFp32;
    auto& usmTex = m_useFp16 ? m_coefUsmTextureFp16 : m_coefUsmTextureFp32;

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getShader());
    ctx->pushConstants(0, sizeof(NISConfig), &nisConfig);

    ctx->bindResourceSampler(NIS_BINDING_SAMPLER_LINEAR_CLAMP, m_sampler);
    ctx->bindResourceView(NIS_BINDING_INPUT, input.view, nullptr);
    ctx->bindResourceView(NIS_BINDING_OUTPUT, output.view, nullptr);
    ctx->bindResourceView(NIS_BINDING_COEF_SCALER, scalerTex.view, nullptr);
    ctx->bindResourceView(NIS_BINDING_COEF_USM, usmTex.view, nullptr);

    uint32_t gridX = uint32_t(std::ceil(outputExtent.width / float(m_blockWidth)));
    uint32_t gridY = uint32_t(std::ceil(outputExtent.height / float(m_blockHeight)));
    ctx->dispatch(gridX, gridY, 1);
  }
} // namespace dxvk
