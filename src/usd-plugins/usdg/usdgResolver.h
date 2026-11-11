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
#ifndef PXR_USD_USD_USDG_RESOLVER_H
#define PXR_USD_USD_USDG_RESOLVER_H

#pragma warning(disable : 4244 4305)

#include "pxr/pxr.h"
#include "pxr/usd/ar/packageResolver.h"
#include "pxr/usd/ar/threadLocalScopedCache.h"

#include "rtx_asset_package.h"

PXR_NAMESPACE_OPEN_SCOPE

class ArAsset;

/// \class Usd_UsdgResolver
///
/// Package resolver responsible for resolving assets in
/// .usdg files.
class Usd_UsdgResolver
    : public ArPackageResolver
{
public:
    Usd_UsdgResolver();

    virtual std::string Resolve(
        const std::string& packagePath,
        const std::string& packagedPath) override;

    virtual std::shared_ptr<ArAsset> OpenAsset(
        const std::string& packagePath,
        const std::string& packagedPath) override;

    virtual void BeginCacheScope(
        VtValue* cacheScopeData) override;

    virtual void EndCacheScope(
        VtValue* cacheScopeData) override;
};

/// \class Usd_UsdgResolverCache
///
/// Singleton thread-local scoped cache used by Usd_UsdgResolver. This
/// allows other clients besides Usd_UsdgResolver to take advantage of
/// caching of package files while a resolver scoped cache is active.
class Usd_UsdgResolverCache
{
public:
    static Usd_UsdgResolverCache& GetInstance();

    Usd_UsdgResolverCache(const Usd_UsdgResolverCache&) = delete;
    Usd_UsdgResolverCache& operator=(const Usd_UsdgResolverCache&) = delete;

    using AssetAndPackageFile = std::pair<std::shared_ptr<ArAsset>,
        std::shared_ptr<dxvk::AssetPackage>>;

    /// Returns the ArAsset and AssetPackage for the given package path.
    /// If a cache scope is active in the current thread, the returned
    /// values will be cached and returned on subsequent calls to this
    /// function for the same packagePath.
    AssetAndPackageFile FindOrOpenPackageFile(
        const std::string& packagePath);

    /// Open a cache scope in the current thread. While a cache scope 
    /// is opened, the results of FindOrOpenPackageFile will be cached and 
    /// reused.
    void BeginCacheScope(
        VtValue* cacheScopeData);

    /// Close cache scope in the current thread. Once all cache scopes
    /// in the current thread are closed, cached package files will be
    /// dropped.
    void EndCacheScope(
        VtValue* cacheScopeData);

    std::shared_ptr<const char> FindOrMapPackageFile(
        const std::string& packagePath);

    AssetAndPackageFile FindPackageFile(
        const std::string& _packagePath);

private:
    Usd_UsdgResolverCache();

    struct _Cache;
    using _ThreadLocalCaches = ArThreadLocalScopedCache<_Cache>;
    using _CachePtr = _ThreadLocalCaches::CachePtr;
    _CachePtr _GetCurrentCache();

    AssetAndPackageFile _OpenPackageFile(const std::string& packagePath);

    _ThreadLocalCaches _caches;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_USDG_RESOLVER_H
