/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <strsafe.h>

#include "detours_common.h"
#include "d3d9_util.h"
#include "remix_state.h"
#include "config/global_options.h"
#include "util_detourtools.h"
#include "di_hook.h"
#include "window.h"

using namespace bridge_util;
using namespace DI;

// Defining all required GUIDs locally so we do not need to link against the SDK
#define _DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  EXTERN_C const GUID DECLSPEC_SELECTANY name \
    = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

_DEFINE_GUID(_GUID_SysMouse,         0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
_DEFINE_GUID(_IID_IDirectInputA,     0x89521360,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
_DEFINE_GUID(_IID_IDirectInput2A,    0x5944E662,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
_DEFINE_GUID(_IID_IDirectInput7A,    0x9A4CB684,0x236D,0x11D3,0x8E,0x9D,0x00,0xC0,0x4F,0x68,0x44,0xAE);
_DEFINE_GUID(_IID_IDirectInput8A,    0xBF798030,0x483A,0x4DA2,0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00);

// DirectInput API method decls and vtbl indexes are shared across all DirectInput versions
// and so can be reused for every DirectInput API version hook.
#define DECL_DI_METHOD(name, vtblIdx, sig) \
  static const uint32_t VtblIdx_##name = vtblIdx; \
  typedef HRESULT (STDMETHODCALLTYPE Type_##name)sig
#define GET_DI_METHOD_PTR(x, vtbl) Orig##x = (Type_##x*) vtbl[VtblIdx_##x]

DECL_DI_METHOD(SetProperty, 6, (void FAR*, REFGUID, LPCDIPROPHEADER));
DECL_DI_METHOD(Acquire, 7, (void FAR*));
DECL_DI_METHOD(Unacquire, 8, (void FAR*));
DECL_DI_METHOD(GetDeviceState, 9, (void FAR*, DWORD, LPVOID));
DECL_DI_METHOD(GetDeviceData, 10, (void FAR*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD));
DECL_DI_METHOD(SetDataFormat, 11, (void FAR*, LPCDIDATAFORMAT));
DECL_DI_METHOD(SetCooperativeLevel, 13, (void FAR*, HWND, DWORD));

static bool gClientUsesDirectInput = false;

template<typename T>
using DeviceArray = std::array<T,(size_t)kNumDeviceTypes>;

// DirectInput translation and forwarding helper
class DirectInputForwarder {
public:
  static void init() {
    s_forwardPolicies[Mouse] = ClientOptions::getForwardDirectInputMousePolicy();
    s_forwardPolicies[Keyboard] = ClientOptions::getForwardDirectInputKeyboardPolicy();
  }
private:
  struct WndMsg {
    HWND hWnd;
    uint32_t msg;
    uint32_t wParam;
    uint32_t lParam;
  };

  // Last known key state
  static inline BYTE s_KS[256] = { 0 };

  // Last known mouse state
  static inline BYTE s_mouseButtons[8] = { 0 };
  static inline LONG s_mouseX = 0;
  static inline LONG s_mouseY = 0;

  static inline DeviceArray<bool> s_bIsExclusive{false,false};

  // Last sent messages
  static inline WndMsg s_mouseMove = { 0 };
  static inline WndMsg s_mouseLButton = { 0 };
  static inline WndMsg s_mouseRButton = { 0 };
  static inline WndMsg s_mouseWheel = { 0 };

  static inline HWND s_hwnd = nullptr;
  static inline LONG s_windowWidth = 3840;
  static inline LONG s_windowHeight = 2160;

  static inline DeviceArray<ForwardPolicy> s_forwardPolicies;


  static void forwardMessage(const WndMsg& wm) {
    const DeviceType devType =
      (wm.msg >= WM_MOUSEFIRST && wm.msg <= WM_MOUSELAST) ? DeviceType::Mouse : DeviceType::Keyboard;
    // Bail when input is not exclusive OR policy says no
    if(!s_bIsExclusive[devType] || !evaluatePolicy(devType)) {
      return;
    }
    WndProc::invokeRemixWndProc(wm.msg, wm.wParam, wm.lParam);
  }

  static bool evaluatePolicy(const DeviceType devType) {
    const auto policy = s_forwardPolicies[devType];
    if(policy == ForwardPolicy::Never) {
      return false;
    }
    if(policy == ForwardPolicy::Always) {
      return true;
    }
    // Match activity status
    return (policy == ForwardPolicy::RemixUIActive) == RemixState::isUIActive();
  }

  static void updateWindowSize() {
    RECT rect;
    ::GetWindowRect(s_hwnd, &rect);

    // Only accept reasonable window extents.
    // NOTE: A game overlay may setup a zero-sized DirectInput window and make
    // cursor position clamping code produce wrong results.
    if (rect.right - rect.left > 16) {
      s_windowWidth = rect.right - rect.left;
    }

    if (rect.bottom - rect.top > 16) {
      s_windowHeight = rect.bottom - rect.top;
    }
  }

public:
  static void setKeyboardExclusive(bool exclusive) {
    s_bIsExclusive[Keyboard] = exclusive;
  }

  static void setMouseExclusive(bool exclusive) {
    s_bIsExclusive[Mouse] = exclusive;
  }
  
  static void setWindow(HWND hwnd) {
    s_hwnd = hwnd;
    updateWindowSize();
  }

  static HWND getWindow() {
    return s_hwnd;
  }

  static void updateKeyState(LPBYTE KS) {
    bool windowUpdated = false;

    for (uint32_t vsc = 0; vsc < 256; vsc++) {
      if (s_KS[vsc] != KS[vsc]) {
        UINT vk = 0;

        switch (vsc) {
        case 210:
          // No translation
          vk = VK_INSERT;
          break;
        default:
          vk = MapVirtualKeyExA(vsc, MAPVK_VSC_TO_VK, NULL);
        }

        if (0 == vk) {
          Logger::warn(format_string("DirectInput: unable to translate VSC: %d", vsc));
          continue;
        }

        WndMsg wm { s_hwnd };
        wm.msg = (KS[vsc] & 0x80) ? WM_KEYDOWN : WM_KEYUP;
        wm.wParam = vk;
        forwardMessage(wm);

#ifdef _DEBUG
        Logger::info(format_string("key: %d (%d)", vk, KS[vsc] >> 7));
#endif

        // Emit WM_CHAR on keydown for characters
        if (wm.msg == WM_KEYDOWN) {
          WORD ascii[2] = { 0 };
          if (1 == ToAscii(vk, vsc, KS, ascii, 0)) {
            // Only process keys that have 1:1 character representation
            wm.msg = WM_CHAR;
            wm.wParam = ascii[0];
            forwardMessage(wm);

#ifdef _DEBUG
            if (wm.wParam < 255) {
              Logger::info(format_string("char: %c", wm.wParam));
            } else {
              Logger::info(format_string("unichar: 0x%x", wm.wParam));
            }
#endif
          }
        }

        s_KS[vsc] = KS[vsc];

        if (!windowUpdated) {
          // Update window size once in a while
          updateWindowSize();
          windowUpdated = true;
        }
      }
    }
  }

  template<typename T>
  static void updateMouseState(const T* state, bool isAbsoluteAxis) {
    if (isAbsoluteAxis) {
      s_mouseX = state->lX;
      s_mouseY = state->lY;
    } else {
      s_mouseX += state->lX;
      s_mouseY += state->lY;
    }

    if (s_mouseX < 0) s_mouseX = 0;
    if (s_mouseY < 0) s_mouseY = 0;
    if (s_mouseX > s_windowWidth) s_mouseX = s_windowWidth;
    if (s_mouseY > s_windowHeight) s_mouseY = s_windowHeight;

    WndMsg wm { s_hwnd };
    wm.msg = WM_MOUSEMOVE;
    wm.lParam = s_mouseX | (s_mouseY << 16);
    wm.wParam = (state->rgbButtons[0] & 0x80) ? MK_LBUTTON : 0;
    wm.wParam += (state->rgbButtons[1] & 0x80) ? MK_RBUTTON : 0;
    wm.wParam += ((s_KS[DIK_LCONTROL] & 0x80) || (s_KS[DIK_RCONTROL] & 0x80)) ? MK_CONTROL : 0;
    wm.wParam += ((s_KS[DIK_LSHIFT] & 0x80) || (s_KS[DIK_RSHIFT] & 0x80)) ? MK_SHIFT : 0;

    bool changed = false;

    if (0 != memcmp(&wm, &s_mouseMove, sizeof(wm))) {
      forwardMessage(wm);
      s_mouseMove = wm;
      changed = true;
    }

    if (s_mouseButtons[0] != state->rgbButtons[0]) {
      wm.msg = (state->rgbButtons[0] & 0x80) ? WM_LBUTTONDOWN : WM_LBUTTONUP;

      s_mouseButtons[0] = state->rgbButtons[0];

      if (0 != memcmp(&wm, &s_mouseLButton, sizeof(wm))) {
        forwardMessage(wm);
        s_mouseLButton = wm;
        changed = true;
      }
    }

    if (s_mouseButtons[1] != state->rgbButtons[1]) {
      wm.msg = (state->rgbButtons[1] & 0x80) ? WM_RBUTTONDOWN : WM_RBUTTONUP;

      s_mouseButtons[1] = state->rgbButtons[1];

      if (0 != memcmp(&wm, &s_mouseRButton, sizeof(wm))) {
        forwardMessage(wm);
        s_mouseRButton = wm;
        changed = true;
      }
    }

    if (GET_WHEEL_DELTA_WPARAM(s_mouseWheel.wParam) != state->lZ) {
      // Preserve button codes
      const WORD buttons = LOWORD(wm.wParam);

      wm.msg = WM_MOUSEWHEEL;
      wm.wParam = MAKELONG(buttons, state->lZ);

      forwardMessage(wm);
      s_mouseWheel = wm;
      changed = true;
    }

#ifdef _DEBUG
    if (changed) {
      Logger::info(format_string("mouse state updated: %d,%d (%d %d) (%d %d %d)",
                                 s_mouseX, s_mouseY, s_mouseButtons[0] >> 7,
                                 s_mouseButtons[1] >> 7, state->lX, state->lY, state->lZ));
    }
#endif
  }
};


// DirectInput hook base class to be shared across all API versions.
// Holds original function pointers and implements the hooked versions.
template<int Version>
class DirectInputHookBase {
protected:
  inline static Type_SetCooperativeLevel* OrigSetCooperativeLevel = nullptr;
  inline static Type_Acquire* OrigAcquire = nullptr;
  inline static Type_Unacquire* OrigUnacquire = nullptr;
  inline static Type_GetDeviceState* OrigGetDeviceState = nullptr;
  inline static Type_GetDeviceData* OrigGetDeviceData = nullptr;
  inline static Type_SetProperty* OrigSetProperty = nullptr;

  inline static void* MouseDevice = nullptr;
  inline static void* KeyboardDevice = nullptr;
  inline static DWORD MouseAxisMode = DIPROPAXISMODE_REL;
  inline static bool MouseDeviceStateUsed = false;
  inline static bool KeyboardDeviceStateUsed = false;

  inline static constexpr DWORD kDefaultCooperativeLevel = DISCL_NONEXCLUSIVE | DISCL_FOREGROUND;
  inline static DeviceArray<DWORD> ogCooperativeLevel{kDefaultCooperativeLevel,
                                                      kDefaultCooperativeLevel};

  inline static std::unordered_map<void*, bool> ExclusiveMode;

  static HRESULT STDMETHODCALLTYPE HookedSetProperty(void FAR* thiz,
                                                     REFGUID rguidProp,
                                                     LPCDIPROPHEADER pdiph) {
    LogStaticFunctionCall();

    const HRESULT hr = OrigSetProperty(thiz, rguidProp, pdiph);

    if (hr == DI_OK && MouseDevice == thiz) {
      if (&rguidProp == &DIPROP_AXISMODE) {
        MouseAxisMode = reinterpret_cast<const DIPROPDWORD*>(pdiph)->dwData;
        if (MouseAxisMode == DIPROPAXISMODE_REL) {
          Logger::info("DirectInput mouse axis mode set to Relative");
        } else {
          Logger::info("DirectInput mouse axis mode set to Absolute");
        }
      }
    }

    return hr;
  }

  inline static constexpr auto kMouseDevType = DI8DEVCLASS_POINTER;
  inline static constexpr auto kKeyboardDevType = DI8DEVCLASS_KEYBOARD;
  static HRESULT STDMETHODCALLTYPE HookedAcquire(void FAR* thiz) {
    LogStaticFunctionCall();

    const HRESULT hr = OrigAcquire(thiz);

    gClientUsesDirectInput = true;

    // TODO: support acquiring new mouse/reaqiuring (?)
    if (KeyboardDevice != thiz || MouseDevice != thiz) {
      IDirectInputDevice* di = (IDirectInputDevice*) thiz;

      DIDEVCAPS caps { sizeof(DIDEVCAPS) };
      di->GetCapabilities(&caps);

      // LSByte of dwDevType indicates device type
      if (KeyboardDevice != thiz && (caps.dwDevType & 0xf) == kKeyboardDevType) {
        ONCE(Logger::debug("DirectInput keyboard acquired"));
        Logger::trace("DirectInput keyboard acquired");
        KeyboardDevice = thiz;

        if (ExclusiveMode.count(thiz) > 0) {
          DirectInputForwarder::setKeyboardExclusive(ExclusiveMode[thiz]);
        }
      } else if (MouseDevice != thiz && (caps.dwDevType & 0xf) == kMouseDevType) {
        ONCE(Logger::debug("DirectInput mouse acquired"));
        Logger::trace("DirectInput mouse acquired");
        MouseDevice = thiz;

        if (ExclusiveMode.count(thiz) > 0) {
          DirectInputForwarder::setMouseExclusive(ExclusiveMode[thiz]);
        }
      }
    }

    return hr;
  }

  static HRESULT STDMETHODCALLTYPE HookedUnacquire(void FAR* thiz) {
    LogStaticFunctionCall();

    const HRESULT hr = OrigUnacquire(thiz);

    if (KeyboardDevice && KeyboardDevice == thiz) {
      ONCE(Logger::debug("DirectInput keyboard unacquired"));
      Logger::trace("DirectInput keyboard unacquired");
      KeyboardDevice = nullptr;
    } else if (MouseDevice && MouseDevice == thiz) {
      ONCE(Logger::debug("DirectInput mouse unacquired"));
      Logger::trace("DirectInput mouse unacquired");
      MouseDevice = nullptr;
    }

    return hr;
  }

  static HRESULT STDMETHODCALLTYPE HookedSetCooperativeLevel(void FAR* thiz,
                                                             HWND hwnd,
                                                             DWORD dwFlags) {
    LogStaticFunctionCall();

#ifdef _DEBUG
    Logger::info(format_string("DirectInput SetCooperativeLevel(%p, %d)",
                               hwnd, dwFlags));
#endif

    if (thiz == MouseDevice) {
      ogCooperativeLevel[Mouse] = dwFlags;
    } else if (thiz == KeyboardDevice) {
      ogCooperativeLevel[Keyboard] = dwFlags;
    }

    if (ClientOptions::getDisableExclusiveInput()) {
      dwFlags = DISCL_NONEXCLUSIVE | DISCL_FOREGROUND;
    }

    ExclusiveMode[thiz] = (dwFlags & DISCL_EXCLUSIVE) != 0;

    return OrigSetCooperativeLevel(thiz, hwnd, dwFlags);
  }

  static HRESULT STDMETHODCALLTYPE HookedGetDeviceState(void FAR* thiz,
                                                        DWORD size,
                                                        LPVOID data) {
    // Could be called way too frequently.
    // LogStaticFunctionCall();

    const HRESULT hr = OrigGetDeviceState(thiz, size, data);

    if (hr != DI_OK) {
      return hr;
    }

    switch (size) {
    case sizeof(DIMOUSESTATE):
      DirectInputForwarder::updateMouseState(static_cast<DIMOUSESTATE*>(data),
                                          MouseAxisMode == DIPROPAXISMODE_ABS);
      MouseDeviceStateUsed = true;
#ifdef _DEBUG
      ONCE(Logger::info("DirectInput mouse state captured."));
#endif
      break;
    case sizeof(DIMOUSESTATE2):
      DirectInputForwarder::updateMouseState(static_cast<DIMOUSESTATE2*>(data),
                                          MouseAxisMode == DIPROPAXISMODE_ABS);
      MouseDeviceStateUsed = true;
#ifdef _DEBUG
      ONCE(Logger::info("DirectInput mouse(2) state captured."));
#endif
      break;
    case 256:
      DirectInputForwarder::updateKeyState(static_cast<LPBYTE>(data));
      KeyboardDeviceStateUsed = true;
#ifdef _DEBUG
      ONCE(Logger::info("DirectInput keyboard state captured."));
#endif
      break;
    }

    if (RemixState::isUIActive())  {
      // Remix UI is active - wipe input state
      memset(data, 0, size);
    }
      
    return hr;
  }

  static HRESULT STDMETHODCALLTYPE HookedGetDeviceData(void FAR* thiz,
                                                       DWORD cbObjectData,
                                                       LPDIDEVICEOBJECTDATA rgdod,
                                                       LPDWORD pdwInOut,
                                                       DWORD dwFlags) {
    LogStaticFunctionCall();

    const HRESULT hr = OrigGetDeviceData(thiz, cbObjectData, rgdod, pdwInOut, dwFlags);

    if (rgdod) {
      if(hr == DI_OK) {
        if (MouseDevice == thiz && MouseDeviceStateUsed == false) {
          for (uint32_t n = 0; n < *pdwInOut; n++) {
            DIMOUSESTATE mstate { 0 };

            if (rgdod[n].dwOfs == DIMOFS_X) {
              mstate.lX = (LONG) rgdod[n].dwData;
            } else if (rgdod[n].dwOfs == DIMOFS_Y) {
              mstate.lY = (LONG) rgdod[n].dwData;
            } else if (rgdod[n].dwOfs == DIMOFS_Z) {
              mstate.lZ = (LONG) rgdod[n].dwData;
            } else if (rgdod[n].dwOfs == DIMOFS_BUTTON0) {
              mstate.rgbButtons[0] = rgdod[n].dwData;
            } else if (rgdod[n].dwOfs == DIMOFS_BUTTON1) {
              mstate.rgbButtons[1] = rgdod[n].dwData;
            } else {
              continue;
            }

            DirectInputForwarder::updateMouseState(&mstate,
                                              MouseAxisMode == DIPROPAXISMODE_ABS);
          }
        } else if (KeyboardDevice == thiz && KeyboardDeviceStateUsed == false) {
          static BYTE data[256];
          for (uint32_t n = 0; n < *pdwInOut; n++) {
            data[rgdod[n].dwOfs] = rgdod[n].dwData;
          }
          DirectInputForwarder::updateKeyState(data);
        }
      }
      // Remix UI is active - wipe input state
      // Some games read this state even if hr != DI_OK
      // So we need to wipe either way
      if (RemixState::isUIActive()) {
        memset(rgdod, 0, *pdwInOut * cbObjectData);
        *pdwInOut = 0;
      }
    }

    return hr;
  }

  static bool attach(const void* dev) {
    void** vtbl = (void**) *((void**) dev);

    // Fetch interface function pointers from vtbl.
    GET_DI_METHOD_PTR(SetProperty, vtbl);
    GET_DI_METHOD_PTR(Acquire, vtbl);
    GET_DI_METHOD_PTR(Unacquire, vtbl);
    GET_DI_METHOD_PTR(GetDeviceState, vtbl);
    GET_DI_METHOD_PTR(GetDeviceData, vtbl);
    GET_DI_METHOD_PTR(SetCooperativeLevel, vtbl);

    LONG error = 0;

    API_ATTACH(SetProperty);
    API_ATTACH(Acquire);
    API_ATTACH(Unacquire);
    API_ATTACH(GetDeviceState);
    API_ATTACH(GetDeviceData);
    API_ATTACH(SetCooperativeLevel);

    return error == 0;
  }

  static void detach() {
    API_DETACH(SetProperty);
    API_DETACH(Acquire);
    API_DETACH(Unacquire);
    API_DETACH(GetDeviceState);
    API_DETACH(GetDeviceData);
    API_DETACH(SetCooperativeLevel);
  }

  static std::string getSystemLibraryPath(const char* name) {
    char szSystemLib[1024];
    GetSystemDirectoryA(szSystemLib, sizeof(szSystemLib));
    StringCchCatA(szSystemLib, sizeof(szSystemLib), "\\");
    StringCchCatA(szSystemLib, sizeof(szSystemLib), name);
    return szSystemLib;
  }
  
public:
  static void unsetCooperativeLevel() {
    const auto hwnd = DirectInputForwarder::getWindow();
    if(hwnd) {
      if (MouseDevice != nullptr) {
        OrigSetCooperativeLevel(MouseDevice, hwnd, kDefaultCooperativeLevel);
        ExclusiveMode[MouseDevice] = false;
      }
      if (KeyboardDevice != nullptr) {
        OrigSetCooperativeLevel(KeyboardDevice, hwnd, kDefaultCooperativeLevel);
        ExclusiveMode[KeyboardDevice] = false;
      }
    }
  }

  static void resetCooperativeLevel() {
    const auto hwnd = DirectInputForwarder::getWindow();
    if(hwnd) {
      if (MouseDevice != nullptr) {
        OrigSetCooperativeLevel(MouseDevice, hwnd, ogCooperativeLevel[Mouse]);
        ExclusiveMode[MouseDevice] = (ogCooperativeLevel[Mouse] & DISCL_EXCLUSIVE) != 0;
      }
      if (KeyboardDevice != nullptr) {
        OrigSetCooperativeLevel(KeyboardDevice, hwnd, ogCooperativeLevel[Keyboard]);
        ExclusiveMode[KeyboardDevice] = (ogCooperativeLevel[Keyboard] & DISCL_EXCLUSIVE) != 0;
      }
    }
  }
};

class DirectInput8Hook: public DirectInputHookBase<8> {
  static HRESULT WINAPI HookedDirectInput8Create(HINSTANCE hinst,
                                                 DWORD dwVersion,
                                                 REFIID riidltf,
                                                 LPVOID* ppvOut,
                                                 LPUNKNOWN punkOuter) {
    LogStaticFunctionCall();
    gClientUsesDirectInput = true;
    return OrigDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
  }

  API_HOOK_DECL(DirectInput8Create);

public:
  static bool attach() {
    // Attempt to retrieve the original injected APIs
    auto OrigLoadLibraryA = DetourRetrieveOriginal(LoadLibraryA);
    auto OrigGetProcAddress = DetourRetrieveOriginal(GetProcAddress);

    HMODULE hdi8 = OrigLoadLibraryA(getSystemLibraryPath("dinput8").c_str());

    OrigDirectInput8Create = reinterpret_cast<decltype(OrigDirectInput8Create)>(
      OrigGetProcAddress(hdi8, "DirectInput8Create"));

    if (nullptr == OrigDirectInput8Create) {
      Logger::warn("Unable to get DirectInput8Create proc address. "
                   "DirectInput8 hook will not be available.");
      return false;
    }

    if (DetourIsInjected(*OrigDirectInput8Create)) {
      Logger::warn("Injected DirectInput8Create proc detected!");
      OrigDirectInput8Create = DetourRetrieveOriginal(*OrigDirectInput8Create);
    }

    IDirectInput8A* di8 = nullptr;
    IDirectInputDevice8A* di8dev = nullptr;

    if (DI_OK != OrigDirectInput8Create(::GetModuleHandleA(NULL),
                                        0x0800, _IID_IDirectInput8A,
                                        (LPVOID*) &di8, NULL)) {
      Logger::warn("Unable to create DirectInput8. "
                   "DirectInput8 hook will not be available.");
      return false;
    }

    if (DI_OK != di8->CreateDevice(_GUID_SysMouse, &di8dev, NULL)) {
      Logger::warn("Unable to create DirectInput8 mouse device. "
                   "DirectInput8 hook will not be available.");
      di8->Release();
      return false;
    }

    bool res = false;
    if (DirectInputHookBase::attach(di8dev)) {
      LONG error = 0;
      API_ATTACH(DirectInput8Create);
      if (error) {
        Logger::warn(format_string("Unable to attach DirectInput8Create %d", error));
      }
      res = error == 0;
    } else {
      Logger::warn("Unable to attach DirectInput8 methods.");
    }

    di8dev->Release();
    di8->Release();

    if (res) {
      Logger::info("DirectInput8 hook attached.");
    }

    return res;
  }

  static void detach() {
    API_DETACH(DirectInput8Create);
    DirectInputHookBase::detach();

    Logger::info("DirectInput8 hook detached.");
  }
};

#if DIRECTINPUT_VERSION > 0x0700
extern HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion,
                                         LPDIRECTINPUTA* ppDI, LPUNKNOWN punkOuter);
extern HRESULT WINAPI DirectInputCreateW(HINSTANCE hinst, DWORD dwVersion,
                                         LPDIRECTINPUTW* ppDI, LPUNKNOWN punkOuter);
#endif

class DirectInput7Hook: public DirectInputHookBase<7> {
  static void VersionCheck(DWORD dwVersion) {
    if (dwVersion != 0x0700) {
      Logger::warn(format_string("Unsupported DirectInput version: %d.%d.",
                                 dwVersion >> 8, dwVersion & 0xff));
    }
  }

  static HRESULT WINAPI HookedDirectInputCreateA(HINSTANCE hinst,
                                                 DWORD dwVersion,
                                                 LPDIRECTINPUTA* lplpDirectInput,
                                                 LPUNKNOWN punkOuter) {
    LogStaticFunctionCall();
    VersionCheck(dwVersion);
    gClientUsesDirectInput = true;
    return OrigDirectInputCreateA(hinst, dwVersion, lplpDirectInput, punkOuter);
  }

  static HRESULT WINAPI HookedDirectInputCreateW(HINSTANCE hinst,
                                                 DWORD dwVersion,
                                                 LPDIRECTINPUTW* lplpDirectInput,
                                                 LPUNKNOWN punkOuter) {
    LogStaticFunctionCall();
    VersionCheck(dwVersion);
    gClientUsesDirectInput = true;
    return OrigDirectInputCreateW(hinst, dwVersion, lplpDirectInput, punkOuter);
  }

  API_HOOK_DECL(DirectInputCreateA);
  API_HOOK_DECL(DirectInputCreateW);

public:
  static bool attach() {
    // Attempt to retrieve the original injected APIs
    auto OrigLoadLibraryA = DetourRetrieveOriginal(LoadLibraryA);
    auto OrigGetProcAddress = DetourRetrieveOriginal(GetProcAddress);

    HMODULE hdi = OrigLoadLibraryA(getSystemLibraryPath("dinput").c_str());

    OrigDirectInputCreateA = reinterpret_cast<decltype(OrigDirectInputCreateA)>(
      OrigGetProcAddress(hdi, "DirectInputCreateA"));

    if (nullptr == OrigDirectInputCreateA) {
      Logger::warn("Unable to get DirectInputCreate proc address. "
                   "DirectInput hook will not be available.");
      return false;
    }

    if (DetourIsInjected(*OrigDirectInputCreateA)) {
      Logger::warn("Injected DirectInputCreate proc detected!");
      OrigDirectInputCreateA = DetourRetrieveOriginal(*OrigDirectInputCreateA);
    }

    IDirectInput7A* di7 = nullptr;
    IDirectInputDevice7A* di7dev = nullptr;

    if (DI_OK != OrigDirectInputCreateA(::GetModuleHandleA(NULL),
                                        0x0700, (LPDIRECTINPUTA*) &di7, NULL)) {
      Logger::warn("Unable to create DirectInput v7.0. "
                   "DirectInput hook will not be available.");
      return false;
    }

    if (DI_OK != di7->CreateDevice(_GUID_SysMouse, (LPDIRECTINPUTDEVICEA*) &di7dev, NULL)) {
      Logger::warn("Unable to create DirectInput mouse device. "
                   "DirectInput hook will not be available.");
      di7->Release();
      return false;
    }

    bool res = false;
    if (DirectInputHookBase::attach(di7dev)) {
      LONG error = 0;
      API_ATTACH(DirectInputCreateA);
      if (error) {
        Logger::warn(format_string("Unable to attach DirectInputCreateA: %d", error));
      }

      // Attach to unicode API just in case
      OrigDirectInputCreateW = reinterpret_cast<decltype(OrigDirectInputCreateW)>(
        OrigGetProcAddress(hdi, "DirectInputCreateW"));

      if (nullptr != OrigDirectInputCreateW) {
        API_ATTACH(DirectInputCreateW);
        if (error) {
          Logger::warn(format_string("Unable to attach DirectInputCreateW: %d", error));
        }
      }

      res = error == 0;
    } else {
      Logger::warn("Unable to attach DirectInput methods.");
    }

    di7dev->Release();
    di7->Release();

    if (res) {
      Logger::info("DirectInput hook attached.");
    }

    return res;
  }

  static void detach() {
    API_DETACH(DirectInputCreateA);
    API_DETACH(DirectInputCreateW);
    DirectInputHookBase::detach();

    Logger::info("DirectInput hook detached.");
  }
};

API_HOOK_DECL(GetCursorPos);
API_HOOK_DECL(SetCursorPos);
API_HOOK_DECL(GetAsyncKeyState);
API_HOOK_DECL(GetKeyState);
API_HOOK_DECL(GetKeyboardState);
API_HOOK_DECL(GetRawInputData);
API_HOOK_DECL(PeekMessageA);
API_HOOK_DECL(PeekMessageW);
API_HOOK_DECL(GetMessageA);
API_HOOK_DECL(GetMessageW);

static BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd,
                                      UINT wMsgFilterMin, UINT wMsgFilterMax,
                                      UINT wRemoveMsg) {
  LogStaticFunctionCall();

  BOOL result;
  do {
    result = OrigPeekMessageA(lpMsg, hWnd, wMsgFilterMin,
                              wMsgFilterMax, wRemoveMsg);

    if (result && lpMsg && (wRemoveMsg & PM_REMOVE) != 0) {
      // The message has been removed so we need to process it here.
      if (WndProc::invokeRemixWndProc(lpMsg->message, lpMsg->wParam, lpMsg->lParam)) {
        // Swallow the message
        continue;
      }
    }
    break;
  } while (true);

  return result;
}

static BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd,
                                      UINT wMsgFilterMin, UINT wMsgFilterMax,
                                      UINT wRemoveMsg) {
  LogStaticFunctionCall();

  BOOL result;
  do {
    result = OrigPeekMessageW(lpMsg, hWnd, wMsgFilterMin,
                              wMsgFilterMax, wRemoveMsg);

    if (result && lpMsg && (wRemoveMsg & PM_REMOVE) != 0) {
      // The message has been removed so we need to process it here.
      if (WndProc::invokeRemixWndProc(lpMsg->message, lpMsg->wParam, lpMsg->lParam)) {
        // Swallow the message
        continue;
      }
    }
    break;
  } while (true);

  return result;
}

static BOOL WINAPI HookedGetMessageA(LPMSG lpMsg, HWND hWnd,
                                     UINT wMsgFilterMin, UINT wMsgFilterMax) {
  LogStaticFunctionCall();

  BOOL result;
  do {
    result = OrigGetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (result && result != -1 && lpMsg) {
      if (WndProc::invokeRemixWndProc(lpMsg->message, lpMsg->wParam, lpMsg->lParam)) {
        // Swallow the message
        continue;
      }
    }
    break;
  } while (true);

  return result;
}

static BOOL WINAPI HookedGetMessageW(LPMSG lpMsg, HWND hWnd,
                                     UINT wMsgFilterMin, UINT wMsgFilterMax) {
  LogStaticFunctionCall();

  BOOL result;
  do {
    result = OrigGetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (result && result != -1 && lpMsg) {
      if (WndProc::invokeRemixWndProc(lpMsg->message, lpMsg->wParam, lpMsg->lParam)) {
        // Swallow the message
        continue;
      }
    }
    break;
  } while (true);

  return result;
}

namespace {
  using IdHook = int;
  std::unordered_map<IdHook, HHOOK> gWinHooks;

  HHOOK accessWinHook(IdHook idHook) {
    auto iter = gWinHooks.find(idHook);
    if (iter == gWinHooks.end()) {
      assert(0);
      return nullptr;
    }
    return iter->second;
  }
}

static LRESULT CALLBACK HookedCallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
  LogStaticFunctionCall();
  
  if (nCode >= 0) {
    if (RemixState::isUIActive()) {
      return 0;
    }
  }

  return CallNextHookEx(accessWinHook(WH_CALLWNDPROC), nCode, wParam, lParam);
}

