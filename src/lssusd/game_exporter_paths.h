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

#include <string>

namespace lss {

namespace commonDirName {
  static const std::string thumbDir("./thumbs/");
  static const std::string texDir("./textures/");
  static const std::string meshDir("./meshes/");
  static const std::string skeletonDir("./skeletons/");
  static const std::string lightDir("./lights/");
  static const std::string matDir("./materials/");
}
  
namespace ext {
  static const std::string usd(".usd");
  static const std::string usda(".usda");
  static const std::string usdc(".usdc");
  static const std::string png(".png");
  static const std::string dds(".dds");
}

enum class UsdVariant : uint8_t {
  USD               = 0,
  USDA              = 1,
  USDC              = 2,
  invalidUsdVariant = 0xff
};

static const struct { 
  UsdVariant variant;
  const std::string& str;
} usdExts[] = {
  { UsdVariant::USD, ext::usd },
  { UsdVariant::USDA, ext::usda },
  { UsdVariant::USDC, ext::usdc }
};

namespace prefix {
  static const std::string mesh("mesh_");
  static const std::string skeleton("skel_");
  static const std::string light("light_");
  static const std::string mat("mat_");
}

}