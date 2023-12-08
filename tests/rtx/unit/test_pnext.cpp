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

#define RTX_REMIX_PNEXT_CHECK_STRUCTS

#include "../../../src/dxvk/rtx_render/rtx_remix_pnext.h"

// Note: including C++ wrapper to check consistency with C API
#include <remix/remix.h>

#include "../../test_utils.h"

#ifdef _MSC_VER
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#define ERROR_INTRO __FUNCTION__ "(" STRINGIFY(__LINE__) "): "
#else
#define ERROR_INTRO ""
#endif

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_pnext.log");
}

namespace pnext_test_app {
  using namespace pnext::detail;

  void test_find() {
    auto ext = remixapi_MaterialInfoOpaqueEXT {};
    {
      ext.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
      ext.pNext = nullptr;
    }
    auto info = remixapi_MaterialInfo {};
    {
      info.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
      info.pNext = &ext;
    }

    if (auto f = pnext::find< remixapi_MaterialInfo >(&info)) {
      if (f != &info) {
        throw dxvk::DxvkError { ERROR_INTRO
          "Result of pnext::find< remixapi_MaterialInfoOpaqueEXT >( remixapi_MaterialInfo{..} )"
          "must match the address of \'info\' variable" };
      }
    } else {
      throw dxvk::DxvkError { ERROR_INTRO
        "pnext::find< remixapi_MaterialInfo >( remixapi_MaterialInfo{..} ) failed" };
    }

    if (auto f = pnext::find< remixapi_MaterialInfoOpaqueEXT >(&info)) {
      if (f != &ext) {
        throw dxvk::DxvkError { ERROR_INTRO
          "Result of pnext::find< remixapi_MaterialInfoOpaqueEXT >( remixapi_MaterialInfo{..} )"
          "must match the address of \'ext\' variable" };
      }
    } else {
      throw dxvk::DxvkError { ERROR_INTRO
        "pnext::find< remixapi_MaterialInfoOpaqueEXT >( remixapi_MaterialInfo{..} ) must return non-null" };
    }

    // must be un-compilable and output a short compilation error
    {
      // pnext::find< remixapi_LightInfo >(&info);
      // pnext::find< remixapi_LightInfoSphereEXT >(&info);
    }
  }

  void test_const() {
    auto ext = remixapi_MaterialInfoOpaqueEXT {};
    {
      ext.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
      ext.pNext = nullptr;
    }
    auto info = remixapi_MaterialInfo {};
    {
      info.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
      info.pNext = &ext;
    }
    const auto constInfo = remixapi_MaterialInfo { info };

    {
      auto pNext = getPNext(&info);
      static_assert(!std::is_const_v< std::remove_pointer_t< decltype(pNext) > >);

      auto constPNext = getPNext(&constInfo);
      static_assert(std::is_const_v< std::remove_pointer_t< decltype(constPNext) > >);
    }
    {
      auto f = pnext::find< remixapi_MaterialInfoOpaqueEXT >(&info);
      if (f != &ext) {
        throw dxvk::DxvkError{ ERROR_INTRO
          "Result of pnext::find< remixapi_MaterialInfoOpaqueEXT >( remixapi_MaterialInfo{..} )"
          "must match the address of \'ext\' variable" };
      }

      static_assert(!std::is_const_v< std::remove_pointer_t< decltype(f) > >);
    }
    {
      auto cf = pnext::find< remixapi_MaterialInfoOpaqueEXT >(&constInfo);
      if (cf != &ext) {
        throw dxvk::DxvkError { ERROR_INTRO
          "Result of pnext::find< remixapi_MaterialInfoOpaqueEXT >( remixapi_MaterialInfo{..} )"
          "must match the address of \'ext\' variable" };
      }

      static_assert(std::is_const_v< std::remove_pointer_t< decltype(cf) > >);
    }
  }

  void test_getPNext() {
    auto ext = remixapi_LightInfoDistantEXT {};
    {
      ext.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
      ext.pNext = nullptr;
    }
    auto info = remixapi_LightInfo {};
    {
      info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
      info.pNext = &ext;
    }

    if (info.pNext != &ext) {
      throw dxvk::DxvkError { ERROR_INTRO "Unexpected pNext mismatch: info.pNext != &ext" };
    }
    if (getPNext(&info) != info.pNext || getPNext(&ext) != ext.pNext) {
      throw dxvk::DxvkError { ERROR_INTRO "getPNext test fail: result is not equal to .pNext" };
    }
  }

  void test_memberDetection() {
    struct BadType_0 {
      int   sType_none;
      void* pNext;
    };
    struct BadType_1 {
      int   sType;
      void* pNext_none;
    };
    struct BadType_2 {
      int   sType;
    };
    struct BadType_3 {
      void* pNext;
    };
    struct BadType_4 {
      std::underlying_type_t< remixapi_StructType > sType;
      void* pNext;
    };
    struct BadType_5 {
      remixapi_StructType sType;
      uint64_t pNext;
    };
    static_assert(!helper::HasSTypePNext< BadType_0 >);
    static_assert(!helper::HasSTypePNext< BadType_1 >);
    static_assert(!helper::HasSTypePNext< BadType_2 >);
    static_assert(!helper::HasSTypePNext< BadType_3 >);
    static_assert(!helper::HasSTypePNext< BadType_4 >);
    static_assert(!helper::HasSTypePNext< BadType_5 >);

    struct GoodType {
      remixapi_StructType sType;
      void* pNext;
    };
    static_assert(helper::HasSTypePNext< GoodType >);
  }
}

int main() {
  try {
    pnext_test_app::test_find();
    pnext_test_app::test_const();
    pnext_test_app::test_getPNext();
    pnext_test_app::test_memberDetection();
  }
  catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return -1;
  }

  return 0;
}