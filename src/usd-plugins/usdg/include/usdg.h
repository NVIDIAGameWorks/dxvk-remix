/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#pragma warning(disable : 4244 4305 4003 4251)

#include <string>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/sdf/fileFormat.h>

#ifdef WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

#ifndef USGD_IS_BIG_ENDIAN
#define USGD_IS_BIG_ENDIAN (*reinterpret_cast<const uint16_t*>("\0\xff") < 0x100)
#endif

namespace usdg {

// USD types plus integer 16-bit types
static inline const char* kTypeNames[] = {
  "undefined",
  "bool", "uchar", "short", "ushort", "int", "uint",
  "int64", "uint64", "float", "half", "double",

  "int2", "uint2", "float2", "half2", "double2",
  "int3", "uint3", "float3", "half3", "double3",
  "int4", "uint4", "float4", "half4", "double4",
};

static inline size_t kTypeSizes[] = {
  0, sizeof(bool), sizeof(uint8_t), sizeof(uint16_t), sizeof(uint16_t),
  sizeof(int32_t), sizeof(int32_t), sizeof(int64_t), sizeof(int64_t),
  sizeof(float), 2, sizeof(double),

  2 * sizeof(int32_t), 2 * sizeof(int32_t), 2 * sizeof(float), 2 * 2, 2 * sizeof(double),
  3 * sizeof(int32_t), 3 * sizeof(int32_t), 3 * sizeof(float), 3 * 2, 3 * sizeof(double),
  4 * sizeof(int32_t), 4 * sizeof(int32_t), 4 * sizeof(float), 4 * 2, 4 * sizeof(double),
};

static inline const char* kAttributeNames[] = {
  "undefined",
  "position", "normal", "uv", "color", "blendIndices", "blendWeight",
  "index"
};

static inline const std::string kUsdg("usdg");
static inline const pxr::TfToken kBaked(kUsdg + ":baked");
static inline const pxr::TfToken kAabb(kUsdg + ":aabb");
static inline const pxr::TfToken kNumBones(kUsdg + ":numBones");
static inline const std::string kIndicesName("indices");
static inline const std::string kVerticesName("vertices");

enum class Type : uint8_t {
  Undefined,
  Bool, UChar, Short, UShort, Int, UInt,
  Int64, UInt64, Float, Half, Double,
  Int2, UInt2, Float2, Half2, Double2,
  Int3, UInt3, Float3, Half3, Double3,
  Int4, UInt4, Float4, Half4, Double4,
};

enum class Attribute : uint8_t {
  Undefined,
  Position, Normal, Uv, Color, BlendIndices, BlendWeight,
  Index
};

static inline bool IsUsdg(const std::string& path) {
  // USD runtime uses file extension to determine the file format.
  // Check that the path ends in 'usdg' or 'USDG'.
  if (path.size() > 4) {
    const uint32_t ext = *reinterpret_cast<const uint32_t*>(&path.back() - 3);
    return USGD_IS_BIG_ENDIAN ? (ext == 'usdg' || ext == 'USDG')
                              : (ext == 'gdsu' || ext == 'GDSU');
  }
  return false;
}

static inline bool IsPathSeparator(char c) {
  return c == '\\' || c == '/';
}

// Check if the path is packaged USD path.
// The packaged USD path format is: full_package_path[package_relative_path]
static inline bool IsPackagedPath(const std::string& path) {
  return path.back() == ']';
}

// Extracts the package-relative portion of a packaged path if any.
// Returns the original path if not packaged.
static inline std::string GetPackageRelativePath(const std::string& path) {
  if (!IsPackagedPath(path)) {
    return path;
  }

  const size_t packageRelativePathPos = path.find('[') + 1;
  return path.substr(packageRelativePathPos, path.length() - packageRelativePathPos - 1);
}

// Makes the full path from packaged USD path.
// Returns true if the path is packaged path, or false if not packaged.
static inline bool MakeFullPathFromPackageRelativePath(std::string& path) {
  if (!IsPackagedPath(path)) {
    return false;
  }

  // Remove the ']'
  path.back() = '\0';

  // Find package-relative path position
  const size_t packageRelativePathPos = path.find('[') + 1;

  // Go back and find start of the package name
  size_t packageNamePos = packageRelativePathPos;
  while (!IsPathSeparator(path[packageNamePos])) {
    --packageNamePos;
  }

  // Erase the package name
  path.erase(packageNamePos + 1, packageRelativePathPos - packageNamePos - 1);

  return true;
}

static inline bool IsBaked(const pxr::UsdObject& obj) {
  return obj.HasAuthoredCustomData() &&
    obj.GetCustomDataByKey(kBaked).Get<bool>();
}

static inline bool IsBaked(const pxr::SdfPrimSpecHandle& spec) {
  pxr::VtValue v = spec->GetLayer()->GetFieldDictValueByKey(spec->GetPath(),
      pxr::SdfFieldKeys->CustomData, kBaked);
  return v.UncheckedGet<bool>();
}

template<typename T>
inline bool GetCustomMetadata(const pxr::UsdObject& obj,
    const pxr::TfToken& keyPath, T& out) {
  return obj.GetMetadataByDictKey<T>(pxr::SdfFieldKeys->CustomData,
      keyPath, &out);
}

template<typename T>
inline bool GetCustomMetadata(const pxr::SdfPrimSpecHandle& spec,
    const pxr::TfToken& keyPath, T& out) {
  pxr::VtValue v = spec->GetLayer()->GetFieldDictValueByKey(spec->GetPath(),
      pxr::SdfFieldKeys->CustomData, keyPath);

  if (v.IsHolding<T>()) {
    out = v.UncheckedGet<T>();
    return true;
  }

  return false;
}

template<typename T>
inline void SetCustomMetadata(const pxr::UsdObject& obj,
    const pxr::TfToken& keyPath, const T& in) {
  obj.SetMetadataByDictKey<T>(pxr::SdfFieldKeys->CustomData,
      keyPath, in);
}

template<typename T>
inline void SetCustomMetadata(const pxr::SdfPrimSpecHandle& spec,
    const pxr::TfToken& keyPath, const T& in) {
  spec->GetLayer()->SetFieldDictValueByKey(spec->GetPath(),
      pxr::SdfFieldKeys->CustomData, keyPath, in);
}

static inline void ClearAttribute(const pxr::UsdPrim& prim,
    const pxr::TfToken& attrib) {
  for (auto spec : prim.GetPrimStack()) {
    auto attribs = spec->GetAttributes();
    auto ait = attribs.find(attrib);
    if (ait != attribs.end()) {
      (*ait)->ClearDefaultValue();
    }
  }
}

// Resolves a full path to an attribute of a primitive in
// the USD composition order.
static inline std::string ResolvePath(const pxr::UsdPrim& prim,
    const pxr::TfToken& attrib) {
  for (auto spec : prim.GetPrimStack()) {
    auto attribs = spec->GetAttributes();
    auto ait = attribs.find(attrib);
    if (ait != attribs.end()) {
      return spec->GetLayer()->GetRealPath() +
        spec->GetPath().GetString();
    }
  }

  return prim.GetPath().GetString();
}

static inline std::string ResolvePath(const pxr::SdfPrimSpecHandle& spec) {
  return spec->GetLayer()->GetRealPath() + spec->GetPath().GetString();
}

static inline pxr::SdfPrimSpecHandle GetPrimSpecForAttrib(
    const pxr::UsdPrim& prim, const pxr::TfToken& attrib) {
  for (auto spec : prim.GetPrimStack()) {
    auto attribs = spec->GetAttributes();
    auto ait = attribs.find(attrib);
    if (ait != attribs.end()) {
      return spec;
    }
  }
  return pxr::SdfPrimSpecHandle {};
}

static const char* ToTypeName(const usdg::Type type) {
  return kTypeNames[static_cast<uint8_t>(type)];
}

static size_t GetTypeSize(usdg::Type type) {
  return kTypeSizes[static_cast<uint8_t>(type)];
}

static size_t GetPackedSize(usdg::Type type, size_t count) {
  const size_t typeSize = usdg::GetTypeSize(type);
  const size_t size = typeSize * count;

  if (typeSize == 1) {
    // Align size to pack byte-sized types into dwords
    return (size + 3) & ~3;
  }

  return size;
}

template<typename T>
static inline usdg::Type GetVectorType(char digit) {
  return usdg::Type::Unknown;
}

template<>
static inline usdg::Type GetVectorType<int32_t>(char digit) {
  switch (digit - '0') {
  case 1:
  default:
    break;
  case 2:
    return usdg::Type::Int2;
  case 3:
    return usdg::Type::Int3;
  case 4:
    return usdg::Type::Int4;
  }

#ifdef _DEBUG
  if (digit - '0' > 9) {
    throw;
  }
#endif

  return usdg::Type::Int;
}

template<>
static inline usdg::Type GetVectorType<uint32_t>(char digit) {
  switch (digit - '0') {
  case 1:
  default:
    break;
  case 2:
    return usdg::Type::UInt2;
  case 3:
    return usdg::Type::UInt3;
  case 4:
    return usdg::Type::UInt4;
  }

#ifdef _DEBUG
  if (digit - '0' > 9) {
    throw;
  }
#endif

  return usdg::Type::UInt;
}

template<>
static inline usdg::Type GetVectorType<float>(char digit) {
  switch (digit - '0') {
  case 1:
  default:
    break;
  case 2:
    return usdg::Type::Float2;
  case 3:
    return usdg::Type::Float3;
  case 4:
    return usdg::Type::Float4;
  }

#ifdef _DEBUG
  if (digit - '0' > 9) {
    throw;
  }
#endif

  return usdg::Type::Float;
}

template<>
static inline usdg::Type GetVectorType<double>(char digit) {
  switch (digit - '0') {
  case 1:
  default:
    break;
  case 2:
    return usdg::Type::Double2;
  case 3:
    return usdg::Type::Double3;
  case 4:
    return usdg::Type::Double4;
  }

#ifdef _DEBUG
  if (digit - '0' > 9) {
    throw;
  }
#endif

  return usdg::Type::Double;
}

template<>
static inline usdg::Type GetVectorType<short>(char digit) {
  // Using short for half here
  switch (digit - '0') {
  case 1:
  default:
    break;
  case 2:
    return usdg::Type::Half2;
  case 3:
    return usdg::Type::Half3;
  case 4:
    return usdg::Type::Half4;
  }

#ifdef _DEBUG
  if (digit - '0' > 9) {
    throw;
  }
#endif

  return usdg::Type::Half;
}

static inline usdg::Type ToType(const char* type, size_t len) {
  uint16_t typeId = *reinterpret_cast<const uint16_t*>(type);

  if (!USGD_IS_BIG_ENDIAN) {
    typeId = ((typeId & 0xff) << 8) | (typeId >> 8);
  }

  switch (typeId) {
  case 'bo':
    return usdg::Type::Bool;
  case 'uc':
    return usdg::Type::UChar;

  case 'sh':
    return usdg::Type::Short;
  case 'us':
    return usdg::Type::UShort;

  case 'in':
    if (len > 3) {
      if (len == 5) {
        return usdg::Type::Int64;
      }

      return GetVectorType<int32_t>(type[3]);
    }
    return usdg::Type::Int;
  case 'ui':
    if (len > 4) {
      if (len == 6) {
        return usdg::Type::UInt64;
      }
      return GetVectorType<uint32_t>(type[4]);
    }
    return usdg::Type::UInt;
  case 'ha':
    if (len == 5) {
      return GetVectorType<short>(type[4]);
    }
    return usdg::Type::Half;
  case 'fl':
    if (len == 6) {
      return GetVectorType<float>(type[5]);
    }
    return usdg::Type::Float;
  case 'do':
    if (len == 7) {
      return GetVectorType<double>(type[6]);
    }
    return usdg::Type::Double;
  }

  return usdg::Type::Undefined;
}

static inline usdg::Type ToType(const std::string& typeName) {
  return ToType(typeName.c_str(), typeName.length());
}

static usdg::Attribute ToAttribute(const char* attribute) {
  const uint8_t attributeId =
    *reinterpret_cast<const uint8_t*>(attribute);

  switch (attributeId) {
  case 'p':
    return Attribute::Position;
  case 'n':
    return Attribute::Normal;
  case 'u':
    return Attribute::Uv;
  case 'c':
    return Attribute::Color;
  case 'b':
    return attribute[5] == 'W'
      ? Attribute::BlendWeight : Attribute::BlendIndices;
  case 'i':
    return Attribute::Index;
  }
  return Attribute::Undefined;
}

static const char* ToAttributeName(usdg::Attribute attribute) {
  return kAttributeNames[static_cast<uint8_t>(attribute)];
}

static const char* ToAttributeName(const std::string& attribute) {
  const uint8_t attributeId =
    *reinterpret_cast<const uint8_t*>(attribute.data());

  switch (attributeId) {
  case 'p':
    return ToAttributeName(Attribute::Position);
  case 'n':
    return ToAttributeName(Attribute::Normal);
  case 'u':
    return ToAttributeName(Attribute::Uv);
  case 'c':
    return ToAttributeName(Attribute::Color);
  case 'b':
    return attribute[5] == 'W'
      ? ToAttributeName(Attribute::BlendWeight)
      : ToAttributeName(Attribute::BlendIndices);
  case 'i':
    return ToAttributeName(Attribute::Index);
  }
  return ToAttributeName(Attribute::Undefined);
}

#ifdef WITH_VULKAN

namespace vk {

