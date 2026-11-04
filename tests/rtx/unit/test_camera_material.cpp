/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>

// NV-DXVK start: USD camera material authoring coverage.
#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include "../../../src/lssusd/usd_include_end.h"
// NV-DXVK end

#include "../../../src/dxvk/rtx_render/rtx_materials.h"
// NV-DXVK start: Camera identity and material reload regression coverage.
#include "../../../src/dxvk/rtx_render/rtx_types.h"
#include "../../../src/dxvk/rtx_render/rtx_asset_replacer.h"
#include "../../../src/dxvk/dxvk_instance.h"
#include "../../../src/dxvk/dxvk_device.h"
// NV-DXVK end

// NV-DXVK start: Raytraced camera material packing coverage.
namespace dxvk {
  Logger Logger::s_instance("test_camera_material.log");

  class CameraTestAsset final : public AssetData {
  public:
    explicit CameraTestAsset(XXH64_hash_t hash) { m_hash = hash; }
    const void* data(int, int) override { return nullptr; }
    void placement(int, int, int, uint64_t& offset, size_t& size) const override { offset = 0; size = 0; }
    void evictCache(int, int) override { }
    void releaseSource() override { }
  };

  static bool testCameraMaterialIdentity(const Rc<DxvkDevice>& pDevice = {}) {
    auto makeTexture = [&](XXH64_hash_t hash, bool renderTarget) {
      if (pDevice != nullptr) {
        DxvkImageCreateInfo info {};
        info.type = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
        info.extent = { 16, 16, 1 };
        info.numLayers = 1;
        info.mipLevels = 1;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (renderTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0);
        info.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        info.access = VK_ACCESS_SHADER_READ_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const auto pImage = pDevice->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXRenderTarget, "Camera identity regression");
        pImage->setHash(hash);
        // D3D9 assigns the descriptor identifier when it creates a render-target texture.
        pImage->setDescriptorHash(renderTarget ? 0xca4e4a : kEmptyHash);
        DxvkImageViewCreateInfo view {};
        view.format = info.format;
        view.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        view.numLevels = 1;
        view.numLayers = 1;
        return TextureRef(pDevice->createImageView(pImage, view));
      }
      Rc<ManagedTexture> pTexture = new ManagedTexture;
      pTexture->m_uniqueKey = hash;
      pTexture->m_assetData = new CameraTestAsset(hash);
      return TextureRef(pTexture);
    };
    const TextureRef ordinary = makeTexture(0x1111, false);
    const TextureRef camera = makeTexture(0x2222, true);
    DrawCallState draw;
    auto& legacy = draw.modifyMaterialData();
    // Bind the same two inputs as a programmable draw. Both use the default UV transform.
    const_cast<TextureRef&>(legacy.getColorTexture()) = ordinary;
    const_cast<TextureRef&>(legacy.getColorTexture2()) = camera;
    legacy.setHashOverride(ordinary.getImageHash());
    if (pDevice != nullptr) {
      draw.setRaytracedRenderTarget(camera.getImageView()->image());
      const TextureRef sameDescriptorTarget = makeTexture(0x3333, true);
      const auto pAliasView = pDevice->createImageView(camera.getImageView()->image(), camera.getImageView()->info());
      if (!draw.isRaytracedRenderTargetTexture(camera) || !draw.isRaytracedRenderTargetTexture(TextureRef(pAliasView)) ||
          draw.isRaytracedRenderTargetTexture(sameDescriptorTarget) ||
          sameDescriptorTarget.getImageView()->image()->getDescriptorHash() != draw.raytracedRenderTargetDescriptorHash) {
        std::cerr << "Camera alpha must follow resource identity across views, not descriptor equality" << std::endl;
        return false;
      }
    } else {
      draw.isUsingRaytracedRenderTarget = true;
      draw.raytracedRenderTargetDescriptorHash = 0xca4e4a;
    }
    const XXH64_hash_t cameraDescriptor = draw.raytracedRenderTargetDescriptorHash;