static LRESULT CALLBACK HookedGetMsgProc(int nCode, WPARAM wParam, LPARAM lParam) {
  LogStaticFunctionCall();
  
  if (nCode >= 0) {
    if (RemixState::isUIActive()) {
      return 0;
    }
  }

  return CallNextHookEx(accessWinHook(WH_GETMESSAGE), nCode, wParam, lParam);
}

static LRESULT CALLBACK HookedKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  LogStaticFunctionCall();

  if (nCode >= 0) {
    if (RemixState::isUIActive()) {
      return 0;
    }
  }

  return CallNextHookEx(accessWinHook(WH_KEYBOARD), nCode, wParam, lParam);
}

static LRESULT CALLBACK HookedLowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  LogStaticFunctionCall();

  if (nCode >= 0) {
    if (RemixState::isUIActive()) {
      return 0;
    }
  }

  return CallNextHookEx(accessWinHook(WH_KEYBOARD_LL), nCode, wParam, lParam);
}

static BOOL WINAPI HookedGetCursorPos(LPPOINT lp) {
  LogStaticFunctionCall();

  static POINT lastKnownPos;

  // Return last know position when Remix UI is active
  if (RemixState::isUIActive()) {
    *lp = lastKnownPos;
    return TRUE;
  }

  if (OrigGetCursorPos(lp)) {
    lastKnownPos = *lp;
    return TRUE;
  }

  return FALSE;
}