  static inline VkFormat ToFormat(const char* type, size_t len) {
    static VkFormat usdgTypeToVk[] = {
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8_UINT, VK_FORMAT_R8_UINT, VK_FORMAT_R16_SINT,
        VK_FORMAT_R16_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_UINT,
        VK_FORMAT_R64_SINT, VK_FORMAT_R64_UINT, VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R16_SFLOAT, VK_FORMAT_R64_SFLOAT,
        VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R64G64_SFLOAT,
        VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_UINT,
        VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT,
        VK_FORMAT_R64G64B64_SFLOAT,
        VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R64G64B64A64_SFLOAT
    };

    return usdgTypeToVk[static_cast<uint32_t>(ToType(type, len))]
  }

} // namespace vk

#endif

struct GpuDataElement {
  usdg::Attribute attribute;
  usdg::Type type;
  uint8_t offset;
  uint8_t channels;
};

class GpuDataDecl {
  std::string m_usdgPath;
  std::vector<GpuDataElement> m_elements;
  size_t m_size;
public:
  GpuDataDecl() = delete;

  explicit GpuDataDecl(const char* name) {
    m_usdgPath = kUsdg + ":" + name;
  }

  explicit GpuDataDecl(const std::string& name) {
    m_usdgPath = kUsdg + ":" + name;
  }

