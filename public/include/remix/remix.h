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

#pragma once

#include "remix_c.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>

#ifndef REMIXAPI_ASSERT
#include <cassert>
#define REMIXAPI_ASSERT(mustBeTrue) assert(mustBeTrue)
#endif

namespace remix {
  namespace detail {
    template< typename T >
    struct Result {
      Result(T&& value)
        : m_value { std::forward< T >(value) }
        , m_status { REMIXAPI_ERROR_CODE_SUCCESS } {
      }

      Result(remixapi_ErrorCode error)
        : m_value {}
        , m_status { error } {
        REMIXAPI_ASSERT(error != REMIXAPI_ERROR_CODE_SUCCESS);
      }

      Result(const Result&) = delete;
      Result(Result&&) = delete;
      Result& operator=(const Result&) = delete;
      Result& operator=(Result&&) = delete;

      operator bool() const {
        return m_status == REMIXAPI_ERROR_CODE_SUCCESS;
      }

      T& value() {
        REMIXAPI_ASSERT(bool { *this });
        return m_value;
      }

      const T& value() const {
        REMIXAPI_ASSERT(bool { *this });
        return m_value;
      }

      remixapi_ErrorCode status() const {
        return m_status;
      }

      const T& operator*() const {
        return value();
      }
      T& operator*() {
        return value();
      }
      const T* operator->() const {
        return &value();
      }
      T* operator->() {
        return &value();
      }

    private:
      const remixapi_ErrorCode m_status;
      T                        m_value;
    };

    template<>
    struct Result< void > {
      Result(remixapi_ErrorCode error) : m_status { error } { }

      Result(const Result&) = delete;
      Result(Result&&) = delete;
      Result& operator=(const Result&) = delete;
      Result& operator=(Result&&) = delete;

      operator bool() const {
        return m_status == REMIXAPI_ERROR_CODE_SUCCESS;
      }

      remixapi_ErrorCode status() const {
        return m_status;
      }

    private:
      const remixapi_ErrorCode m_status;
    };

    template< typename T >
    void assign_if(remixapi_Bool& hasvalue, T& value, const std::optional< T >& src) {
      if (src) {
        hasvalue = true;
        value = src.value();
      } else {
        hasvalue = false;
      }
    }
  }

  template< typename T >
  using Result = detail::Result< T >;

  using StructType = remixapi_StructType;
  using Float2D = remixapi_Float2D;
  using Float3D = remixapi_Float3D;
  using Float4D = remixapi_Float4D;
  using Transform = remixapi_Transform;



  struct MaterialInfo;
  struct MeshInfo;
  struct CameraInfo;
  struct InstanceInfo;
  struct LightInfo;
  namespace detail {
    struct dxvk_ExternalSwapchain;
    struct dxvk_VkImage;
  }

  struct Interface {
    HMODULE            m_RemixDLL { nullptr };
    remixapi_Interface m_CInterface {};

    // Functions
    Result< void >                    Shutdown();
    Result< remixapi_MaterialHandle > CreateMaterial(const MaterialInfo& info);
    Result< void >                    DestroyMaterial(remixapi_MaterialHandle handle);
    Result< remixapi_MeshHandle >     CreateMesh(const MeshInfo& info);
    Result< void >                    DestroyMesh(remixapi_MeshHandle handle);
    Result< void >                    SetupCamera(const CameraInfo& info);
    Result< void >                    DrawInstance(const InstanceInfo& info);
    Result< remixapi_LightHandle >    CreateLight(const LightInfo& info);
    Result< void >                    DestroyLight(remixapi_LightHandle handle);
    Result< void >                    DrawLightInstance(remixapi_LightHandle handle);
    Result< void >                    SetConfigVariable(const char* key, const char* value);

    // DXVK interoperability
    Result< IDirect3D9Ex* >                  dxvk_CreateD3D9(bool disableSrgbConversionForOutput);
    Result< void >                           dxvk_RegisterD3D9Device(IDirect3DDevice9Ex* d3d9Device);
    Result< detail::dxvk_ExternalSwapchain > dxvk_GetExternalSwapchain();
    Result< detail::dxvk_VkImage >           dxvk_GetVkImage(IDirect3DSurface9* source);
    Result< void >                           dxvk_CopyRenderingOutput(IDirect3DSurface9* destination,
                                                                      remixapi_dxvk_CopyRenderingOutputType type);
    Result< void >                           dxvk_SetDefaultOutput(remixapi_dxvk_CopyRenderingOutputType type,
                                                                   const remixapi_Float4D& color);
  };