static BOOL WINAPI HookedSetCursorPos(int X, int Y) {
  LogStaticFunctionCall();
  // Block if Remix UI is active
  if (RemixState::isUIActive()) {
    return TRUE;
  }
  return OrigSetCursorPos(X, Y);
}

static SHORT WINAPI HookedGetAsyncKeyState(int vk) {
  LogStaticFunctionCall();
  // Block if Remix UI is active
  if (RemixState::isUIActive()) {
    return 0;
  }
  return OrigGetAsyncKeyState(vk);
}

static SHORT WINAPI HookedGetKeyState(int vk) {
  LogStaticFunctionCall();
  // Block if Remix UI is active
  if (RemixState::isUIActive()) {
    return 0;
  }
  return OrigGetKeyState(vk);
}

static SHORT WINAPI HookedGetKeyboardState(PBYTE lpKeyState) {
  LogStaticFunctionCall();
  // Block if Remix UI is active
  if (RemixState::isUIActive()) {
    memset(lpKeyState, 0, 256);
    return TRUE;
  }
  return OrigGetKeyboardState(lpKeyState);
}

static UINT WINAPI HookedGetRawInputData(HRAWINPUT hRawInput,
                                         UINT uiCommand, LPVOID pData,
                                         PUINT pcbSize, UINT cbSizeHeader) {
  static RAWMOUSE lastKnownMouseState;
  static RAWKEYBOARD lastKnownKeyboardState;

  LogStaticFunctionCall();

  UINT res = OrigGetRawInputData(hRawInput, uiCommand,
                                 pData, pcbSize, cbSizeHeader);

  if (gClientUsesDirectInput) {
    // Must NOT mess with the Raw input when app actively uses DirectInput.
    return res;
  }

  if (nullptr != pData && pcbSize && res == *pcbSize) {
    // We have raw data to process
    RAWINPUT* raw = static_cast<RAWINPUT*>(pData);

    // Block if Remix UI is active
    if (RemixState::isUIActive()) {
      if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        raw->data.keyboard = lastKnownKeyboardState;
      } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        raw->data.mouse = lastKnownMouseState;
      }

      return res;
    }

    // Update last known states
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
      lastKnownKeyboardState = raw->data.keyboard;
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
      lastKnownMouseState = raw->data.mouse;
    }
  }
  return res;
}

