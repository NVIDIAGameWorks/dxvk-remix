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

#include <cstdint>
#include <cstring>
#include <vector>

#define REMIX_WINAPI_NO_INCLUDE
#define REMIX_WINAPI_NO_LIBRARY_LOADER

#include "../../test_utils.h"
#include "../../../src/util/util_remix_api_version.h"

namespace dxvk {

  Logger Logger::s_instance("test_remix_api_version.log");

  namespace {

    constexpr uint8_t kCanary = 0xCD;
    constexpr size_t kCanaryBytes = 64;

    void require(bool condition, const char* pMessage) {
      if (!condition) {
        throw DxvkError(pMessage);
      }
    }

    remixapi_ErrorCode initialize(uint64_t version, void* pOut) {
      remixapi_InitializeLibraryInfo info = {};
      info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
      info.version = version;
      return remixapi_InitializeLibrary(&info, static_cast<remixapi_Interface*>(pOut));
    }

    // A client allocates only the interface it knows, so the bytes behind it must stay untouched.
    // The client's part starts zeroed, so a null first or last pointer means that the runtime did not write it.
    void testClientInterfaceSize(uint64_t version, size_t clientBytes, const char* pMessage) {
      std::vector<uint8_t> buffer(sizeof(remixapi_Interface) + kCanaryBytes, kCanary);
      std::memset(buffer.data(), 0, clientBytes);
      require(initialize(version, buffer.data()) == REMIXAPI_ERROR_CODE_SUCCESS, pMessage);

      for (size_t i = clientBytes; i < buffer.size(); ++i) {
        require(buffer[i] == kCanary, pMessage);
      }

      void* pFirst = nullptr;
      void* pLast = nullptr;
      std::memcpy(&pFirst, buffer.data(), sizeof(void*));
      std::memcpy(&pLast, buffer.data() + clientBytes - sizeof(void*), sizeof(void*));
      require(pFirst != nullptr && pLast != nullptr, pMessage);
    }

    void testRejectedVersion(uint64_t version, const char* pMessage) {
      std::vector<uint8_t> buffer(sizeof(remixapi_Interface) + kCanaryBytes, kCanary);
      require(initialize(version, buffer.data()) == REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION, pMessage);

      for (const uint8_t byte : buffer) {
        require(byte == kCanary, pMessage);
      }
    }

    constexpr uint64_t kRuntimeVersion = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
    constexpr size_t kCurrentInterfaceBytes = sizeof(remixapi_Interface);
    constexpr size_t kOldInterfaceBytes = offsetof(remixapi_Interface, SetCameraMediumMaterial);
    constexpr size_t kNoWaterInterfaceBytes = offsetof(remixapi_Interface, SetWaterSim);

    // The functions around the slot that 0.6.4 moved must be in their 0.6.3 order.
    void testInterface063() {
      remixapi_Interface interf = {};
      remixapi_version::remixapi_Interface_063 interf063 = {};
      require(initialize(kRuntimeVersion, &interf) == REMIXAPI_ERROR_CODE_SUCCESS, "current client");
      require(initialize(REMIXAPI_VERSION_MAKE(0, 6, 3), &interf063) == REMIXAPI_ERROR_CODE_SUCCESS, "0.6.3 client");

      require(interf063.SetupCamera == interf.SetupCamera, "0.6.3 SetupCamera");
      require(interf063.SetCameraMediumMaterial == interf.SetCameraMediumMaterial, "0.6.3 SetCameraMediumMaterial");
      require(interf063.DrawInstance == interf.DrawInstance, "0.6.3 DrawInstance");
      require(interf063.Present == interf.Present, "0.6.3 Present");
    }

    void testCompatibilityRule() {
      using remixapi_version::isClientVersionCompatible;

      for (uint64_t clientPatch = 0; clientPatch < 12; ++clientPatch) {
        for (uint64_t runtimePatch = 0; runtimePatch < 12; ++runtimePatch) {
          require(isClientVersionCompatible(REMIXAPI_VERSION_MAKE(0, 6, clientPatch), REMIXAPI_VERSION_MAKE(0, 6, runtimePatch)),
                  "0.6.x clients work with any 0.6.x runtime");
          require(!isClientVersionCompatible(REMIXAPI_VERSION_MAKE(0, 5, clientPatch), REMIXAPI_VERSION_MAKE(0, 6, runtimePatch)),
                  "0.5.x clients are rejected by 0.6.x runtimes");
          require(!isClientVersionCompatible(REMIXAPI_VERSION_MAKE(0, 7, clientPatch), REMIXAPI_VERSION_MAKE(0, 6, runtimePatch)),
                  "0.7.x clients are rejected by 0.6.x runtimes");
          require(isClientVersionCompatible(REMIXAPI_VERSION_MAKE(1, 1, clientPatch), REMIXAPI_VERSION_MAKE(1, 2, runtimePatch)),
                  "1.1.x clients work with 1.2.x runtimes");
          require(!isClientVersionCompatible(REMIXAPI_VERSION_MAKE(1, 3, clientPatch), REMIXAPI_VERSION_MAKE(1, 2, runtimePatch)),
                  "1.3.x clients are rejected by 1.2.x runtimes");
          require(!isClientVersionCompatible(REMIXAPI_VERSION_MAKE(0, 6, clientPatch), REMIXAPI_VERSION_MAKE(1, 6, runtimePatch)),
                  "0.x clients are rejected by 1.x runtimes");
        }
      }
    }

    void testClientInterfaceSizes() {
      using remixapi_version::clientInterfaceSize;

      for (uint64_t patch = 0; patch < 12; ++patch) {
        const uint64_t version = REMIXAPI_VERSION_MAKE(0, 6, patch);
        size_t expected = kCurrentInterfaceBytes;
        if (patch < 3) {
          expected = kOldInterfaceBytes;
        } else if (patch == 3) {
          expected = sizeof(remixapi_version::remixapi_Interface_063);
        } else if (patch < 11) {
          expected = kNoWaterInterfaceBytes;
        }
        require(clientInterfaceSize(version) == expected, "clientInterfaceSize matches the client's patch");
      }
    }

  }

  void testVersions() {
    testCompatibilityRule();
    testClientInterfaceSizes();

    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 0), kOldInterfaceBytes, "0.6.0 client keeps its canary");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 4), kNoWaterInterfaceBytes, "0.6.4 client");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 5), kNoWaterInterfaceBytes, "0.6.5 client");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 6), kNoWaterInterfaceBytes, "0.6.6 client");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 8), kNoWaterInterfaceBytes, "0.6.8 client");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 10), kNoWaterInterfaceBytes, "0.6.10 client");
    testClientInterfaceSize(kRuntimeVersion, kCurrentInterfaceBytes, "client at the runtime's version");
    testClientInterfaceSize(kRuntimeVersion + 1, kCurrentInterfaceBytes, "client with a newer patch");
    testClientInterfaceSize(REMIXAPI_VERSION_MAKE(0, 6, 3), sizeof(remixapi_version::remixapi_Interface_063), "0.6.3 client keeps its canary");
    testInterface063();

    testRejectedVersion(REMIXAPI_VERSION_MAKE(0, REMIXAPI_VERSION_MINOR + 1, 0), "next minor is rejected");
    testRejectedVersion(REMIXAPI_VERSION_MAKE(0, 5, 0), "older minor is rejected");
  }

}

int main() {
  try {
    dxvk::testVersions();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED with exception: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All Remix API version tests passed." << std::endl;
  return 0;
}