  void addElement(usdg::Attribute attribute, usdg::Type type,
      size_t offset, size_t channels = 1) {
    m_elements.push_back({ attribute, type,
        static_cast<uint8_t>(offset), static_cast<uint8_t>(channels) });
    m_size += GetPackedSize(type, channels);
  }

  void deserialize(const pxr::SdfPrimSpecHandle& obj) {
    pxr::VtDictionary usdgMeta = obj->GetCustomData();
    auto it = usdgMeta.find(m_usdgPath);

    if (it != usdgMeta.end()) {
      deserialize(it->second.Get<pxr::VtDictionary>());
    }
  }

  void deserialize(const pxr::UsdObject& obj) {
    auto usdgMeta = obj.GetCustomData();
    if (auto gpuDataDecl = usdgMeta.GetValueAtPath(m_usdgPath)) {
      auto gpuDataDeclDict = gpuDataDecl->Get<pxr::VtDictionary>();
      deserialize(gpuDataDeclDict);
    }
  }

  void deserialize(const pxr::VtDictionary& gpuDataDeclDict) {
    m_elements.resize(gpuDataDeclDict.size());
    auto element = m_elements.begin();

    m_size = 0;
    for (auto& semanticVt : gpuDataDeclDict) {
      auto elementDict = semanticVt.second.Get<pxr::VtDictionary>();

      element->attribute = ToAttribute(semanticVt.first.c_str());
      element->type = ToType(elementDict["type"].Get<std::string>());
      element->offset = elementDict["offset"].Get<uint8_t>();
      element->channels = elementDict["channels"].Get<uint8_t>();

      m_size += GetPackedSize(element->type, element->channels);

      ++element;
    }

    std::sort(m_elements.begin(), m_elements.end(),
        [](const usdg::GpuDataElement& a, const usdg::GpuDataElement& b) {
          return a.offset < b.offset;
        }
    );
  }