static void InputWinHooksAttach() {
  if (!ClientOptions::getOverrideCustomWinHooks()) {
    return;
  }

  assert(gWinHooks.empty());

  auto attachWinHook = [](int idHook, HOOKPROC lpfn) {
    if (HHOOK h = SetWindowsHookEx(idHook, lpfn, nullptr, GetCurrentThreadId())) {
      return std::pair { idHook, h };
    }
    Logger::warn(format_string("SetWindowsHookEx failed with idHook=%d", idHook));
    return std::pair { idHook , static_cast<HHOOK>(nullptr) };
  };

  gWinHooks = {
    attachWinHook(WH_CALLWNDPROC, HookedCallWndProc),
    attachWinHook(WH_GETMESSAGE,  HookedGetMsgProc),
    attachWinHook(WH_KEYBOARD,    HookedKeyboardProc),
    attachWinHook(WH_KEYBOARD_LL, HookedLowLevelKeyboardProc),
  };
}

static void InputWinHooksDetach() {
  for (auto [id, hook] : gWinHooks) {
    UnhookWindowsHookEx(hook);
  }
  gWinHooks.clear();
}

static void AttachConventionalInput() {
  LONG error;

  OrigGetCursorPos = GetCursorPos;
  OrigSetCursorPos = SetCursorPos;
  OrigGetKeyState = GetKeyState;
  OrigGetAsyncKeyState = GetAsyncKeyState;
  OrigGetKeyboardState = GetKeyboardState;
  OrigGetRawInputData = GetRawInputData;

  API_ATTACH(GetCursorPos);
  API_ATTACH(SetCursorPos);
  API_ATTACH(GetKeyState);
  API_ATTACH(GetAsyncKeyState);
  API_ATTACH(GetKeyboardState);
  API_ATTACH(GetRawInputData);

  if (ClientOptions::getHookMessagePump()) {
    // Attach to message pump functions
    OrigPeekMessageA = PeekMessageA;
    OrigPeekMessageW = PeekMessageW;
    OrigGetMessageA = GetMessageA;
    OrigGetMessageW = GetMessageW;

    API_ATTACH(PeekMessageA);
    API_ATTACH(PeekMessageW);
    API_ATTACH(GetMessageA);
    API_ATTACH(GetMessageW);
  }

  InputWinHooksAttach();
}