  namespace lib {
    // Helper function to load a .dll of Remix, and initialize it.
    // pRemixD3D9DllPath is a path to .dll file, e.g. "C:\dxvk-remix-nv\public\bin\d3d9.dll"
    // TODO: wchar_t / char
    [[nodiscard]] inline Result< Interface > loadRemixDllAndInitialize(const char* pRemixD3D9DllPath) {
      {
        auto lastSlash = std::string_view { pRemixD3D9DllPath }.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
          SetDllDirectoryA(
              std::string { std::string_view{ pRemixD3D9DllPath }.substr(0, lastSlash) }
          .c_str());
        }
      }
      
      if (HMODULE remixDll = LoadLibraryA(pRemixD3D9DllPath)) {
        auto pfn_InitializeLibrary = reinterpret_cast<decltype(&remixapi_InitializeLibrary)>(
            GetProcAddress(remixDll, "remixapi_InitializeLibrary"));
      
        if (pfn_InitializeLibrary) {
          remixapi_InitializeLibraryInfo info = {};
          {
            info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
            info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR,
                                                 REMIXAPI_VERSION_MINOR,
                                                 REMIXAPI_VERSION_PATCH);
          }

          remixapi_Interface interfaceInC = {};
          remixapi_ErrorCode status = pfn_InitializeLibrary(&info, &interfaceInC);
      
          if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
            return status;
          }
      
          remix::Interface interfaceInCpp = {};
          {
            interfaceInCpp.m_RemixDLL = remixDll;
            interfaceInCpp.m_CInterface = interfaceInC;
          }
          return interfaceInCpp;
        }
      