  void serialize(const pxr::SdfPrimSpecHandle& obj) {
#define SET_META(obj, field, value) \
  SetCustomMetadata(obj, pxr::TfToken(m_usdgPath + ":" +\
      ToAttributeName(element.attribute) + ":" + #field), value)

    for (const auto& element : m_elements) {
      SET_META(obj, type, ToTypeName(element.type));
      SET_META(obj, offset, element.offset);
      SET_META(obj, channels, element.channels);
    }

#undef SET_META
  }

  size_t size() const {
    return m_size;
  }

  const std::vector<GpuDataElement>& elements() const {
    return m_elements;
  }
};

class AABB {
  pxr::VtVec3fArray m_value = { {0, 0, 0}, {0, 0, 0} };
public:
  void deserialize(const pxr::UsdObject& obj) {
    GetCustomMetadata(obj, kAabb, m_value);
  }

  void serialize(const pxr::UsdObject& obj) {
    SetCustomMetadata(obj, kAabb, m_value);
  }

  pxr::VtVec3fArray& value() {
    return m_value;
  }
};

typedef void* PackageHandle;

static constexpr uint32_t kNoAssetIdx = ~0;

struct AssetDesc {
  enum class Type : uint8_t {
    UNKNOWN,
    IMAGE_1D,
    IMAGE_2D,
    IMAGE_3D,
    IMAGE_CUBE,
    BUFFER,
    RAW_FILE,
  };

  uint16_t unused;

  Type type;
  uint8_t format;

  union {
    uint32_t size;
    struct {
      uint16_t width;
      uint16_t height;
    };
  };
  uint16_t depth;

  uint16_t numMips;
  uint16_t numTailMips;
  uint16_t arraySize;

  uint16_t baseBlobIdx;
  uint16_t tailBlobIdx;
};

static_assert(sizeof(AssetDesc) == 20, "Asset description structure size overrun!");

struct BlobDesc {
  uint64_t offset : 40;
  uint64_t compression : 4;
  uint64_t volume : 4;
  uint64_t flags : 8;

  uint32_t size;
  uint32_t crc32;
};

static_assert(sizeof(BlobDesc) == 16, "Blob description structure size overrun!");

#ifdef USDG_IMPLEMENTATION
#define USDG_API ARCH_EXPORT
#else
#define USDG_API ARCH_IMPORT
#endif

USDG_API bool OpenPackage(const char* path, PackageHandle* handle);
USDG_API void ClosePackage(PackageHandle handle);

USDG_API const char* GetVolumeFilename(PackageHandle handle, uint32_t volume);
USDG_API uint32_t GetAssetsCount(PackageHandle handle);

USDG_API uint32_t FindAsset(PackageHandle handle, const char* filename);
USDG_API const AssetDesc* GetAssetDesc(PackageHandle handle, uint32_t assetIdx);
USDG_API const BlobDesc* GetDataBlobDesc(PackageHandle handle, uint32_t blobIdx);
USDG_API size_t ReadDataBlob(PackageHandle handle, uint32_t idx, void* out, size_t outSize);

} // namespace usdg

#undef USGD_IS_BIG_ENDIAN
