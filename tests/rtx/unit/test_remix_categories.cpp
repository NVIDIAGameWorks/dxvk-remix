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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

#include <windows.h>

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/base/plug/registry.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include "../../../src/lssusd/usd_include_end.h"

#include "../../../src/lssusd/remix_category_names.h"

#ifndef BUILD_SOURCE_ROOT
#define BUILD_SOURCE_ROOT "./"
#endif

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Expected the D3D9 and RemixCategories plugin DLL paths.\n";
    return -1;
  }

  const std::filesystem::path goldenSchema = BUILD_SOURCE_ROOT "src/usd-plugins/RemixCategories/resources/generatedSchema.usda";
  const std::filesystem::path generatedSchema = std::filesystem::absolute("remix_categories_generatedSchema.usda");

  HMODULE d3d9 = LoadLibraryA(argv[1]);
  if (d3d9 == nullptr) {
    std::cerr << "Unable to load D3D9 from " << argv[1] << ".\n";
    return -1;
  }

  using WriteSchema = bool (*)(const char*);
  WriteSchema writeSchema = reinterpret_cast<WriteSchema>(GetProcAddress(d3d9, "writeRemixCategoriesSchemaUsda"));
  if (writeSchema == nullptr || !writeSchema(generatedSchema.string().c_str())) {
    std::cerr << "Failed to generate category schema from " << argv[1] << ".\n";
    return -1;
  }

  std::ifstream expectedFile(goldenSchema);
  std::ifstream generatedFile(generatedSchema);
  if (!expectedFile || !generatedFile) {
    std::cerr << "Unable to open category schemas.\nExpected: " << goldenSchema
              << "\nGenerated: " << generatedSchema << "\n";
    return -1;
  }

  const std::string expectedSchema(std::istreambuf_iterator<char>(expectedFile), {});
  const std::string generatedSchemaText(std::istreambuf_iterator<char>(generatedFile), {});
  if (generatedSchemaText != expectedSchema) {
    std::cerr << "Generated category schema differs.\nExpected: " << goldenSchema
              << "\nGenerated: " << generatedSchema
              << "\nTo update the checked-in schema, copy the generated file:\n"
              << "Copy-Item -LiteralPath \"" << generatedSchema.string()
              << "\" -Destination \"" << goldenSchema.string() << "\" -Force\n";
    return -1;
  }

  const std::filesystem::path pluginDir = std::filesystem::path(argv[2]).parent_path() / "resources";
  pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginDir.string());

  const pxr::UsdPrimDefinition* primDef = pxr::UsdSchemaRegistry::GetInstance()
    .FindAppliedAPIPrimDefinition(pxr::TfToken("RemixInstanceCategoryAPI"));
  if (primDef == nullptr) {
    std::cerr << "RemixInstanceCategoryAPI was not registered from " << pluginDir << ".\n";
    return -1;
  }

  std::set<std::string> expected;
  for (const dxvk::RemixCategoryEntry& entry : dxvk::kRemixCategoryEntries) {
    expected.emplace(entry.attr);

    const pxr::SdfAttributeSpecHandle attribute = primDef->GetSchemaAttributeSpec(pxr::TfToken(entry.attr));
    if (attribute == nullptr || attribute->GetDocumentation().empty()) {
      std::cerr << "Missing schema documentation: " << entry.attr << "\n";
      return -1;
    }
  }

  std::set<std::string> actual;
  for (const pxr::TfToken& property : primDef->GetPropertyNames()) {
    actual.emplace(property.GetString());
  }

  if (actual != expected) {
    for (const std::string& property : expected) {
      if (actual.find(property) == actual.end()) {
        std::cerr << "Missing schema property: " << property << "\n";
      }
    }
    for (const std::string& property : actual) {
      if (expected.find(property) == expected.end()) {
        std::cerr << "Unexpected schema property: " << property << "\n";
      }
    }
    return -1;
  }

  return 0;
}