    AssetReplacements replacements;
    OpaqueMaterialData authored;
    authored.setOpacityConstant(0.5f);
    const auto descriptorMaterial = replacements.storeMaterial(cameraDescriptor, MaterialData(authored));
    auto lookup = [&](XXH64_hash_t hash) {
      std::shared_ptr<MaterialData> material;
      replacements.getMaterial(hash, material);
      return material;
    };
    const auto replacement = draw.findMaterialReplacement(lookup);
    if (replacement != descriptorMaterial || draw.getCapturedMaterialHash(legacy.getHash()) != cameraDescriptor) {
      std::cerr << "Slot-1 camera descriptor must drive replacement and capture naming" << std::endl;
      return false;
    }
    MaterialData merged = *replacement;
    merged.mergeLegacyMaterial(legacy);
    if (merged.getOpaqueMaterialData().getAlbedoOpacityTexture().getUniqueKey() != ordinary.getUniqueKey() ||
        draw.isRaytracedRenderTargetTexture(merged.getOpaqueMaterialData().getAlbedoOpacityTexture()) ||
        legacy.getColorTexture2().getUniqueKey() != camera.getUniqueKey()) {
      std::cerr << "Camera lookup must preserve the ordinary slot-0 opacity mask and both bindings" << std::endl;
      return false;
    }

    AssetReplacements reloaded;
    authored.setOpacityConstant(0.25f);
    const auto updatedMaterial = reloaded.storeMaterial(cameraDescriptor, MaterialData(authored));
    AssetChanges changes;
    changes.dirtyMatHashes.insert(cameraDescriptor);
    replacements.mergeFrom(std::move(reloaded), changes);
    if (!draw.isCameraMaterialDirty(changes.dirtyMatHashes) || draw.findMaterialReplacement(lookup) != updatedMaterial ||
        draw.isCameraMaterialDirty({ 0xdead })) {
      std::cerr << "Hot reload must invalidate and resolve the categorized camera descriptor" << std::endl;
      return false;
    }

    const auto rawMaterial = replacements.storeMaterial(legacy.getHash(), MaterialData(OpaqueMaterialData {}));
    if (draw.findMaterialReplacement(lookup) != rawMaterial) {
      std::cerr << "Legacy material hashes must retain precedence over the camera descriptor" << std::endl;
      return false;
    }
    draw.setRaytracedRenderTarget(nullptr);
    if (draw.isUsingRaytracedRenderTarget || draw.raytracedRenderTargetDescriptorHash != kEmptyHash ||
        draw.raytracedRenderTargetImage != nullptr || draw.isCameraMaterialDirty(changes.dirtyMatHashes) ||
        draw.getCapturedMaterialHash(legacy.getHash()) != legacy.getHash()) {
      std::cerr << "Camera classification and identity must reset together for the next draw" << std::endl;
      return false;
    }
    return true;
  }
}

