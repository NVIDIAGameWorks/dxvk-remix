#include "dxvk_hud_item.h"

#include <iomanip>
#include <version.h>

#include "rtx_render/rtx_options.h"

namespace dxvk::hud {

  HudItem::~HudItem() {

  }


  void HudItem::update(dxvk::high_resolution_clock::time_point time) {
    // Do nothing by default. Some items won't need this.
  }


  HudItemSet::HudItemSet(const Rc<DxvkDevice>& device) {
    std::string configStr = env::getEnvVar("DXVK_HUD");

    if (configStr.empty())
      configStr = device->config().hud;

    std::string::size_type pos = 0;
    std::string::size_type end = 0;
    std::string::size_type mid = 0;
    
    while (pos < configStr.size()) {
      end = configStr.find(',', pos);
      mid = configStr.find('=', pos);
      
      if (end == std::string::npos)
        end = configStr.size();
      
      if (mid != std::string::npos && mid < end) {
        m_options.insert({
          configStr.substr(pos,     mid - pos),
          configStr.substr(mid + 1, end - mid - 1) });
      } else {
        m_enabled.insert(configStr.substr(pos, end - pos));
      }

      pos = end + 1;
    }

    if (m_enabled.find("full") != m_enabled.end())
      m_enableFull = true;
    
    if (m_enabled.find("1") != m_enabled.end()) {
      m_enabled.insert("version");
      m_enabled.insert("devinfo");
      m_enabled.insert("raytracingMode");
      m_enabled.insert("fps");
      m_enabled.insert("memory");
      m_enabled.insert("gpuload");
      m_enabled.insert("rtx");
    }
  }


  HudItemSet::~HudItemSet() {

  }


  // NV-DXVK start: DLFG integration
  void HudItemSet::update(uint32_t presentCount) {
    auto time = dxvk::high_resolution_clock::now();

    for (const auto& item : m_items) {
      item->setPresentCount(presentCount);
      item->update(time);
    }
  }
  // NV-DXVK end

  void HudItemSet::render(HudRenderer& renderer) {
    HudPos position = { 8.0f, 8.0f };

    for (const auto& item : m_items)
      position = item->render(renderer, position);
  }


  void HudItemSet::parseOption(const std::string& str, float& value) {
    try {
      value = std::stof(str);
    } catch (const std::invalid_argument&) {
      return;
    }
  }


