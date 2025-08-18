#include "dxvk_device_filter.h"

namespace dxvk {
  
  DxvkDeviceFilter::DxvkDeviceFilter(DxvkDeviceFilterFlags flags)
  : m_flags(flags) {
    m_matchDeviceName = env::getEnvVar("DXVK_FILTER_DEVICE_NAME");
    
    if (m_matchDeviceName.size() != 0)
      m_flags.set(DxvkDeviceFilterFlag::MatchDeviceName);
  }
  
  
  DxvkDeviceFilter::~DxvkDeviceFilter() {
    
  }
  
  
  bool DxvkDeviceFilter::testAdapter(const VkPhysicalDeviceProperties& properties) const {
    if (properties.apiVersion < VK_MAKE_VERSION(1, 1, 0)) {
      Logger::warn(str::format("Skipping Vulkan 1.0 adapter: ", properties.deviceName));
      return false;
    }

    if (m_flags.test(DxvkDeviceFilterFlag::MatchDeviceName)) {
      if (std::string(properties.deviceName).find(m_matchDeviceName) == std::string::npos)
        return false;
    }

    if (m_flags.test(DxvkDeviceFilterFlag::SkipCpuDevices)) {
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        Logger::warn(str::format("Skipping CPU adapter: ", properties.deviceName));
        return false;
      }
    }

    // NV-DXVK start: Integrated GPU device filter
    if (m_flags.test(DxvkDeviceFilterFlag::SkipIntegratedGPUDevices)) {
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        Logger::warn(str::format("Skipping Integrated GPU adapter: ", properties.deviceName));
        return false;
      }
    }
    // NV-DXVK end

    return true;
  }
  
}
