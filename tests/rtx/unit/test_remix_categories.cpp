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
#include <iostream>
#include <set>
#include <string>

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/base/plug/registry.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include "../../../src/lssusd/usd_include_end.h"

#include "../../../src/lssusd/remix_category_names.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Expected the RemixCategories plugin DLL path.\n";
    return -1;
  }

  const std::filesystem::path pluginDir = std::filesystem::path(argv[1]).parent_path() / "resources";
  pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginDir.string());

  const pxr::UsdPrimDefinition* primDef = pxr::UsdSchemaRegistry::GetInstance()
    .FindAppliedAPIPrimDefinition(pxr::TfToken("RemixInstanceCategoryAPI"));
  if (primDef == nullptr) {
    std::cerr << "RemixInstanceCategoryAPI was not registered from " << pluginDir << ".\n";
    return -1;
  }

  std::set<std::string> expected;
  for (const char* property : dxvk::kRemixCategoryNames) {
    expected.emplace(property);
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
