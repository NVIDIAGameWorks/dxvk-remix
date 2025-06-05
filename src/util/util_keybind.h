/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/xxHash/xxhash.h"

#include <windows.h>

#include <array>
#include <unordered_map>
#include <sstream>
#include <string>

namespace dxvk {
    
using VkValue = uint8_t;
struct VirtualKey {
    VkValue val = kInvalidVal;
    static constexpr VkValue kInvalidVal = 0xFF;

    bool operator==(const VirtualKey& other) const {
        return val == other.val;
    }
};
using VirtualKeys = std::vector<VirtualKey>;

class KeyBind {
public:
    static std::string getName(const VirtualKey vk) {
        const auto itr = get().m_vkValToKey.find(vk.val);
        if(itr == get().m_vkValToKey.cend()) {
            return Key::kInvalidName;
        }
        return itr->second->names.at(0);
    }
    static VirtualKey getVk(const std::string name) {
        const auto itr = get().m_nameToKey.find(name);
        if(itr == get().m_nameToKey.cend()) {
            return VirtualKey(); // Invalid
        }
        return VirtualKey{itr->second->vkVal};
    }
    static bool isValidVk(const VirtualKey& vk) {
        return vk.val != VirtualKey::kInvalidVal && 
               get().m_vkValToKey.find(vk.val) != get().m_vkValToKey.cend();
    }

private:
    static const KeyBind& get() {
        const static KeyBind single;
        return single;
    }

    struct Key {
        std::vector<std::string> names = {kInvalidName};
        VkValue                  vkVal = VirtualKey::kInvalidVal;

        static constexpr size_t kDefaultNameIdx = 0;
        inline static const std::string kInvalidName = "INVALID";
    };
    static constexpr size_t kNumKeys = 256;
    using KeyArray = std::array<Key, kNumKeys>;
    const KeyArray m_keys;
    using NameToKey = std::unordered_map<std::string, const Key* const>;
    const NameToKey m_nameToKey;
    using VkValToKey = std::unordered_map<VkValue, const Key* const>;
    const VkValToKey m_vkValToKey;
    
