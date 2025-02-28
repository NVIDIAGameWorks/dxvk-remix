#include "dxvk_common.h"
#include "rtx_render/rtx_dlss.h"
#include "rtx_render/rtx_fsr.h"

namespace dxvk {

DxvkCommon::DxvkCommon(DxvkDevice* device)
  : m_device(device)
  , m_dlss(device)
  , m_fsr(device) {  // Initialize FSR
}

DxvkCommon::~DxvkCommon() {
}

} // namespace dxvk 