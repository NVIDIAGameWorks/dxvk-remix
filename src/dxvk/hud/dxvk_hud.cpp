#include <cstring>

#include "dxvk_hud.h"

namespace dxvk::hud {
  
  Hud::Hud(
    const Rc<DxvkDevice>& device,
    const HudConfig&      config)
  : m_config        (config),
    m_device        (device),
    m_uniformBuffer (createUniformBuffer()),
    m_renderer      (device),
    m_hudDeviceInfo (device),
    m_hudFramerate  (config.elements),
    m_hudStats      (config.elements) {
    // Set up constant state
    m_rsState.polygonMode       = VK_POLYGON_MODE_FILL;
    m_rsState.cullMode          = VK_CULL_MODE_BACK_BIT;
    m_rsState.frontFace         = VK_FRONT_FACE_CLOCKWISE;
    m_rsState.depthClipEnable   = VK_FALSE;
    m_rsState.depthBiasEnable   = VK_FALSE;
    m_rsState.sampleCount       = VK_SAMPLE_COUNT_1_BIT;

    m_blendMode.enableBlending  = VK_TRUE;
    m_blendMode.colorSrcFactor  = VK_BLEND_FACTOR_ONE;
    m_blendMode.colorDstFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_blendMode.colorBlendOp    = VK_BLEND_OP_ADD;
    m_blendMode.alphaSrcFactor  = VK_BLEND_FACTOR_ONE;
    m_blendMode.alphaDstFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_blendMode.alphaBlendOp    = VK_BLEND_OP_ADD;
    m_blendMode.writeMask       = VK_COLOR_COMPONENT_R_BIT
                                | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT;

    addItem<HudVersionItem>("version");
    addItem<HudClientApiItem>("api", m_device);
    addItem<HudDeviceInfoItem>("devinfo", m_device);
    addItem<HudFpsItem>("fps");
    addItem<HudFrameTimeItem>("frametimes");
  }
  
  
  Hud::~Hud() {
    
  }
  
  
  void Hud::update() {
    m_hudItems.update();
    m_hudFramerate.update();
    m_hudStats.update(m_device);
  }
  
  
  void Hud::render(const Rc<DxvkContext>& ctx, VkExtent2D surfaceSize) {
    m_uniformData.surfaceSize = surfaceSize;
    
    this->updateUniformBuffer(ctx, m_uniformData);

    this->setupRendererState(ctx);
    this->renderHudElements(ctx);
  }
  
  
  Rc<Hud> Hud::createHud(const Rc<DxvkDevice>& device) {
    std::string hudElements = env::getEnvVar("DXVK_HUD");

    if (hudElements.empty())
      hudElements = device->config().hud;

    HudConfig config(hudElements);

    return !config.elements.isClear()
      ? new Hud(device, config)
      : nullptr;
  }


  void Hud::setupRendererState(const Rc<DxvkContext>& ctx) {
    ctx->setRasterizerState(m_rsState);
    ctx->setBlendMode(0, m_blendMode);

    ctx->bindResourceBuffer(0,
      DxvkBufferSlice(m_uniformBuffer));

    m_renderer.beginFrame(ctx, m_uniformData.surfaceSize);
  }


  void Hud::renderHudElements(const Rc<DxvkContext>& ctx) {
    m_hudItems.render(m_renderer);

    HudPos position = { 8.0f, 24.0f };

    position = m_hudFramerate.render(m_renderer, position);
    position = m_hudStats    .render(m_renderer, position);
  }
  
  
  void Hud::updateUniformBuffer(const Rc<DxvkContext>& ctx, const HudUniformData& data) {
    auto slice = m_uniformBuffer->allocSlice();
    std::memcpy(slice.mapPtr, &data, sizeof(data));

    ctx->invalidateBuffer(m_uniformBuffer, slice);
  }


  Rc<DxvkBuffer> Hud::createUniformBuffer() {
    DxvkBufferCreateInfo info;
    info.size           = sizeof(HudUniformData);
    info.usage          = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.stages         = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                        | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    info.access         = VK_ACCESS_UNIFORM_READ_BIT;
    
    return m_device->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
  
}
