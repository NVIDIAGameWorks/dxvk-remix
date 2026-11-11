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
#ifndef PXR_USD_USD_USDG_FILE_FORMAT_H
#define PXR_USD_USD_USDG_FILE_FORMAT_H

#pragma warning(disable : 4244 4305)

#include "pxr/pxr.h"
#include "pxr/usd/usd/api.h"
#if PXR_VERSION > 2211
#include "pxr/usd/sdf/usdaFileFormat.h"
#else
#include "pxr/usd/usd/usdaFileFormat.h"
#endif
#include "pxr/base/tf/staticTokens.h"

#include <string>

#define USD_API_LOCAL ARCH_HIDDEN

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(UsdUsdgFileFormat);

#define USD_USDG_FILE_FORMAT_TOKENS  \
    ((Id,      "usdg"))              \
    ((Version, "1.0"))               \
    ((Target,  "usd"))

TF_DECLARE_PUBLIC_TOKENS(
    UsdUsdgFileFormatTokens, USD_API_LOCAL, USD_USDG_FILE_FORMAT_TOKENS);

/// \class UsdUsdgFileFormat
///
/// File format for package .usdg files.
class UsdUsdgFileFormat : public SdfFileFormat
{
public:
    using SdfFileFormat::FileFormatArguments;

    USD_API_LOCAL
    virtual bool IsPackage() const override;

    USD_API_LOCAL
    virtual std::string GetPackageRootLayerPath(
        const std::string& resolvedPath) const override;

    USD_API_LOCAL
    virtual SdfAbstractDataRefPtr
    InitData(const FileFormatArguments& args) const override;

    USD_API_LOCAL
    virtual bool CanRead(const std::string &file) const override;

    USD_API_LOCAL
    virtual bool Read(
        SdfLayer* layer,
        const std::string& resolvedPath,
        bool metadataOnly) const override;

    USD_API_LOCAL
    virtual bool WriteToFile(
        const SdfLayer& layer,
        const std::string& filePath,
        const std::string& comment = std::string(),
        const FileFormatArguments& args = FileFormatArguments()) const override;

    USD_API_LOCAL
    virtual bool ReadFromString(
        SdfLayer* layer,
        const std::string& str) const override;

    USD_API_LOCAL
    virtual bool WriteToString(
        const SdfLayer& layer,
        std::string* str,
        const std::string& comment = std::string()) const override;

    USD_API_LOCAL
    virtual bool WriteToStream(
        const SdfSpecHandle &spec,
        std::ostream& out,
        size_t indent) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;
#if PXR_VERSION >= 2211
    bool _ReadDetached(
        SdfLayer* layer,
        const std::string& resolvedPath,
        bool metadataOnly) const override;
#endif
private:
    USD_API_LOCAL
    UsdUsdgFileFormat();
    virtual ~UsdUsdgFileFormat();

    template <bool Detached>
    bool _ReadHelper(
        SdfLayer* layer,
        const std::string& resolvedPath,
        bool metadataOnly) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_USDG_FILE_FORMAT_H