  HudPos HudVersionItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      "DXVK " DXVK_VERSION);

    position.y += 8.0f;
    return position;
  }


  HudClientApiItem::HudClientApiItem(std::string api)
  : m_api(api) {

  }


  HudClientApiItem::~HudClientApiItem() {

  }


  HudPos HudClientApiItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_api);

    position.y += 8.0f;
    return position;
  }


  HudDeviceInfoItem::HudDeviceInfoItem(const Rc<DxvkDevice>& device) {
    VkPhysicalDeviceProperties props = device->adapter()->deviceProperties();

    m_deviceName = props.deviceName;
    m_driverVer = str::format("Driver: ",
      VK_VERSION_MAJOR(props.driverVersion), ".",
      VK_VERSION_MINOR(props.driverVersion), ".",
      VK_VERSION_PATCH(props.driverVersion));
    m_vulkanVer = str::format("Vulkan: ",
      VK_VERSION_MAJOR(props.apiVersion), ".",
      VK_VERSION_MINOR(props.apiVersion), ".",
      VK_VERSION_PATCH(props.apiVersion));
  }


  HudDeviceInfoItem::~HudDeviceInfoItem() {

  }


  HudPos HudDeviceInfoItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_deviceName);
    
    position.y += 24.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_driverVer);
    
    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_vulkanVer);

    position.y += 8.0f;
    return position;
  }


  HudPos HudRaytracingModeItem::render(
    HudRenderer&      renderer,
    HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 0.25f, 1.0f },
      "Raytracing Mode: ");
    
    if (RtxOptions::Get()->enableRaytracing()) {
      position.y += 16.0f;
      renderer.drawText(14.0f,
        { position.x, position.y },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        str::format("GBuffer [", DxvkPathtracerGbuffer::raytraceModeToString(RtxOptions::Get()->getRenderPassGBufferRaytraceMode()), "]"));

      position.y += 16.0f;
      renderer.drawText(14.0f,
        { position.x, position.y },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        str::format("Integrate Direct [", DxvkPathtracerIntegrateDirect::raytraceModeToString(RtxOptions::Get()->getRenderPassIntegrateDirectRaytraceMode()), "]"));

      position.y += 16.0f;
      renderer.drawText(14.0f,
        { position.x, position.y },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        str::format("Integrate Indirect [", DxvkPathtracerIntegrateIndirect::raytraceModeToString(RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode()), "]"));
    } else {
      position.y += 16.0f;
      renderer.drawText(14.0f,
        { position.x, position.y },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "RTX-Off (Raster)");
    }

    position.y += 16.0f;

    return position;
  }

  HudFpsItem::HudFpsItem() { }
  HudFpsItem::~HudFpsItem() { }

  // NV-DXVK start: DLFG integration
  void HudFpsItem::setPresentCount(uint32_t presentCount) {
    m_presentCount = presentCount;
  }
  // NV-DXVK end

  void HudFpsItem::update(dxvk::high_resolution_clock::time_point time) {
    // NV-DXVK start: DLFG integration
    m_frameCount += m_presentCount;
    // NV-DXVK end

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate);

    if (elapsed.count() >= UpdateInterval) {
      int64_t fps = (10'000'000ll * m_frameCount) / elapsed.count();
      int64_t frameTime = elapsed.count() / 100 / m_frameCount;

      m_frameRate = str::format(fps / 10, ".", fps % 10);
      m_frameTime = str::format(frameTime / 10, ".", frameTime % 10);
      m_frameCount = 0;
      m_lastUpdate = time;
    }
  }


  HudPos HudFpsItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 0.25f, 0.25f, 1.0f },
      "FPS:");

    renderer.drawText(16.0f,
      { position.x + 60.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_frameRate);

    renderer.drawText(16.0f,
      { position.x + 140.0f, position.y },
      { 1.0f, 0.25f, 0.25f, 1.0f },
      "Frame Time:");

    renderer.drawText(16.0f,
      { position.x + 285.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_frameTime);

    position.y += 8.0f;
    return position;
  }


  HudFrameTimeItem::HudFrameTimeItem() { }
  HudFrameTimeItem::~HudFrameTimeItem() { }


  void HudFrameTimeItem::update(dxvk::high_resolution_clock::time_point time) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate);

    m_dataPoints[m_dataPointId] = float(elapsed.count());
    m_dataPointId = (m_dataPointId + 1) % NumDataPoints;

    m_lastUpdate = time;
  }


  HudPos HudFrameTimeItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    std::array<HudLineVertex, NumDataPoints * 2> vData;
    position.y += 40.0f;

    // 60 FPS = optimal, 10 FPS = worst
    const float targetUs =  16'666.6f;
    const float minUs    =   5'000.0f;
    const float maxUs    = 100'000.0f;
    
    // Ten times the maximum/minimum number
    // of milliseconds for a single frame
    uint32_t minMs = 0xFFFFFFFFu;
    uint32_t maxMs = 0x00000000u;
    
    // Paint the time points
    for (uint32_t i = 0; i < NumDataPoints; i++) {
      float us = m_dataPoints[(m_dataPointId + i) % NumDataPoints];
      
      minMs = std::min(minMs, uint32_t(us / 100.0f));
      maxMs = std::max(maxMs, uint32_t(us / 100.0f));
      
      float r = std::min(std::max(-1.0f + us / targetUs, 0.0f), 1.0f);
      float g = std::min(std::max( 3.0f - us / targetUs, 0.0f), 1.0f);
      float l = std::sqrt(r * r + g * g);
      
      HudNormColor color = {
        uint8_t(255.0f * (r / l)),
        uint8_t(255.0f * (g / l)),
        uint8_t(0), uint8_t(255) };
      
      float x = position.x + float(i);
      float y = position.y;
      
      float hVal = std::log2(std::max((us - minUs) / targetUs + 1.0f, 1.0f))
                 / std::log2((maxUs - minUs) / targetUs);
      float h = std::min(std::max(40.0f * hVal, 2.0f), 40.0f);
      
      vData[2 * i + 0] = HudLineVertex { { x, y     }, color };
      vData[2 * i + 1] = HudLineVertex { { x, y - h }, color };
    }
    
    renderer.drawLines(vData.size(), vData.data());
    
    // Paint min/max frame times in the entire window
    position.y += 18.0f;

    renderer.drawText(12.0f,
      { position.x, position.y },
      { 1.0f, 0.25f, 0.25f, 1.0f },
      "min:");

    renderer.drawText(12.0f,
      { position.x + 45.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(minMs / 10, ".", minMs % 10));
    
    renderer.drawText(12.0f,
      { position.x + 150.0f, position.y },
      { 1.0f, 0.25f, 0.25f, 1.0f },
      "max:");
    
    renderer.drawText(12.0f,
      { position.x + 195.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(maxMs / 10, ".", maxMs % 10));
    
    position.y += 4.0f;
    return position;
  }


  HudSubmissionStatsItem::HudSubmissionStatsItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudSubmissionStatsItem::~HudSubmissionStatsItem() {

  }


  void HudSubmissionStatsItem::update(dxvk::high_resolution_clock::time_point time) {
    DxvkStatCounters counters = m_device->getStatCounters();
    
    uint32_t currCounter = counters.getCtr(DxvkStatCounter::QueueSubmitCount);
    m_diffCounter = std::max(m_diffCounter, currCounter - m_prevCounter);
    m_prevCounter = currCounter;

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate);

    if (elapsed.count() >= UpdateInterval) {
      m_showCounter = m_diffCounter;
      m_diffCounter = 0;

      m_lastUpdate = time;
    }
  }


  HudPos HudSubmissionStatsItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 0.5f, 0.25f, 1.0f },
      "Queue submissions: ");

    renderer.drawText(16.0f,
      { position.x + 228.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_showCounter));

    position.y += 8.0f;
    return position;
  }


  HudDrawCallStatsItem::HudDrawCallStatsItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudDrawCallStatsItem::~HudDrawCallStatsItem() {

  }


  void HudDrawCallStatsItem::update(dxvk::high_resolution_clock::time_point time) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate);

    DxvkStatCounters counters = m_device->getStatCounters();
    auto diffCounters = counters.diff(m_prevCounters);

    if (elapsed.count() >= UpdateInterval) {
      m_gpCount = diffCounters.getCtr(DxvkStatCounter::CmdDrawCalls);
      m_cpCount = diffCounters.getCtr(DxvkStatCounter::CmdDispatchCalls);
      m_rtpCount = diffCounters.getCtr(DxvkStatCounter::CmdTraceRaysCalls);
      m_rpCount = diffCounters.getCtr(DxvkStatCounter::CmdRenderPassCount);

      m_lastUpdate = time;
    }

    m_prevCounters = counters;
  }


  HudPos HudDrawCallStatsItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 1.0f, 1.0f },
      "Draw calls:");
    
    renderer.drawText(16.0f,
      { position.x + 192.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_gpCount));
    
    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 1.0f, 1.0f },
      "Dispatch calls:");
    
    renderer.drawText(16.0f,
      { position.x + 192.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_cpCount));

    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 1.0f, 1.0f },
      "TraceRays calls:");

    renderer.drawText(16.0f,
      { position.x + 192.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_rtpCount));
    
    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 1.0f, 1.0f },
      "Render passes:");
    
    renderer.drawText(16.0f,
      { position.x + 192.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_rpCount));
    
    position.y += 8.0f;
    return position;
  }


  HudPipelineStatsItem::HudPipelineStatsItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudPipelineStatsItem::~HudPipelineStatsItem() {

  }


  void HudPipelineStatsItem::update(dxvk::high_resolution_clock::time_point time) {
    DxvkStatCounters counters = m_device->getStatCounters();

    m_graphicsPipelines = counters.getCtr(DxvkStatCounter::PipeCountGraphics);
    m_computePipelines  = counters.getCtr(DxvkStatCounter::PipeCountCompute);
  }


  HudPos HudPipelineStatsItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 0.25f, 1.0f, 1.0f },
      "Graphics pipelines:");
    
    renderer.drawText(16.0f,
      { position.x + 240.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_graphicsPipelines));
    
    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 1.0f, 0.25f, 1.0f, 1.0f },
      "Compute pipelines:");

    renderer.drawText(16.0f,
      { position.x + 240.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      str::format(m_computePipelines));

    position.y += 8.0f;
    return position;
  }


  HudMemoryStatsItem::HudMemoryStatsItem(const Rc<DxvkDevice>& device)
  : m_device(device), m_memory(device->adapter()->memoryProperties()) {

  }


  HudMemoryStatsItem::~HudMemoryStatsItem() {

  }


  void HudMemoryStatsItem::update(dxvk::high_resolution_clock::time_point time) {
    for (uint32_t i = 0; i < m_memory.memoryHeapCount; i++)
      m_heaps[i] = m_device->getMemoryStats(i);
  }


  HudPos HudMemoryStatsItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    for (uint32_t i = 0; i < m_memory.memoryHeapCount; i++) {
      bool isDeviceLocal = m_memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

      VkDeviceSize memSizeMib = m_memory.memoryHeaps[i].size >> 20;
      VkDeviceSize memUsedMib = m_heaps[i].totalUsed() >> 20;
      VkDeviceSize memAllocatedMib = m_heaps[i].totalAllocated() >> 20;
      uint64_t percentage = (100 * memUsedMib) / memSizeMib;

      std::string label = str::format(isDeviceLocal ? "Vidmem" : "Sysmem", " heap ", i, ":");
      std::string text  = str::format(std::setfill(' '), std::setw(5), memUsedMib, " / ", memAllocatedMib, " / ", memSizeMib, " MB(", percentage, "%)");

      position.y += 16.0f;
      renderer.drawText(16.0f,
        { position.x, position.y },
        { 1.0f, 1.0f, 0.25f, 1.0f },
        label);

      renderer.drawText(16.0f,
        { position.x + 168.0f, position.y },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        text);
      position.y += 4.0f;

      if (isDeviceLocal) {
        for (uint32_t cat = DxvkMemoryStats::Category::First; cat <= DxvkMemoryStats::Category::Last; cat++) {
          VkDeviceSize memSizeMib = m_heaps[i].usedByCategory(DxvkMemoryStats::Category(cat)) >> 20;
          if (memSizeMib == 0) {
            continue;
          }

          std::string text = str::format(std::setfill(' '), std::setw(5), DxvkMemoryStats::categoryToString(DxvkMemoryStats::Category(cat)), ": ", memSizeMib, " MB");
          position.y += 16.0f;
          renderer.drawText(16.0f,
                            { position.x + 16.0f, position.y },
                            { 1.0f, 1.0f, 1.0f, 1.0f },
                            text);
          position.y += 4.0f;
        }

        position.y += 16.0f;
      }
    }

    position.y += 4.0f;
    return position;
  }


  HudCsThreadItem::HudCsThreadItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudCsThreadItem::~HudCsThreadItem() {

  }


  void HudCsThreadItem::update(dxvk::high_resolution_clock::time_point time) {
    uint64_t ticks = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate).count();

    // Capture the maximum here since it's more useful to
    // identify stutters than using any sort of average
    DxvkStatCounters counters = m_device->getStatCounters();
    uint64_t currCsSyncCount = counters.getCtr(DxvkStatCounter::CsSyncCount);
    uint64_t currCsSyncTicks = counters.getCtr(DxvkStatCounter::CsSyncTicks);

    m_maxCsSyncCount = std::max(m_maxCsSyncCount, currCsSyncCount - m_prevCsSyncCount);
    m_maxCsSyncTicks = std::max(m_maxCsSyncTicks, currCsSyncTicks - m_prevCsSyncTicks);

    m_prevCsSyncCount = currCsSyncCount;
    m_prevCsSyncTicks = currCsSyncTicks;

    m_updateCount++;

    if (ticks >= UpdateInterval) {
      uint64_t currCsChunks = counters.getCtr(DxvkStatCounter::CsChunkCount);
      uint64_t diffCsChunks = (currCsChunks - m_prevCsChunks) / m_updateCount;
      m_prevCsChunks = currCsChunks;

      uint64_t syncTicks = m_maxCsSyncTicks / 100;

      m_csChunkString = str::format(diffCsChunks);
      m_csSyncString = m_maxCsSyncCount
        ? str::format(m_maxCsSyncCount, " (", (syncTicks / 10), ".", (syncTicks % 10), " ms)")
        : str::format(m_maxCsSyncCount);

      m_maxCsSyncCount = 0;
      m_maxCsSyncTicks = 0;

      m_updateCount = 0;
      m_lastUpdate = time;
    }
  }


  HudPos HudCsThreadItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 1.0f, 0.25f, 1.0f },
      "CS chunks:");

    renderer.drawText(16.0f,
      { position.x + 132.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_csChunkString);

    position.y += 20.0f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 1.0f, 0.25f, 1.0f },
      "CS syncs:");

    renderer.drawText(16.0f,
      { position.x + 132.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_csSyncString);

    position.y += 8.0f;
    return position;
  }


  HudGpuLoadItem::HudGpuLoadItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudGpuLoadItem::~HudGpuLoadItem() {

  }


  void HudGpuLoadItem::update(dxvk::high_resolution_clock::time_point time) {
    uint64_t ticks = std::chrono::duration_cast<std::chrono::microseconds>(time - m_lastUpdate).count();

    if (ticks >= UpdateInterval) {
      DxvkStatCounters counters = m_device->getStatCounters();
      uint64_t currGpuIdleTicks = counters.getCtr(DxvkStatCounter::GpuIdleTicks);

      m_diffGpuIdleTicks = currGpuIdleTicks - m_prevGpuIdleTicks;
      m_prevGpuIdleTicks = currGpuIdleTicks;

      uint64_t busyTicks = ticks > m_diffGpuIdleTicks
        ? uint64_t(ticks - m_diffGpuIdleTicks)
        : uint64_t(0);

      m_gpuLoadString = str::format((100 * busyTicks) / ticks, "%");
      m_lastUpdate = time;
    }
  }


  HudPos HudGpuLoadItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 8.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 0.25f, 1.0f },
      "GPU:");

    renderer.drawText(16.0f,
      { position.x + 60.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_gpuLoadString);

    position.y += 16.0f;
    return position;
  }


  HudCompilerActivityItem::HudCompilerActivityItem(const Rc<DxvkDevice>& device)
  : m_device(device) {

  }


  HudCompilerActivityItem::~HudCompilerActivityItem() {

  }


  void HudCompilerActivityItem::update(dxvk::high_resolution_clock::time_point time) {
    DxvkStatCounters counters = m_device->getStatCounters();
    bool doShow = counters.getCtr(DxvkStatCounter::PipeCompilerBusy);

    if (!doShow) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time - m_timeShown);
      doShow = elapsed.count() <= MinShowDuration;
    }

    if (doShow && !m_show)
      m_timeShown = time;

    m_show = doShow;
  }


  HudPos HudCompilerActivityItem::render(
          HudRenderer&      renderer,
          HudPos            position) {
    if (m_show) {
      renderer.drawText(16.0f,
        { position.x, renderer.surfaceSize().height / renderer.scale() - 20.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "Compiling shaders...");
    }

    return position;
  }


  HudRtxActivityItem::HudRtxActivityItem(const Rc<DxvkDevice>& device)
    : m_device(device) {
  }

  HudRtxActivityItem::~HudRtxActivityItem() {
  }

  void HudRtxActivityItem::update(dxvk::high_resolution_clock::time_point time) {
  }

  HudPos HudRtxActivityItem::render(
    HudRenderer& renderer,
    HudPos       position) {
    const DxvkStatCounters counters = m_device->getStatCounters();

    const std::string labels[] = { "# Presents:" , 
                                   "# BLAS:" ,
                                   "# Buffers:" , 
                                   "# Textures:" , 
                                   "# Instances/Surfaces:" , 
                                   "# Surface Materials:" , 
                                   "# Surface Material Extensions:" ,
                                   "# Volume Materials:" , 
                                   "# Lights:",
                                   "# Samplers:",
                                   "# Textures in-flight:",
                                   "# Last tex. batch (ms):"}; 
    const uint64_t values[] = { counters.getCtr(DxvkStatCounter::QueuePresentCount),
                                counters.getCtr(DxvkStatCounter::RtxBlasCount),
                                counters.getCtr(DxvkStatCounter::RtxBufferCount),
                                counters.getCtr(DxvkStatCounter::RtxTextureCount),
                                counters.getCtr(DxvkStatCounter::RtxInstanceCount),
                                counters.getCtr(DxvkStatCounter::RtxSurfaceMaterialCount),
                                counters.getCtr(DxvkStatCounter::RtxSurfaceMaterialExtensionCount),
                                counters.getCtr(DxvkStatCounter::RtxVolumeMaterialCount),
                                counters.getCtr(DxvkStatCounter::RtxLightCount),
                                counters.getCtr(DxvkStatCounter::RtxSamplers),
                                counters.getCtr(DxvkStatCounter::RtxTexturesInFlight),
                                counters.getCtr(DxvkStatCounter::RtxLastTextureBatchDuration)};

    const uint32_t kNumLabels = sizeof(labels) / sizeof(labels[0]);
    static_assert(kNumLabels == sizeof(values) / sizeof(values[0]));

    position.y += 8.0f;

    const float xOffset = 16.f;
    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.25f, 0.5f, 0.25f, 1.0f },
      str::format("RTX:"));

    position.y += 16.0f;

    for (uint32_t i=0 ; i<kNumLabels; i++)  {
      renderer.drawText(14.0f,
        { position.x + xOffset, position.y },
        { 1.0f, 1.0f, 0.25f, 1.0f },
        labels[i]);

      std::string text = str::format(std::setfill(' '), std::setw(8), values[i]);

      renderer.drawText(14.0f,
        { position.x + xOffset + 250, position.y },
        { 1.0f, 1.0f, 1.f, 1.0f },
        text);

      position.y += 16.0f;
    }

    if (RtxOptions::Get()->getPresentThrottleDelay()) {
      position.y += 8.0f;

      renderer.drawText(16.0f,
                        { position.x, position.y },
                        { 1.0f, 0.2f, 0.2f, 1.0f },
                        "Present throttling enabled!");
      position.y += 16.0f;
    }

    if (RtxTextureManager::getShowProgress()) {
      constexpr size_t kNumTexPerLine = 64;
      int64_t numTexInFlight = counters.getCtr(DxvkStatCounter::RtxTexturesInFlight);

      std::string progress(std::min<size_t>(kNumTexPerLine, numTexInFlight), '*');

      while (numTexInFlight > 0) {
        if (progress.length() > numTexInFlight) {
          progress.resize(numTexInFlight);
        }

        position.y += 8.0f;
        renderer.drawText(16.0f,
                          { position.x, position.y },
                          { 0.0f, 1.0f, 0.0f, 1.0f },
                          progress);
        position.y += 16.0f;

        numTexInFlight -= kNumTexPerLine;
      }
    }

    return position;
  }

  HudPos HudScrollingLineItem::render(HudRenderer& renderer, HudPos position) {
    if (m_linePosition >= renderer.surfaceSize().width)
      m_linePosition = 0;

    const HudNormColor color = { 0xff, 0xff, 0x80, 0xff };

    HudLineVertex vertices[2] = {
      { { (float)m_linePosition, 0.f }, color },
      { { (float)m_linePosition, (float)renderer.surfaceSize().height }, color }
    };

    renderer.drawLines(2, vertices);

    ++m_linePosition;

    return position;
  }
}