    KeyBind() : 
        m_keys{
            Key{{"LBUTTON"}, VK_LBUTTON},
            Key{{"RBUTTON"}, VK_RBUTTON},
            Key{{"CANCEL"}, VK_CANCEL},
            Key{{"MBUTTON"}, VK_MBUTTON},
            Key{{"XBUTTON1"}, VK_XBUTTON1},
            Key{{"XBUTTON2"}, VK_XBUTTON2},
            Key{{"BACK"}, VK_BACK},
            Key{{"TAB"}, VK_TAB},
            Key{{"CLEAR"}, VK_CLEAR},
            Key{{"ENTER","RETURN"}, VK_RETURN},
            Key{{"SHFT","SHIFT"}, VK_SHIFT},
            Key{{"CTRL","CONTROL"}, VK_CONTROL},
            Key{{"ALT","MENU"}, VK_MENU},
            Key{{"PAUSE"}, VK_PAUSE},
            Key{{"CAPITAL"}, VK_CAPITAL},
            Key{{"KANA"}, VK_KANA},
            Key{{"IME_ON"}, VK_IME_ON},
            Key{{"JUNJA"}, VK_JUNJA},
            Key{{"FINAL"}, VK_FINAL},
            Key{{"HANJA"}, VK_HANJA},
            Key{{"IME_OFF"}, VK_IME_OFF},
            Key{{"ESCAPE"}, VK_ESCAPE},
            Key{{"CONVERT"}, VK_CONVERT},
            Key{{"NONCONVERT"}, VK_NONCONVERT},
            Key{{"ACCEPT"}, VK_ACCEPT},
            Key{{"MODECHANGE"}, VK_MODECHANGE},
            Key{{"SPACE"}, VK_SPACE},
            Key{{"PRIOR"}, VK_PRIOR},
            Key{{"NEXT"}, VK_NEXT},
            Key{{"END"}, VK_END},
            Key{{"HOME"}, VK_HOME},
            Key{{"LEFT"}, VK_LEFT},
            Key{{"UP"}, VK_UP},
            Key{{"RIGHT"}, VK_RIGHT},
            Key{{"DOWN"}, VK_DOWN},
            Key{{"SELECT"}, VK_SELECT},
            Key{{"PRINT"}, VK_PRINT},
            Key{{"EXECUTE"}, VK_EXECUTE},
            Key{{"SNAPSHOT"}, VK_SNAPSHOT},
            Key{{"INSERT"}, VK_INSERT},
            Key{{"DELETE"}, VK_DELETE},
            Key{{"HELP"}, VK_HELP},
            Key{{"0"}, '0'},
            Key{{"1"}, '1'},
            Key{{"2"}, '2'},
            Key{{"3"}, '3'},
            Key{{"4"}, '4'},
            Key{{"5"}, '5'},
            Key{{"6"}, '6'},
            Key{{"7"}, '7'},
            Key{{"8"}, '8'},
            Key{{"9"}, '9'},
            Key{{"A"}, 'A'},
            Key{{"B"}, 'B'},
            Key{{"C"}, 'C'},
            Key{{"D"}, 'D'},
            Key{{"E"}, 'E'},
            Key{{"F"}, 'F'},
            Key{{"G"}, 'G'},
            Key{{"H"}, 'H'},
            Key{{"I"}, 'I'},
            Key{{"J"}, 'J'},
            Key{{"K"}, 'K'},
            Key{{"L"}, 'L'},
            Key{{"M"}, 'M'},
            Key{{"N"}, 'N'},
            Key{{"O"}, 'O'},
            Key{{"P"}, 'P'},
            Key{{"Q"}, 'Q'},
            Key{{"R"}, 'R'},
            Key{{"S"}, 'S'},
            Key{{"T"}, 'T'},
            Key{{"U"}, 'U'},
            Key{{"V"}, 'V'},
            Key{{"W"}, 'W'},
            Key{{"X"}, 'X'},
            Key{{"Y"}, 'Y'},
            Key{{"Z"}, 'Z'},
            Key{{"LWIN"}, VK_LWIN},
            Key{{"RWIN"}, VK_RWIN},
            Key{{"APPS"}, VK_APPS},
            Key{{"SLEEP"}, VK_SLEEP},
            Key{{"NUMPAD0"}, VK_NUMPAD0},
            Key{{"NUMPAD1"}, VK_NUMPAD1},
            Key{{"NUMPAD2"}, VK_NUMPAD2},
            Key{{"NUMPAD3"}, VK_NUMPAD3},
            Key{{"NUMPAD4"}, VK_NUMPAD4},
            Key{{"NUMPAD5"}, VK_NUMPAD5},
            Key{{"NUMPAD6"}, VK_NUMPAD6},
            Key{{"NUMPAD7"}, VK_NUMPAD7},
            Key{{"NUMPAD8"}, VK_NUMPAD8},
            Key{{"NUMPAD9"}, VK_NUMPAD9},
            Key{{"MULTIPLY"}, VK_MULTIPLY},
            Key{{"ADD"}, VK_ADD},
            Key{{"SEPARATOR"}, VK_SEPARATOR},
            Key{{"SUBTRACT"}, VK_SUBTRACT},
            Key{{"DECIMAL"}, VK_DECIMAL},
            Key{{"DIVIDE"}, VK_DIVIDE},
            Key{{"F1"}, VK_F1},
            Key{{"F2"}, VK_F2},
            Key{{"F3"}, VK_F3},
            Key{{"F4"}, VK_F4},
            Key{{"F5"}, VK_F5},
            Key{{"F6"}, VK_F6},
            Key{{"F7"}, VK_F7},
            Key{{"F8"}, VK_F8},
            Key{{"F9"}, VK_F9},
            Key{{"F10"}, VK_F10},
            Key{{"F11"}, VK_F11},
            Key{{"F12"}, VK_F12},
            Key{{"F13"}, VK_F13},
            Key{{"F14"}, VK_F14},
            Key{{"F15"}, VK_F15},
            Key{{"F16"}, VK_F16},
            Key{{"F17"}, VK_F17},
            Key{{"F18"}, VK_F18},
            Key{{"F19"}, VK_F19},
            Key{{"F20"}, VK_F20},
            Key{{"F21"}, VK_F21},
            Key{{"F22"}, VK_F22},
            Key{{"F23"}, VK_F23},
            Key{{"F24"}, VK_F24},
            Key{{"NAVIGATION_VIEW"}, VK_NAVIGATION_VIEW},
            Key{{"NAVIGATION_MENU"}, VK_NAVIGATION_MENU},
            Key{{"NAVIGATION_UP"}, VK_NAVIGATION_UP},
            Key{{"NAVIGATION_DOWN"}, VK_NAVIGATION_DOWN},
            Key{{"NAVIGATION_LEFT"}, VK_NAVIGATION_LEFT},
            Key{{"NAVIGATION_RIGHT"}, VK_NAVIGATION_RIGHT},
            Key{{"NAVIGATION_ACCEPT"}, VK_NAVIGATION_ACCEPT},
            Key{{"NAVIGATION_CANCEL"}, VK_NAVIGATION_CANCEL},
            Key{{"NUMLOCK"}, VK_NUMLOCK},
            Key{{"SCROLL"}, VK_SCROLL},
            Key{{"OEM_NEC_EQUAL"}, VK_OEM_NEC_EQUAL},
            Key{{"OEM_FJ_MASSHOU"}, VK_OEM_FJ_MASSHOU},
            Key{{"OEM_FJ_TOUROKU"}, VK_OEM_FJ_TOUROKU},
            Key{{"OEM_FJ_LOYA"}, VK_OEM_FJ_LOYA},
            Key{{"OEM_FJ_ROYA"}, VK_OEM_FJ_ROYA},
            Key{{"LSHIFT"}, VK_LSHIFT},
            Key{{"RSHIFT"}, VK_RSHIFT},
            Key{{"LCONTROL"}, VK_LCONTROL},
            Key{{"RCONTROL"}, VK_RCONTROL},
            Key{{"LMENU"}, VK_LMENU},
            Key{{"RMENU"}, VK_RMENU},
            Key{{"BROWSER_BACK"}, VK_BROWSER_BACK},
            Key{{"BROWSER_FORWARD"}, VK_BROWSER_FORWARD},
            Key{{"BROWSER_REFRESH"}, VK_BROWSER_REFRESH},
            Key{{"BROWSER_STOP"}, VK_BROWSER_STOP},
            Key{{"BROWSER_SEARCH"}, VK_BROWSER_SEARCH},
            Key{{"BROWSER_FAVORITES"}, VK_BROWSER_FAVORITES},
            Key{{"BROWSER_HOME"}, VK_BROWSER_HOME},
            Key{{"VOLUME_MUTE"}, VK_VOLUME_MUTE},
            Key{{"VOLUME_DOWN"}, VK_VOLUME_DOWN},
            Key{{"VOLUME_UP"}, VK_VOLUME_UP},
            Key{{"MEDIA_NEXT_TRACK"}, VK_MEDIA_NEXT_TRACK},
            Key{{"MEDIA_PREV_TRACK"}, VK_MEDIA_PREV_TRACK},
            Key{{"MEDIA_STOP"}, VK_MEDIA_STOP},
            Key{{"MEDIA_PLAY_PAUSE"}, VK_MEDIA_PLAY_PAUSE},
            Key{{"LAUNCH_MAIL"}, VK_LAUNCH_MAIL},
            Key{{"LAUNCH_MEDIA_SELECT"}, VK_LAUNCH_MEDIA_SELECT},
            Key{{"LAUNCH_APP1"}, VK_LAUNCH_APP1},
            Key{{"LAUNCH_APP2"}, VK_LAUNCH_APP2},
            Key{{"OEM_1"}, VK_OEM_1},
            Key{{"OEM_PLUS"}, VK_OEM_PLUS},
            Key{{"OEM_COMMA"}, VK_OEM_COMMA},
            Key{{"OEM_MINUS"}, VK_OEM_MINUS},
            Key{{"OEM_PERIOD"}, VK_OEM_PERIOD},
            Key{{"OEM_2"}, VK_OEM_2},
            Key{{"OEM_3"}, VK_OEM_3},
            Key{{"GAMEPAD_A"}, VK_GAMEPAD_A},
            Key{{"GAMEPAD_B"}, VK_GAMEPAD_B},
            Key{{"GAMEPAD_X"}, VK_GAMEPAD_X},
            Key{{"GAMEPAD_Y"}, VK_GAMEPAD_Y},
            Key{{"GAMEPAD_RIGHT_SHOULDER"}, VK_GAMEPAD_RIGHT_SHOULDER},
            Key{{"GAMEPAD_LEFT_SHOULDER"}, VK_GAMEPAD_LEFT_SHOULDER},
            Key{{"GAMEPAD_LEFT_TRIGGER"}, VK_GAMEPAD_LEFT_TRIGGER},
            Key{{"GAMEPAD_RIGHT_TRIGGER"}, VK_GAMEPAD_RIGHT_TRIGGER},
            Key{{"GAMEPAD_DPAD_UP"}, VK_GAMEPAD_DPAD_UP},
            Key{{"GAMEPAD_DPAD_DOWN"}, VK_GAMEPAD_DPAD_DOWN},
            Key{{"GAMEPAD_DPAD_LEFT"}, VK_GAMEPAD_DPAD_LEFT},
            Key{{"GAMEPAD_DPAD_RIGHT"}, VK_GAMEPAD_DPAD_RIGHT},
            Key{{"GAMEPAD_MENU"}, VK_GAMEPAD_MENU},
            Key{{"GAMEPAD_VIEW"}, VK_GAMEPAD_VIEW},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_BUTTON"}, VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_BUTTON"}, VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_UP"}, VK_GAMEPAD_LEFT_THUMBSTICK_UP},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_DOWN"}, VK_GAMEPAD_LEFT_THUMBSTICK_DOWN},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_RIGHT"}, VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_LEFT"}, VK_GAMEPAD_LEFT_THUMBSTICK_LEFT},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_UP"}, VK_GAMEPAD_RIGHT_THUMBSTICK_UP},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_DOWN"}, VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_RIGHT"}, VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_LEFT"}, VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT},
            Key{{"OEM_4"}, VK_OEM_4},
            Key{{"OEM_5"}, VK_OEM_5},
            Key{{"OEM_6"}, VK_OEM_6},
            Key{{"OEM_7"}, VK_OEM_7},
            Key{{"OEM_8"}, VK_OEM_8},
            Key{{"OEM_AX"}, VK_OEM_AX},
            Key{{"OEM_102"}, VK_OEM_102},
            Key{{"ICO_HELP"}, VK_ICO_HELP},
            Key{{"ICO_00"}, VK_ICO_00},
            Key{{"PROCESSKEY"}, VK_PROCESSKEY},
            Key{{"ICO_CLEAR"}, VK_ICO_CLEAR},
            Key{{"PACKET"}, VK_PACKET},
            Key{{"OEM_RESET"}, VK_OEM_RESET},
            Key{{"OEM_JUMP"}, VK_OEM_JUMP},
            Key{{"OEM_PA1"}, VK_OEM_PA1},
            Key{{"OEM_PA2"}, VK_OEM_PA2},
            Key{{"OEM_PA3"}, VK_OEM_PA3},
            Key{{"OEM_WSCTRL"}, VK_OEM_WSCTRL},
            Key{{"OEM_CUSEL"}, VK_OEM_CUSEL},
            Key{{"OEM_ATTN"}, VK_OEM_ATTN},
            Key{{"OEM_FINISH"}, VK_OEM_FINISH},
            Key{{"OEM_COPY"}, VK_OEM_COPY},
            Key{{"OEM_AUTO"}, VK_OEM_AUTO},
            Key{{"OEM_ENLW"}, VK_OEM_ENLW},
            Key{{"OEM_BACKTAB"}, VK_OEM_BACKTAB},
            Key{{"ATTN"}, VK_ATTN},
            Key{{"CRSEL"}, VK_CRSEL},
            Key{{"EXSEL"}, VK_EXSEL},
            Key{{"EREOF"}, VK_EREOF},
            Key{{"PLAY"}, VK_PLAY},
            Key{{"ZOOM"}, VK_ZOOM},
            Key{{"NONAME"}, VK_NONAME},
            Key{{"PA1"}, VK_PA1},
            Key{{"OEM_CLEAR"}, VK_OEM_CLEAR},
            Key{{"INVALID"}, 0xFF}
        },
        m_nameToKey(initNameToKey(m_keys)),
        m_vkValToKey(initVkValToKey(m_keys))
    {}
    ~KeyBind() {}
    KeyBind(const KeyBind& other) = delete;
    KeyBind(const KeyBind&& other) = delete;

    static NameToKey initNameToKey(const KeyArray& keyArray) {
        NameToKey nameToKey;
        for(const auto& key : keyArray) {
            for(const auto& name : key.names) {
                nameToKey.emplace(name, &key);
            }
        }
        return nameToKey;
    }
    
    static VkValToKey initVkValToKey(const KeyArray& keyArray) {
        VkValToKey vkValToKey;
        for(const auto& key : keyArray) {
            vkValToKey.emplace(key.vkVal, &key);
        }
        return vkValToKey;
    } 
};

static std::string buildKeyBindDescriptorString(const VirtualKeys& virtKeys) {
    std::stringstream ss;
    bool bIsFirst = true;
    for(const auto& virtKey : virtKeys) {
        if(!bIsFirst) {
            ss << " + ";
        }
        ss << KeyBind::getName(virtKey);
        bIsFirst = false;
    }
    return ss.str();
}

}