static void DetachConventionalInput() {
  API_DETACH(GetCursorPos);
  API_DETACH(SetCursorPos);
  API_DETACH(GetKeyState);
  API_DETACH(GetAsyncKeyState);
  API_DETACH(GetKeyboardState);
  API_DETACH(GetRawInputData);

  if (ClientOptions::getHookMessagePump()) {
    API_DETACH(PeekMessageA);
    API_DETACH(PeekMessageW);
    API_DETACH(GetMessageA);
    API_DETACH(GetMessageW);
  }

  InputWinHooksDetach();
}

void InputWinHooksReattach() {
  InputWinHooksDetach();
  InputWinHooksAttach();
}

void DInputHookAttach() {
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  AttachConventionalInput();
  DirectInput8Hook::attach();
  DirectInput7Hook::attach();

  // TODO: add other DI versions if needed.
  // When adding a new version it is crucial to check the DI vtbl
  // beforehand because the methods may be shared across multiple
  // versions (in particular, on recent Windows versions) and we
  // may end up with numerous handler invocations.

  DetourTransactionCommit();
  DirectInputForwarder::init();
}

void DInputHookDetach() {
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  DetachConventionalInput();
  DirectInput8Hook::detach();
  DirectInput7Hook::detach();

  DetourTransactionCommit();
}

void DInputSetDefaultWindow(HWND hwnd) {
  // Note: some games may not call SetCooperativeLevel() so we have little
  // options to know about the window and actual cooperative level.
  // Assume exclusive input by default to force di messages forwarding.
  DirectInputForwarder::setWindow(hwnd);
  DirectInputForwarder::setKeyboardExclusive(true);
  DirectInputForwarder::setMouseExclusive(true);
}

namespace DI {

void unsetCooperativeLevel() {
  DirectInput7Hook::unsetCooperativeLevel();
  DirectInput8Hook::unsetCooperativeLevel();
}

void resetCooperativeLevel() {
  DirectInput7Hook::resetCooperativeLevel();
  DirectInput8Hook::resetCooperativeLevel();
}

}
