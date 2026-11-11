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
#pragma warning(disable : 4244 4305)

#include "pxr/pxr.h"

#include "usdgResolver.h"

#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/definePackageResolver.h"
#if PXR_VERSION >= 2211
#include "pxr/usd/ar/resolvedPath.h"
#endif
#include "pxr/usd/ar/resolver.h"
#include "pxr/base/arch/fileSystem.h"

#include <tbb/concurrent_hash_map.h>
#include <mutex>
#include <filesystem>
#include <memory>

#define USE_PACKAGE_MMAP 1

PXR_NAMESPACE_OPEN_SCOPE

static std::string MakePathPreferred(const std::string& _path)
{
    std::filesystem::path path(_path);
    path.make_preferred();

    return path.string();
}

Usd_UsdgResolverCache&
Usd_UsdgResolverCache::GetInstance()
{
    static Usd_UsdgResolverCache cache;
    return cache;
}

Usd_UsdgResolverCache::Usd_UsdgResolverCache()
{
}

struct Usd_UsdgResolverCache::_Cache
{
    using _Map = tbb::concurrent_hash_map<std::string,
        AssetAndPackageFile>;
    _Map _pathToEntryMap;

#if USE_PACKAGE_MMAP
    using _Map2 = tbb::concurrent_hash_map<std::string,
        std::shared_ptr<const char>>;
    _Map2 _packageToMmap;
#endif
};

void 
Usd_UsdgResolverCache::BeginCacheScope(
    VtValue* cacheScopeData)
{
    _caches.BeginCacheScope(cacheScopeData);
}

void
Usd_UsdgResolverCache::EndCacheScope(
    VtValue* cacheScopeData)
{
    _caches.EndCacheScope(cacheScopeData);
}

Usd_UsdgResolverCache::_CachePtr 
Usd_UsdgResolverCache::_GetCurrentCache()
{
    return _caches.GetCurrentCache();
}

Usd_UsdgResolverCache::AssetAndPackageFile
Usd_UsdgResolverCache::_OpenPackageFile(const std::string& path)
{
    AssetAndPackageFile result;
#if PXR_VERSION >= 2211
    result.first = ArGetResolver().OpenAsset(ArResolvedPath(path));
#else
    result.first = ArGetResolver().OpenAsset(path);
#endif
    if (result.first) {
        auto package = std::make_shared<dxvk::AssetPackage>(path);
        if (package->initialize()) {
            result.second = std::move(package);
        }
    }
    return result;
}

Usd_UsdgResolverCache::AssetAndPackageFile
Usd_UsdgResolverCache::FindOrOpenPackageFile(const std::string& _packagePath)
{
    auto packagePath(MakePathPreferred(_packagePath));

    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
        _Cache::_Map::accessor accessor;
        if (currentCache->_pathToEntryMap.insert(
                accessor, std::make_pair(packagePath, AssetAndPackageFile()))) {
            accessor->second = _OpenPackageFile(packagePath);

#if USE_PACKAGE_MMAP
            if (currentCache->_packageToMmap.insert(
                std::make_pair(accessor->second.second->getBaseFilename(),
                    accessor->second.first->GetBuffer()))) {
#ifdef _DEBUG
                TF_STATUS("Package %s mapped.\n",
                    accessor->second.second->getBaseFilename().c_str());
#endif
            }
#endif
        }
        return accessor->second;
    }

    return _OpenPackageFile(packagePath);
}

std::shared_ptr<const char>
Usd_UsdgResolverCache::FindOrMapPackageFile(
    const std::string& _packagePath)
{
#if USE_PACKAGE_MMAP
    auto packagePath(MakePathPreferred(_packagePath));

    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
        _Cache::_Map2::accessor accessor;
        if (currentCache->_packageToMmap.insert(accessor,
            std::make_pair(packagePath, std::shared_ptr<const char>()))) {
            TF_WARN("Cached mmap not found for package %s\n",
                packagePath.c_str());
            return {};
        }
        return accessor->second;
    }
    TF_WARN("No USDG cache in current scope!\n");
    return {};
#else
    TF_CODING_ERROR("Package mmap is not enabled!");
#endif
}

Usd_UsdgResolverCache::AssetAndPackageFile
Usd_UsdgResolverCache::FindPackageFile(
    const std::string& _packagePath)
{
    auto packagePath(MakePathPreferred(_packagePath));

    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
        _Cache::_Map::accessor accessor;
        if (currentCache->_pathToEntryMap.find(accessor, packagePath)) {
            return accessor->second;
        }
    }

    return { nullptr, nullptr };
}

// ------------------------------------------------------------

AR_DEFINE_PACKAGE_RESOLVER(Usd_UsdgResolver, ArPackageResolver);

Usd_UsdgResolver::Usd_UsdgResolver()
{
}

void 
Usd_UsdgResolver::BeginCacheScope(
    VtValue* cacheScopeData)
{
    Usd_UsdgResolverCache::GetInstance().BeginCacheScope(cacheScopeData);
}

void
Usd_UsdgResolver::EndCacheScope(
    VtValue* cacheScopeData)
{
    Usd_UsdgResolverCache::GetInstance().EndCacheScope(cacheScopeData);
}