        return REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
      }
      
      return REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE;
    }

    inline void shutdownAndUnloadRemixDll(Interface& interfaceInCpp) {
      interfaceInCpp.Shutdown();
      if (interfaceInCpp.m_RemixDLL) {
        FreeLibrary(interfaceInCpp.m_RemixDLL);
      }
      interfaceInCpp = {};
    }
  }



  inline Result< void > Interface::Shutdown() {
    if (m_CInterface.Shutdown) {
      return m_CInterface.Shutdown();
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  inline Result< void > Interface::SetConfigVariable(const char* key, const char* value) {
    return m_CInterface.SetConfigVariable(key, value);
  }



  struct MaterialInfoOpaqueEXT : remixapi_MaterialInfoOpaqueEXT {
    MaterialInfoOpaqueEXT() {
      sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
      pNext = nullptr;
      roughnessTexture = {};
      metallicTexture = {};
      anisotropy = 0.0f;
      albedoConstant = { 1.0f, 1.0f, 1.0f };
      opacityConstant = 1.0f;
      roughnessConstant = 1.0f;
      metallicConstant = 0.0f;
      thinFilmThickness_hasvalue = false;
      thinFilmThickness_value = 200.f;
      alphaIsThinFilmThickness = false;
      heightTexture = {};
      heightTextureStrength = 0.0f;
      useDrawCallAlphaState = true;
      blendType_hasvalue = false;
      blendType_value = 0;
      invertedBlend = false;
      alphaTestType = 7;
      alphaReferenceValue = 0;
      static_assert(sizeof remixapi_MaterialInfoOpaqueEXT == 112);
    }

    void set_roughnessTexture(std::filesystem::path v) {
      cpp_roughnessTexture = std::move(v);
      roughnessTexture = cpp_roughnessTexture.c_str();
    }
    void set_metallicTexture(std::filesystem::path v) {
      cpp_metallicTexture = std::move(v);
      metallicTexture = cpp_metallicTexture.c_str();
    }
    void set_heightTexture(std::filesystem::path v) {
      cpp_heightTexture = std::move(v);
      heightTexture = cpp_heightTexture.c_str();
    }
    void set_thinFilmThickness(const std::optional< float >& v) {
      detail::assign_if(thinFilmThickness_hasvalue, thinFilmThickness_value, v);
    }
    void set_blendType(const std::optional< int >& v) {
      detail::assign_if(blendType_hasvalue, blendType_value, v);
    }

  private:
    std::filesystem::path cpp_roughnessTexture {};
    std::filesystem::path cpp_metallicTexture {};
    std::filesystem::path cpp_heightTexture {};
  };

  // Can be linked to MaterialInfoOpaqueEXT
  struct MaterialInfoOpaqueSubsurfaceEXT : remixapi_MaterialInfoOpaqueSubsurfaceEXT {
    MaterialInfoOpaqueSubsurfaceEXT() {
      sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT;
      pNext = nullptr;
      subsurfaceTransmittanceTexture = {};
      subsurfaceThicknessTexture = {};
      subsurfaceSingleScatteringAlbedoTexture = {};
      subsurfaceTransmittanceColor = { 0.5f, 0.5f, 0.5f };
      subsurfaceMeasurementDistance = 0.0f;
      subsurfaceSingleScatteringAlbedo = { 0.5f, 0.5f, 0.5f };;
      subsurfaceVolumetricAnisotropy = 0.0f;
      static_assert(sizeof remixapi_MaterialInfoOpaqueSubsurfaceEXT == 72);
    }

    void set_subsurfaceTransmittanceTexture(std::filesystem::path v) {
      cpp_subsurfaceTransmittanceTexture = std::move(v);
      subsurfaceTransmittanceTexture = cpp_subsurfaceTransmittanceTexture.c_str();
    }
    void set_subsurfaceThicknessTexture(std::filesystem::path v) {
      cpp_subsurfaceThicknessTexture = std::move(v);
      subsurfaceThicknessTexture = cpp_subsurfaceThicknessTexture.c_str();
    }
    void set_subsurfaceSingleScatteringAlbedoTexture(std::filesystem::path v) {
      cpp_subsurfaceSingleScatteringAlbedoTexture = std::move(v);
      subsurfaceSingleScatteringAlbedoTexture = cpp_subsurfaceSingleScatteringAlbedoTexture.c_str();
    }

  private:
    std::filesystem::path cpp_subsurfaceTransmittanceTexture {};
    std::filesystem::path cpp_subsurfaceThicknessTexture {};
    std::filesystem::path cpp_subsurfaceSingleScatteringAlbedoTexture {};
  };

  struct MaterialInfoTranslucentEXT : remixapi_MaterialInfoTranslucentEXT {
    MaterialInfoTranslucentEXT() {
      sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
      pNext = nullptr;
      transmittanceTexture = {};
      refractiveIndex = 1.3f;
      transmittanceColor = { 0.97f, 0.97f, 0.97f };
      transmittanceMeasurementDistance = 1.0f;
      thinWallThickness_hasvalue = false;
      thinWallThickness_value = 0.001f;
      useDiffuseLayer = false;
      static_assert(sizeof remixapi_MaterialInfoTranslucentEXT == 56);
    }

    void set_transmittanceTexture(std::filesystem::path v) {
      cpp_transmittanceTexture = std::move(v);
      transmittanceTexture = cpp_transmittanceTexture.c_str();
    }
    void set_thinWallThickness(const std::optional< float >& v) {
      detail::assign_if(thinWallThickness_hasvalue, thinWallThickness_value, v);
    }

  private:
    std::filesystem::path cpp_transmittanceTexture {};
  };

  struct MaterialInfoPortalEXT : remixapi_MaterialInfoPortalEXT {
    MaterialInfoPortalEXT() {
      sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT;
      pNext = nullptr;
      rayPortalIndex = 0;
      rotationSpeed = 0.0f;
      static_assert(sizeof remixapi_MaterialInfoPortalEXT == 24);
    }
  };

  struct MaterialInfo : remixapi_MaterialInfo {
    MaterialInfo() {
      sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
      pNext = nullptr;
      hash = 0;
      albedoTexture = {};
      normalTexture = {};
      tangentTexture = {};
      emissiveTexture = {};
      emissiveIntensity = 0.0f;
      emissiveColorConstant = { 0.0f, 0.0f, 0.0f };
      spriteSheetRow = 1;
      spriteSheetCol = 1;
      spriteSheetFps = 0;
      filterMode = 1; // Linear
      wrapModeU = 0; // Repeat
      wrapModeV = 0; // Repeat
      static_assert(sizeof remixapi_MaterialInfo == 80);
    }

    void set_albedoTexture(std::filesystem::path v) {
      cpp_albedoTexture = std::move(v);
      albedoTexture = cpp_albedoTexture.c_str();
    }
    void set_normalTexture(std::filesystem::path v) {
      cpp_normalTexture = std::move(v);
      normalTexture = cpp_normalTexture.c_str();
    }
    void set_tangentTexture(std::filesystem::path v) {
      cpp_tangentTexture = std::move(v);
      tangentTexture = cpp_tangentTexture.c_str();
    }
    void set_emissiveTexture(std::filesystem::path v) {
      cpp_emissiveTexture = std::move(v);
      emissiveTexture = cpp_emissiveTexture.c_str();
    }

  private:
    std::filesystem::path cpp_albedoTexture {};
    std::filesystem::path cpp_normalTexture {};
    std::filesystem::path cpp_tangentTexture {};
    std::filesystem::path cpp_emissiveTexture {};
  };

  inline Result< remixapi_MaterialHandle > Interface::CreateMaterial(const MaterialInfo& info) {
    remixapi_MaterialHandle handle = nullptr;
    remixapi_ErrorCode status = m_CInterface.CreateMaterial(&info, &handle);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return handle;
  }

  inline Result< void > Interface::DestroyMaterial(remixapi_MaterialHandle handle) {
    return m_CInterface.DestroyMaterial(handle);
  }



  struct MeshInfo : remixapi_MeshInfo {
    MeshInfo() {
      sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
      pNext = nullptr;
      hash = 0;
      surfaces_values = {};
      surfaces_count = 0;
      static_assert(sizeof remixapi_MeshInfo == 40);
    }
  };

  inline Result< remixapi_MeshHandle > Interface::CreateMesh(const MeshInfo& info) {
    remixapi_MeshHandle handle = nullptr;
    remixapi_ErrorCode status = m_CInterface.CreateMesh(&info, &handle);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return handle;
  }

  inline Result< void > Interface::DestroyMesh(remixapi_MeshHandle handle) {
    return m_CInterface.DestroyMesh(handle);
  }



  using CameraType = remixapi_CameraType;

  // Ignores view / projection matrices from CameraInfo
  // by recalculating them from the given arguments in this struct.
  struct CameraInfoParameterizedEXT : remixapi_CameraInfoParameterizedEXT {
    CameraInfoParameterizedEXT() {
      sType = { REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT };
      pNext = { nullptr };
      position = { 0, 0, 0 };
      forward = { 0, 0, 1 };
      up = { 0, 1, 0 };
      right = { 1, 0, 0 };
      fovYInDegrees = 75.f;
      aspect = 16.f / 9.f;
      nearPlane = 0.1f;
      farPlane = 1000.f;
      static_assert(sizeof remixapi_CameraInfoParameterizedEXT == 80);
    }
  };

  struct CameraInfo : remixapi_CameraInfo {
    CameraInfo() {
      sType = { REMIXAPI_STRUCT_TYPE_CAMERA_INFO };
      pNext = { nullptr };
      type = { REMIXAPI_CAMERA_TYPE_WORLD };
      view[0][0] = view[1][1] = view[2][2] = view[3][3] = 1.f;
      projection[0][0] = projection[1][1] = projection[2][2] = projection[3][3] = 1.f;
      static_assert(sizeof remixapi_CameraInfo == 152);
    }
  };

  inline Result< void > Interface::SetupCamera(const CameraInfo& info) {
    return m_CInterface.SetupCamera(&info);
  }



  struct InstanceInfoBoneTransformsEXT : remixapi_InstanceInfoBoneTransformsEXT {
    InstanceInfoBoneTransformsEXT() {
      sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
      pNext = nullptr;
      boneTransforms_count = 0;
      boneTransforms_values = {};
      static_assert(sizeof remixapi_InstanceInfoBoneTransformsEXT == 32);
    }
  };

  struct InstanceInfoBlendEXT : remixapi_InstanceInfoBlendEXT {
    InstanceInfoBlendEXT() {
      sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
      pNext = nullptr;
      alphaTestEnabled = false;
      alphaTestReferenceValue = 0;
      alphaTestCompareOp = 7 /* VK_COMPARE_OP_ALWAYS */;
      alphaBlendEnabled = false;
      srcColorBlendFactor = 1 /* VK_BLEND_FACTOR_ONE */;
      dstColorBlendFactor = 0 /* VK_BLEND_FACTOR_ZERO */;
      colorBlendOp = 0 /* VK_BLEND_OP_ADD */;
      textureColorArg1Source = 1 /* RtTextureArgSource::Texture */;
      textureColorArg2Source = 0 /* RtTextureArgSource::None */;
      textureColorOperation = 3 /* DxvkRtTextureOperation::Modulate */;
      textureAlphaArg1Source = 1 /* RtTextureArgSource::Texture */;
      textureAlphaArg2Source = 0 /* RtTextureArgSource::None */;
      textureAlphaOperation = 1 /* DxvkRtTextureOperation::SelectArg1 */;
      tFactor = 0XFFFFFFFF;
      isTextureFactorBlend = false;
      static_assert(sizeof remixapi_InstanceInfoBlendEXT == 80);
    }
  };

  struct InstanceInfoObjectPickingEXT : remixapi_InstanceInfoObjectPickingEXT {
    InstanceInfoObjectPickingEXT() {
      sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT;
      pNext = nullptr;
      objectPickingValue = 0;
    }
  };

  using InstanceCategoryBit = remixapi_InstanceCategoryBit;
  using InstanceCategoryFlags = remixapi_InstanceCategoryFlags;

  struct InstanceInfo : remixapi_InstanceInfo {
    InstanceInfo() {
      sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
      pNext = nullptr;
      categoryFlags = 0;
      mesh = 0;
      transform = {};
      doubleSided = false;
      static_assert(sizeof remixapi_InstanceInfo == 88);
    }
  };

  inline Result< void > Interface::DrawInstance(const InstanceInfo& info) {
    return m_CInterface.DrawInstance(&info);
  }



  namespace detail {
    inline remixapi_LightInfoLightShaping defaultLightShaping() {
      remixapi_LightInfoLightShaping shaping {};
      {
        shaping.primaryAxis = { 0.0f, 0.0f, 1.0f };
        shaping.coneAngleDegrees = 180.0f;
        shaping.coneSoftness = 0.0f;
        shaping.focusExponent = 0.0f;
      }
      return shaping;
    };
  }

  using LightInfoLightShaping = remixapi_LightInfoLightShaping;

  struct LightInfoSphereEXT : remixapi_LightInfoSphereEXT {
    LightInfoSphereEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
      pNext = nullptr;
      position = { 0.0f, 0.0f, 0.0f };
      radius = 0.05f;
      shaping_hasvalue = false;
      shaping_value = detail::defaultLightShaping();
      static_assert(sizeof remixapi_LightInfoSphereEXT == 64);
    }

    void set_shaping(const std::optional< remixapi_LightInfoLightShaping >& v) {
      detail::assign_if(shaping_hasvalue, shaping_value, v);
    }
  };

  struct LightInfoRectEXT : remixapi_LightInfoRectEXT {
    LightInfoRectEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
      pNext = nullptr;
      position = { 0.0f, 0.0f, 0.0f };
      xAxis = { 1.0f, 0.0f, 0.0f };
      xSize = 1.0f;
      yAxis = { 0.0f, 1.0f, 0.0f };
      ySize = 1.0f;
      shaping_hasvalue = false;
      shaping_value = detail::defaultLightShaping();
      static_assert(sizeof remixapi_LightInfoRectEXT == 88);
    }

    void set_shaping(const std::optional< remixapi_LightInfoLightShaping >& v) {
      detail::assign_if(shaping_hasvalue, shaping_value, v);
    }
  };

  struct LightInfoDiskEXT : remixapi_LightInfoDiskEXT {
    LightInfoDiskEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT;
      pNext = nullptr;
      position = { 0.0f, 0.0f, 0.0f };
      xAxis = { 1.0f, 0.0f, 0.0f };
      xRadius = 1.0f;
      yAxis = { 0.0f, 1.0f, 0.0f };
      yRadius = 1.0f;
      shaping_hasvalue = false;
      shaping_value = detail::defaultLightShaping();
      static_assert(sizeof remixapi_LightInfoDiskEXT == 88);
    }

    void set_shaping(const std::optional< remixapi_LightInfoLightShaping >& v) {
      detail::assign_if(shaping_hasvalue, shaping_value, v);
    }
  };

  struct LightInfoCylinderEXT : remixapi_LightInfoCylinderEXT {
    LightInfoCylinderEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT;
      pNext = nullptr;
      position = { 0.0f, 0.0f, 0.0f };
      radius = 1.0f;
      axis = { 1.0f, 0.0f, 0.0f };
      axisLength = 1.0f;
      static_assert(sizeof remixapi_LightInfoCylinderEXT == 48);
    }
  };

  struct LightInfoDistantEXT : remixapi_LightInfoDistantEXT {
    LightInfoDistantEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
      pNext = nullptr;
      direction = { 0.0f, -1.0f, 0.0f };
      angularDiameterDegrees = 0.5f;
      static_assert(sizeof remixapi_LightInfoDistantEXT == 32);
    }
  };

  struct LightInfoDomeEXT : remixapi_LightInfoDomeEXT {
    LightInfoDomeEXT() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
      pNext = nullptr;
      transform = {};
      colorTexture = {};
      static_assert(sizeof remixapi_LightInfoDomeEXT == 72);
    }

    void set_colorTexture(std::filesystem::path v) {
      cpp_colorTexture = std::move(v);
      colorTexture = cpp_colorTexture.c_str();
    }

  private:
    std::filesystem::path cpp_colorTexture {};
  };

  struct LightInfo : remixapi_LightInfo {
    LightInfo() {
      sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
      pNext = nullptr;
      hash = 0;
      radiance = { 1.0f, 1.0f, 1.0f };
      static_assert(sizeof remixapi_LightInfo == 40);
    }
  };

  inline Result< remixapi_LightHandle > Interface::CreateLight(const LightInfo& info) {
    remixapi_LightHandle handle = nullptr;
    remixapi_ErrorCode status = m_CInterface.CreateLight(&info, &handle);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return handle;
  }

  inline Result< void > Interface::DestroyLight(remixapi_LightHandle handle) {
    return m_CInterface.DestroyLight(handle);
  }

  inline Result< void > Interface::DrawLightInstance(remixapi_LightHandle handle) {
    return m_CInterface.DrawLightInstance(handle);
  }

  namespace detail {
    struct dxvk_ExternalSwapchain {
      uint64_t vkImage;
      uint64_t vkSemaphoreRenderingDone;
      uint64_t vkSemaphoreResumeSemaphore;
    };

    struct dxvk_VkImage {
      uint64_t vkImage;
    };
  }

  inline Result< IDirect3D9Ex* > Interface::dxvk_CreateD3D9(bool disableSrgbConversionForOutput) {
    IDirect3D9Ex* d3d9 { nullptr };
    remixapi_ErrorCode status = m_CInterface.dxvk_CreateD3D9(disableSrgbConversionForOutput, &d3d9);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return d3d9;
  }

  inline Result< void > Interface::dxvk_RegisterD3D9Device(IDirect3DDevice9Ex* d3d9Device) {
    return m_CInterface.dxvk_RegisterD3D9Device(d3d9Device);
  }

  inline Result< detail::dxvk_ExternalSwapchain > Interface::dxvk_GetExternalSwapchain() {
    detail::dxvk_ExternalSwapchain externalSwapchain {};
    remixapi_ErrorCode status = m_CInterface.dxvk_GetExternalSwapchain(
      &externalSwapchain.vkImage,
      &externalSwapchain.vkSemaphoreRenderingDone,
      &externalSwapchain.vkSemaphoreResumeSemaphore);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return externalSwapchain;
  }

  inline Result< detail::dxvk_VkImage > Interface::dxvk_GetVkImage(IDirect3DSurface9* source) {
    detail::dxvk_VkImage externalImage {};
    remixapi_ErrorCode status = m_CInterface.dxvk_GetVkImage(source, &externalImage.vkImage);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      return status;
    }
    return externalImage;
  }

  inline Result< void > Interface::dxvk_CopyRenderingOutput(
      IDirect3DSurface9* destination, remixapi_dxvk_CopyRenderingOutputType type) {
    return m_CInterface.dxvk_CopyRenderingOutput(destination, type);
  }

  inline Result< void > Interface::dxvk_SetDefaultOutput(
      remixapi_dxvk_CopyRenderingOutputType type, const remixapi_Float4D& color) {
    return m_CInterface.dxvk_SetDefaultOutput(type, color);
  }
}
