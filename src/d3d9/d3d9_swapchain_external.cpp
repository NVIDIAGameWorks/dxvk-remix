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
#include "d3d9_swapchain.h"
#include "d3d9_surface.h"
#include "d3d9_monitor.h"

#include "d3d9_hud.h"
#include "../util/util_env.h"
#include "../dxvk/rtx_render/rtx_bridge_message_channel.h"
#include "../dxvk/dxvk_scoped_annotation.h"

namespace dxvk {

  D3D9SwapchainExternal::D3D9SwapchainExternal(
        D3D9DeviceEx* pDevice,
        D3DPRESENT_PARAMETERS* pPresentParams,
  const D3DDISPLAYMODEEX* pFullscreenDisplayMode)
    : D3D9SwapChainEx(pDevice, pPresentParams, pFullscreenDisplayMode) {
    m_frameEndSemaphore = RtxSemaphore::createBinary(pDevice->GetDXVKDevice().ptr(), "ExternalPresenter::frameEnd");
    m_frameResumeSemaphore = RtxSemaphore::createBinary(pDevice->GetDXVKDevice().ptr(), "ExternalPresenter::frameResume");
  }

  HRESULT STDMETHODCALLTYPE D3D9SwapchainExternal::Present(const RECT*, const RECT*, HWND, const RGNDATA*,  DWORD) {
    auto targetImage = m_backBuffers[0]->GetCommonTexture()->GetImage();

    auto& imageInfo = targetImage->info();

    m_parent->m_rtx.EndFrame(targetImage);

    m_parent->Flush();
    m_parent->SynchronizeCsThread();

    m_context->beginRecording(m_device->createCommandList());

    // Retrieve the image and image view to present
    auto swapImage = m_backBuffers[0]->GetCommonTexture()->GetImage();
    auto swapImageView = m_backBuffers[0]->GetImageView(false);

    VkSurfaceFormatKHR fmt;
    fmt.format = imageInfo.format;
    fmt.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    if (m_hud != nullptr)
      m_hud->render(m_context, fmt, { imageInfo.extent.width,imageInfo.extent.height });

    // TODO: Figure out if we want HUD rendering, and how to use it
    //m_device->getCommon()->getImgui().render(m_window, m_context, fmt, { imageInfo.extent.width,imageInfo.extent.height }, m_vsync);

    m_parent->m_rtx.OnPresent(targetImage);

    m_context->changeImageLayout(swapImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_context->emitMemoryBarrier(VK_DEPENDENCY_DEVICE_GROUP_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_ACCESS_SHADER_READ_BIT);

    // signal to external that rendewring is done
    m_context->getCommandList()->addSignalSemaphore(m_frameEndSemaphore->handle(), 1);
    m_device->submitCommandList(m_context->endRecording(), VK_NULL_HANDLE, VK_NULL_HANDLE);


    m_parent->GetDXVKDevice()->incrementPresentCount();

    // Wait on the next frame before resuming rendering
    m_parent->EmitCs([this](DxvkContext* ctx) {
      ctx->getCommandList()->addWaitSemaphore(m_frameResumeSemaphore->handle(), 1);
      ctx->flushCommandList();
    });

    return S_OK;
  }

}
