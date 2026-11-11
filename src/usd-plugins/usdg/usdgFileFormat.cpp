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
#include "pxr/pxr.h"
#include "usdgFileFormat.h"
#include "usdgResolver.h"

#include "pxr/usd/sdf/layer.h"

#include "pxr/usd/ar/packageUtils.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverScopedCache.h"

#include "pxr/base/trace/trace.h"

#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"

dxvk::Logger dxvk::Logger::s_instance("usdg.log", LogLevel::None);

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdUsdgFileFormatTokens, USD_USDG_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdUsdgFileFormat, SdfFileFormat);
}

UsdUsdgFileFormat::UsdUsdgFileFormat()
    : SdfFileFormat(UsdUsdgFileFormatTokens->Id,
                    UsdUsdgFileFormatTokens->Version,
                    UsdUsdgFileFormatTokens->Target,
                    UsdUsdgFileFormatTokens->Id)
{
}

UsdUsdgFileFormat::~UsdUsdgFileFormat()
{
}

bool 
UsdUsdgFileFormat::IsPackage() const
{
    return true;
}

namespace
{

std::string _GetMainUsdFile(const std::string& packagePath)
{
    auto package = Usd_UsdgResolverCache::GetInstance()
        .FindOrOpenPackageFile(packagePath).second;

    if (!package) {
        return "";
    }

    for (auto name : { "main.usd", "main.usda", "main.usdc" }) {
        if (package->findAsset(name) != dxvk::AssetPackage::kNoAssetIdx) {
            return name;
        }
    }

    return "";
}

} // end anonymous namespace

std::string 
UsdUsdgFileFormat::GetPackageRootLayerPath(
    const std::string& resolvedPath) const
{
    TRACE_FUNCTION();
    return _GetMainUsdFile(resolvedPath);
}

SdfAbstractDataRefPtr
UsdUsdgFileFormat::InitData(const FileFormatArguments& args) const
{
    return SdfFileFormat::InitData(args);
}

bool
UsdUsdgFileFormat::CanRead(const std::string& filePath) const
{
    TRACE_FUNCTION();

    const std::string firstFile = _GetMainUsdFile(filePath);
    if (firstFile.empty()) {
        return false;
    }

    const SdfFileFormatConstPtr packagedFileFormat = 
        SdfFileFormat::FindByExtension(firstFile);
    if (!packagedFileFormat) {
        return false;
    }

    const std::string packageRelativePath = 
        ArJoinPackageRelativePath(filePath, firstFile);
    return packagedFileFormat->CanRead(packageRelativePath);
}

bool
UsdUsdgFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    TRACE_FUNCTION();

    return _ReadHelper</* Detached = */ false>(
        layer, resolvedPath, metadataOnly);
}

#if PXR_VERSION >= 2211
bool
UsdUsdgFileFormat::_ReadDetached(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    TRACE_FUNCTION();

    return _ReadHelper</* Detached = */ true>(
        layer, resolvedPath, metadataOnly);
}
#endif

template <bool Detached>
bool
UsdUsdgFileFormat::_ReadHelper(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    // Use a scoped cache here so we only open the .usdg asset once.
    //
    // If the call to Read below calls ArResolver::OpenAsset, it will
    // ultimately ask Usd_UsdgResolver to open the .usdg asset. The
    // scoped cache will ensure that will pick up the asset opened
    // in _GetMainUsdFile instead of asking the resolver to
    // open the asset again.
    ArResolverScopedCache scopedCache;

    const std::string mainUsdFile = _GetMainUsdFile(resolvedPath);
    if (mainUsdFile.empty()) {
        return false;
    }

    const SdfFileFormatConstPtr packagedFileFormat = 
        SdfFileFormat::FindByExtension(mainUsdFile);
    if (!packagedFileFormat) {
        return false;
    }

    const std::string packageRelativePath = 
        ArJoinPackageRelativePath(resolvedPath, mainUsdFile);
    return
#if PXR_VERSION >= 2211
        Detached ?
            packagedFileFormat->ReadDetached(
                layer, packageRelativePath, metadataOnly) :
#endif
        packagedFileFormat->Read(
            layer, packageRelativePath, metadataOnly);
}

bool
UsdUsdgFileFormat::WriteToFile(
    const SdfLayer& layer,
    const std::string& filePath,
    const std::string& comment,
    const FileFormatArguments& args) const
{
    TF_CODING_ERROR("Writing usdg layers is not allowed via this API.");
    return false;
}

#if PXR_VERSION > 2211
#define USDA_FILE_FORMAT_TOKES SdfUsdaFileFormatTokens
#else
#define USDA_FILE_FORMAT_TOKES UsdUsdaFileFormatTokens
#endif

bool 
UsdUsdgFileFormat::ReadFromString(
    SdfLayer* layer,
    const std::string& str) const
{
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKES->Id)->
        ReadFromString(layer, str);
}

bool 
UsdUsdgFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKES->Id)->
        WriteToString(layer, str, comment);
}

bool
UsdUsdgFileFormat::WriteToStream(
    const SdfSpecHandle &spec,
    std::ostream& out,
    size_t indent) const
{
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKES->Id)->
        WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE

#ifdef USDG_IMPLEMENTATION
namespace usdg {

template<typename T, typename H>
static inline T& castFromApi(H h) { return *static_cast<T*>(h); }
template<typename H, typename T>
static inline H castToApi(T& obj) { return static_cast<H>(&obj); }
template<typename H, typename T>
static inline H castToApi(T* obj) { return static_cast<H>(obj); }

USDG_API
bool
OpenPackage(
    const char* path,
    PackageHandle* handle)
{
    auto* package = new dxvk::AssetPackage(path);

    if (package->initialize()) {
        *handle = castToApi<PackageHandle>(package);
        return true;
    }

    delete package;

    return false;
}

USDG_API
void
ClosePackage(PackageHandle handle)
{
    auto& package = castFromApi<dxvk::AssetPackage>(handle);
    package.closeAllFiles();
    delete &package;
}

USDG_API
const char*
GetVolumeFilename(
    PackageHandle handle,
    uint32_t volume)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        getVolumeFilename(volume).c_str();
}

USDG_API
uint32_t
GetAssetsCount(PackageHandle handle)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        getAssetCount();
}

USDG_API
uint32_t
FindAsset(
    PackageHandle handle,
    const char* filename)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        findAsset(filename);
}

USDG_API
const AssetDesc*
GetAssetDesc(
    PackageHandle handle,
    uint32_t assetIdx)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        getAssetDesc(assetIdx);
}

USDG_API
const BlobDesc*
GetDataBlobDesc(
    PackageHandle handle,
    uint32_t blobIdx)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        getDataBlobDesc(blobIdx);
}

USDG_API
size_t
ReadDataBlob(
    PackageHandle handle,
    uint32_t idx,
    void* out,
    size_t outSize)
{
    return castFromApi<dxvk::AssetPackage>(handle).
        readDataBlob(idx, out, outSize);
}

}
#endif