std::string 
Usd_UsdgResolver::Resolve(
    const std::string& packagePath,
    const std::string& packagedPath)
{
    std::shared_ptr<ArAsset> asset;
    std::shared_ptr<dxvk::AssetPackage> packageFile;
    std::tie(asset, packageFile) = Usd_UsdgResolverCache::GetInstance()
        .FindOrOpenPackageFile(packagePath);

    if (!packageFile) {
        return std::string();
    }
    return packageFile->findAsset(packagedPath) !=
        dxvk::AssetPackage::kNoAssetIdx ? packagedPath : std::string();
}

namespace
{

class _Asset
    : public ArAsset
{
private:
    std::shared_ptr<ArAsset> _sourceAsset;
    std::shared_ptr<dxvk::AssetPackage> _package;

    const uint32_t _assetIdx;
    mutable std::shared_ptr<char> _data;

    void LazyReadWhole() const
    {
        if (_data) {
            return;
        }

        auto assetDesc = _package->getAssetDesc(_assetIdx);

        _data.reset(new char[assetDesc->size]);

        {
            // TODO: this lock should be per-package
            static std::recursive_mutex _mutex;
            std::lock_guard< std::recursive_mutex> _(_mutex);

            _package->readDataBlob(assetDesc->baseBlobIdx,
                _data.get(), assetDesc->size);
            _package->closeFile(0);
        }
    }

public:
    explicit _Asset(std::shared_ptr<ArAsset>&& sourceAsset,
                    std::shared_ptr<dxvk::AssetPackage>&& package,
                    uint32_t assetIdx)
        : _sourceAsset(std::move(sourceAsset))
        , _package(std::move(package))
        , _assetIdx(assetIdx)
    {
    }

    size_t GetSize() const override
    {
        return _package->getAssetDesc(_assetIdx)->size;
    }

    std::shared_ptr<const char> GetBuffer() const override
    {
#if USE_PACKAGE_MMAP
        auto assetDesc = _package->getAssetDesc(_assetIdx);
        auto blobDesc = _package->getDataBlobDesc(assetDesc->baseBlobIdx);

        auto packageMmap = Usd_UsdgResolverCache::GetInstance()
            .FindOrMapPackageFile(_package->getBaseFilename());

        if (!packageMmap) {
            packageMmap = _sourceAsset->GetBuffer();
        }

        struct _Deleter
        {
            void operator()(const char* b)
            {
                _source = nullptr;
            }
            std::shared_ptr<const char> _source;
        } deleter;

        deleter._source = packageMmap;

        return std::shared_ptr<const char>(packageMmap.get() +
            blobDesc->offset, deleter);
#else
        LazyReadWhole();

        struct _Deleter
        {
            void operator()(const char* b)
            {
                _data = nullptr;
            }
            std::shared_ptr<const char> _data;
        } deleter;

        deleter._data = _data;

        return std::shared_ptr<const char>(_data.get(), deleter);
#endif
    }

    size_t Read(void* buffer, size_t count, size_t offset) const override
    {
        if (ARCH_UNLIKELY(offset + count > GetSize())) {
            return 0;
        }

        LazyReadWhole();

        memcpy(buffer, _data.get() + offset, count);
        return count;
    }

    std::pair<FILE*, size_t> GetFileUnsafe() const override
    {
        std::pair<FILE*, size_t> result = _sourceAsset->GetFileUnsafe();
        if (result.first) {
            result.second += _package->getDataBlobDesc(
                _package->getAssetDesc(_assetIdx)->baseBlobIdx)->offset;
        }
        return result;
    }
};

} // end anonymous namespace

std::shared_ptr<ArAsset> 
Usd_UsdgResolver::OpenAsset(
    const std::string& packagePath,
    const std::string& packagedPath)
{
    std::shared_ptr<ArAsset> asset;
    std::shared_ptr<dxvk::AssetPackage> packageFile;
    std::tie(asset, packageFile) = Usd_UsdgResolverCache::GetInstance()
        .FindOrOpenPackageFile(packagePath);

    if (!packageFile) {
        return nullptr;
    }

    auto iter = packageFile->findAsset(packagedPath);
    if (iter == dxvk::AssetPackage::kNoAssetIdx) {
        return nullptr;
    }

    auto info = packageFile->getAssetDesc(iter);

    if (info->type != dxvk::AssetPackage::AssetDesc::Type::RAW_FILE) {
        TF_RUNTIME_ERROR(
            "Cannot open %s in %s: asset is not a raw file",
            packagedPath.c_str(), packagePath.c_str());
        return nullptr;
    }

    auto dataInfo = packageFile->getDataBlobDesc(info->baseBlobIdx);

    if (dataInfo->compression != 0) {
        TF_RUNTIME_ERROR(
            "Cannot open %s in %s: compressed files are not supported",
            packagedPath.c_str(), packagePath.c_str());
        return nullptr;
    }

    return std::shared_ptr<ArAsset>(
        new _Asset(
            std::move(asset), std::move(packageFile), iter)
        );
}

PXR_NAMESPACE_CLOSE_SCOPE

ARCH_EXPORT
std::shared_ptr<dxvk::AssetPackage>
UsdgGetPackage(const char* packagePath)
{
    auto& cache = pxr::Usd_UsdgResolverCache::GetInstance();

    auto package = cache.FindPackageFile(packagePath).second;

    // TODO: FindPackageFile() may not not work with USDA since cache
    // needs to be open for the current thread but USDA are loaded whole
    // and the assets are closed.
    if (!package) {
        package = cache.FindOrOpenPackageFile(packagePath).second;
    }

    return package;
}
