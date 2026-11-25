/*
* Copyright (c) 2026, Kim2091
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

namespace dxvk {
  class DxvkContext;

  // The notes have to be drawn before the tab bar opens, because text inside a tab bar overlaps the first tab's controls.
  void showNumosNotes(DxvkContext* pCtx);

  // Draws the tab items of the NUMOS pages inside the sky's tab bar.
  void showNumosTabs(DxvkContext* pCtx);

  void showWeatherPresetSummary(DxvkContext* pCtx);

  // Draws nothing while the window is closed.
  void showWeatherPresetWindow(DxvkContext* pCtx);

  // The user menu's controls change only settings that rtx_numos_settings.cpp flags as user settings.
  void showNumosUserHaze(DxvkContext* pCtx);
  void showNumosUserClouds(DxvkContext* pCtx);
  void showNumosUserNight(DxvkContext* pCtx);
}