int main(int argc, char** argv) {
  using namespace dxvk;

  if (argc == 2 && std::string_view(argv[1]) == "--gpu-resource-identity") {
    try {
      const Rc<DxvkInstance> pInstance = new DxvkInstance;
      const auto pAdapter = pInstance->enumAdapters(0);
      if (pAdapter == nullptr) {
        std::cerr << "No Vulkan adapter available for the camera resource regression" << std::endl;
        return -1;
      }
      const auto pDevice = pAdapter->createDevice(pInstance, pAdapter->features());
      return testCameraMaterialIdentity(pDevice) ? 0 : -1;
    } catch (const std::exception& error) {
      std::cerr << error.what() << std::endl;
      return -1;
    }
  }

  if (!testCameraMaterialIdentity()) {
    return -1;
  }

  const auto stage = pxr::UsdStage::CreateInMemory();
  const auto shader = stage->DefinePrim(pxr::SdfPath("/CameraShader"), pxr::TfToken("Shader"));
  const auto opacityAttribute = shader.CreateAttribute(pxr::TfToken("inputs:opacity_constant"), pxr::SdfValueTypeNames->Float);
  const auto emissionAttribute = shader.CreateAttribute(pxr::TfToken("inputs:enable_emission"), pxr::SdfValueTypeNames->Bool);
  const auto intensityAttribute = shader.CreateAttribute(pxr::TfToken("inputs:emissive_intensity"), pxr::SdfValueTypeNames->Float);
  for (bool emission : { false, true }) {
    for (float opacity : { 0.0f, 0.5f, 1.0f }) {
      for (float intensity : { 0.0f, 25.0f }) {
        opacityAttribute.Set(opacity);
        emissionAttribute.Set(emission);
        intensityAttribute.Set(intensity);
        const auto authoredMaterial = OpaqueMaterialData::deserialize(
          [](const pxr::UsdPrim&, const pxr::TfToken&) { return TextureRef {}; }, shader);
        MaterialData material(authoredMaterial);
        material.mergeLegacyMaterial(LegacyMaterialData {});
        const auto& mergedMaterial = material.getOpaqueMaterialData();
        if (mergedMaterial.getOpacityConstant() != opacity ||
            mergedMaterial.getEnableEmission() != emission ||
            mergedMaterial.getEmissiveIntensity() != intensity) {
          std::cerr << "Camera controls did not survive USD deserialization and legacy material merging" << std::endl;
          return -1;
        }
      }
    }
  }

  auto makeMaterial = [](bool camera, bool emission, float intensity, float opacity, bool cameraTexture = false) {
    constexpr uint32_t kNoTexture = BINDING_INDEX_INVALID;
    return RtOpaqueSurfaceMaterial(
      kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture,
      0.0f, intensity, Vector4(1.0f, 1.0f, 1.0f, opacity), 0.5f, 0.0f,
      Vector3(0.2f, 0.4f, 0.8f), emission, false, false, false, 0.0f,
      kNoTexture, 0.0f, 0.0f, false, 1.0f, 0.3f, 0.7f,
      SURFACE_INDEX_INVALID, camera, false, SAMPLER_FEEDBACK_INVALID, kNoTexture, cameraTexture);
  };

  constexpr size_t kStartOffset = 8;
  std::array<unsigned char, kSurfaceMaterialGPUSize + 16> storage;
  for (bool camera : { false, true }) {
    for (bool emission : { false, true }) {
      for (float opacity : { 0.0f, 0.25f, 0.75f, 1.0f }) {
        for (float intensity : { 0.0f, 1.0f, 40.0f, 65504.0f }) {
          const auto material = makeMaterial(camera, emission, intensity, opacity);
          storage.fill(0xcd);
          size_t offset = kStartOffset;
          material.writeGPUData(storage.data(), offset);
          uint16_t flags;
          uint16_t packedOpacity;
          uint16_t packedIntensity;
          std::memcpy(&flags, storage.data() + kStartOffset, sizeof(flags));
          std::memcpy(&packedOpacity, storage.data() + kStartOffset + 14, sizeof(packedOpacity));
          std::memcpy(&packedIntensity, storage.data() + kStartOffset + 38, sizeof(packedIntensity));
          if (offset != kStartOffset + 64 ||
              bool(flags & OPAQUE_SURFACE_MATERIAL_FLAG_IS_RAYTRACED_RENDER_TARGET) != camera ||
              bool(flags & OPAQUE_SURFACE_MATERIAL_FLAG_ENABLE_RENDER_TARGET_EMISSION) != (camera && emission) ||
              glm::unpackHalf1x16(packedOpacity) != opacity ||
              glm::unpackHalf1x16(packedIntensity) != (emission ? intensity : 0.0f)) {
            std::cerr << "Invalid camera material packing: camera=" << camera
              << " emission=" << emission << " opacity=" << opacity << " intensity=" << intensity << std::endl;
            return -1;
          }
          for (size_t i = 0; i < kStartOffset; ++i) {
            if (storage[i] != 0xcd || storage[kStartOffset + kSurfaceMaterialGPUSize + i] != 0xcd) {
              std::cerr << "Camera material write exceeded its 64-byte slot" << std::endl;
              return -1;
            }
          }
        }
      }
    }
  }

  const auto disabled = makeMaterial(true, false, 0.0f, 1.0f);
  const auto enabledZero = makeMaterial(true, true, 0.0f, 1.0f);
  const auto enabled = makeMaterial(true, true, 40.0f, 1.0f);
  const auto transparent = makeMaterial(true, true, 40.0f, 0.25f);
  const auto originalTexture = makeMaterial(true, true, 40.0f, 1.0f, true);
  size_t offset = kStartOffset;
  originalTexture.writeGPUData(storage.data(), offset);
  uint16_t flags;
  std::memcpy(&flags, storage.data() + kStartOffset, sizeof(flags));
  if (!(flags & OPAQUE_SURFACE_MATERIAL_FLAG_ALBEDO_IS_RENDER_TARGET) || originalTexture == enabled) {
    std::cerr << "Original camera textures and authored masks must have distinct flags and cache keys" << std::endl;
    return -1;
  }
  if (disabled == enabledZero || enabledZero == enabled || enabled == transparent) {
    std::cerr << "Distinct camera controls share a material cache key" << std::endl;
    return -1;
  }
  return 0;
}
// NV-DXVK end
