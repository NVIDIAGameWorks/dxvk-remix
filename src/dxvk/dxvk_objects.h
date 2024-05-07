#pragma once

#include "dxvk_gpu_event.h"
#include "dxvk_gpu_query.h"
#include "dxvk_memory.h"
#include "dxvk_meta_blit.h"
#include "dxvk_meta_clear.h"
#include "dxvk_meta_copy.h"
#include "dxvk_meta_mipgen.h"
#include "dxvk_meta_pack.h"
#include "dxvk_meta_resolve.h"
#include "dxvk_pipemanager.h"
#include "dxvk_renderpass.h"
#include "dxvk_unbound.h"
#include "rtx_render/rtx_volume_integrate.h"
#include "rtx_render/rtx_volume_filter.h"
#include "rtx_render/rtx_volume_preintegrate.h"
#include "rtx_render/rtx_pathtracer_gbuffer.h"
#include "rtx_render/rtx_rtxdi_rayquery.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "rtx_render/rtx_pathtracer_integrate_direct.h"
#include "rtx_render/rtx_pathtracer_integrate_indirect.h"
#include "rtx_render/rtx_demodulate.h"
#include "rtx_render/rtx_nee_cache.h"
#include "rtx_render/rtx_denoise.h"
#include "rtx_render/rtx_ngx_wrapper.h"
#include "rtx_render/rtx_dlfg.h"
#include "rtx_render/rtx_dlss.h"
#include "rtx_render/rtx_nis.h"
#include "rtx_render/rtx_taa.h"
#include "rtx_render/rtx_composite.h"
#include "rtx_render/rtx_debug_view.h"
#include "rtx_render/rtx_auto_exposure.h"
#include "rtx_render/rtx_tone_mapping.h"
#include "rtx_render/rtx_local_tone_mapping.h"
#include "rtx_render/rtx_bloom.h"
#include "rtx_render/rtx_geometry_utils.h"
#include "rtx_render/rtx_image_utils.h"
#include "rtx_render/rtx_postFx.h"
#include "rtx_render/rtx_initializer.h"
#include "rtx_render/rtx_scene_manager.h"
#include "rtx_render/rtx_ray_reconstruction.h"
#include "rtx_render/rtx_reflex.h"
#include "rtx_render/rtx_game_capturer.h"

#include "rtx_render/rtx_denoise_type.h"
#include "../util/util_lazy.h"
#include "../util/util_active.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkDenoise;
  class DxvkToneMapping;
  class DxvkBloom;
  class RtxGeometryUtils;
  class CompositePass;
  class DebugView;
  class DxvkPostFx;
  class OpacityMicromapManager;
  class ImGUI;
  class RtxTextureManager;

  class NGXContext;

  class DxvkObjects {

  public:

    explicit DxvkObjects(DxvkDevice* device);

    DxvkMemoryAllocator& memoryManager() {
      return m_memoryManager;
    }

    DxvkRenderPassPool& renderPassPool() {
      return m_renderPassPool;
    }

    DxvkPipelineManager& pipelineManager() {
      return m_pipelineManager;
    }

    DxvkGpuEventPool& eventPool() {
      return m_eventPool;
    }

    DxvkGpuQueryPool& queryPool() {
      return m_queryPool;
    }

    DxvkUnboundResources& dummyResources() {
      return m_dummyResources;
    }

    DxvkMetaBlitObjects& metaBlit() {
      return m_metaBlit.get(m_device);
    }

    DxvkMetaClearObjects& metaClear() {
      return m_metaClear.get(m_device);
    }

    DxvkMetaCopyObjects& metaCopy() {
      return m_metaCopy.get(m_device);
    }

    DxvkMetaResolveObjects& metaResolve() {
      return m_metaResolve.get(m_device);
    }
    
    DxvkMetaPackObjects& metaPack() {
      return m_metaPack.get(m_device);
    }

    DxvkVolumeIntegrate& metaVolumeIntegrate() {
      return m_volumeIntegrate.get();
    }

    DxvkVolumeFilter& metaVolumeFilter() {
      return m_volumeFilter.get();
    }

    DxvkVolumePreintegrate& metaVolumePreintegrate() {
      return m_volumePreintegrate.get();
    }

    DxvkPathtracerGbuffer& metaPathtracerGbuffer() {
      return m_pathtracerGbuffer.get();
    }

    DxvkRtxdiRayQuery& metaRtxdiRayQuery() {
      return m_rtxdiRayQuery.get();
    }

    DxvkReSTIRGIRayQuery& metaReSTIRGIRayQuery() {
      return m_restirgiRayQuery.get();
    }

    DxvkPathtracerIntegrateDirect& metaPathtracerIntegrateDirect() {
      return m_pathtracerIntegrateDirect.get();
    }

    DxvkPathtracerIntegrateIndirect& metaPathtracerIntegrateIndirect() {
      return m_pathtracerIntegrateIndirect.get();
    }

    DemodulatePass& metaDemodulate() {
      return m_demodulate.get();
    }

    NeeCachePass& metaNeeCache() {
      return m_neeCache.get();
    }

    DxvkDenoise& metaPrimaryDirectLightDenoiser() {
      return m_primaryDirectLightDenoiser.get();
    }

    DxvkDenoise& metaPrimaryIndirectLightDenoiser() {
      return m_primaryIndirectLightDenoiser.get();
    }

    DxvkDenoise& metaPrimaryCombinedLightDenoiser() {
      return m_primaryCombinedLightDenoiser.get();
    }

    DxvkDenoise& metaSecondaryCombinedLightDenoiser() {
      return m_secondaryCombinedLightDenoiser.get();
    }

    NGXContext& metaNGXContext() {
      return m_ngxContext.get();
    }

    DxvkDenoise& metaReferenceDenoiserSecondLobe0() {
      return m_referenceDenoiserSecondLobe0.get();
    }
    
    DxvkDenoise& metaReferenceDenoiserSecondLobe1() {
      return m_referenceDenoiserSecondLobe1.get();
    }

    DxvkDenoise& metaReferenceDenoiserSecondLobe2() {
      return m_referenceDenoiserSecondLobe2.get();
    }

    DxvkDLSS& metaDLSS() {
      return m_dlss.get();
    }

    DxvkRayReconstruction& metaRayReconstruction() {
      return m_rayReconstruction.get();
    }

    DxvkDLFG& metaDLFG() {
      return m_dlfg.get();
    }

    DxvkNIS& metaNIS() {
      return m_nis.get();
    }

    DxvkTemporalAA& metaTAA() {
      return m_taa.get();
    }

    CompositePass& metaComposite() {
      return m_composite.get();
    }

    DebugView& metaDebugView() {
      return m_debug_view.get();
    }

    DxvkAutoExposure& metaAutoExposure() {
      return m_autoExposure.get();
    }

    DxvkToneMapping& metaToneMapping() {
      return m_toneMapping.get();
    }

    DxvkLocalToneMapping& metaLocalToneMapping() {
      return m_localToneMapping.get();
    }

    DxvkBloom& metaBloom() {
      return m_bloom.get();
    }

    RtxGeometryUtils& metaGeometryUtils() {
      return m_geometryUtils.get();
    }

    RtxImageUtils& metaImageUtils() {
      return m_imageUtils.get();
    }

    DxvkPostFx& metaPostFx() {
      return m_postFx.get();
    }
    
    RtxReflex& metaReflex() {
      return m_reflex.get(m_device);
    }

    SceneManager& getSceneManager() {
      return m_sceneManager;
    }

    Resources& getResources() {
      return m_rtResources;
    }

    RtxInitializer& getRtxInitializer() {
      return m_rtInitializer;
    }

    RtxTextureManager& getTextureManager();
    
    ImGUI& getImgui() {
      return m_imgui;
    }

    const OpacityMicromapManager* getOpacityMicromapManager() {
      return m_sceneManager.getOpacityMicromapManager();
    }

    const TerrainBaker& getTerrainBaker() {
      return m_sceneManager.getTerrainBaker();
    }

    AssetExporter& metaExporter() {
      return m_exporter.get();
    }

    Rc<GameCapturer> capturer() {
      return m_capturer;
    }

    void onDestroy();

    void setWindowHandle(const HWND hwnd) {
      m_lastKnownWindowHandle.store(hwnd);
    }
    HWND getLastKnownWindowHandle() const {
      return m_lastKnownWindowHandle.load();
    }

  private:

    DxvkDevice*                       m_device;

    DxvkMemoryAllocator               m_memoryManager;
    DxvkRenderPassPool                m_renderPassPool;
    DxvkPipelineManager               m_pipelineManager;

    DxvkGpuEventPool                  m_eventPool;
    DxvkGpuQueryPool                  m_queryPool;

    DxvkUnboundResources              m_dummyResources;

    Lazy<DxvkMetaBlitObjects>         m_metaBlit;
    Lazy<DxvkMetaClearObjects>        m_metaClear;
    Lazy<DxvkMetaCopyObjects>         m_metaCopy;
    Lazy<DxvkMetaResolveObjects>      m_metaResolve;
    Lazy<DxvkMetaPackObjects>         m_metaPack;


    // Note: SceneManager(...) retrieves m_exporter from DxvkObjects(), so m_exporter has to be initialized prior to m_sceneManager
    Lazy<AssetExporter>               m_exporter;

    // RTX Management
    SceneManager       m_sceneManager;
    Resources          m_rtResources;
    RtxInitializer     m_rtInitializer;
    std::unique_ptr<RtxTextureManager> m_textureManager;
    ImGUI              m_imgui;
    Rc<GameCapturer>   m_capturer;


    // RTX Shaders
    Active<DxvkVolumeIntegrate>             m_volumeIntegrate;
    Active<DxvkVolumeFilter>                m_volumeFilter;
    Active<DxvkVolumePreintegrate>          m_volumePreintegrate;
    Active<DxvkPathtracerGbuffer>           m_pathtracerGbuffer;
    Active<DxvkRtxdiRayQuery>               m_rtxdiRayQuery;
    Active<DxvkReSTIRGIRayQuery>            m_restirgiRayQuery;
    Active<DxvkPathtracerIntegrateDirect>   m_pathtracerIntegrateDirect;
    Active<DxvkPathtracerIntegrateIndirect> m_pathtracerIntegrateIndirect;
    Active<DemodulatePass>                  m_demodulate;
    Active<NeeCachePass>                    m_neeCache;
    Active<DxvkDenoise>                     m_primaryDirectLightDenoiser;
    Active<DxvkDenoise>                     m_primaryIndirectLightDenoiser;
    Active<DxvkDenoise>                     m_primaryCombinedLightDenoiser;
    Active<DxvkDenoise>                     m_secondaryCombinedLightDenoiser;
    Active<NGXContext>                      m_ngxContext;
    Active<DxvkDLFG>                        m_dlfg;
    // Secondary reference denoisers used for a second lobe when non-combined signal reference denoising is enabled
    Active<DxvkDenoise>                     m_referenceDenoiserSecondLobe0;
    Active<DxvkDenoise>                     m_referenceDenoiserSecondLobe1;
    Active<DxvkDenoise>                     m_referenceDenoiserSecondLobe2;
    Active<DxvkDLSS>                        m_dlss;
    Active<DxvkRayReconstruction>           m_rayReconstruction;
    Active<DxvkNIS>                         m_nis;
    Active<DxvkTemporalAA>                  m_taa;
    Active<CompositePass>                   m_composite;
    Active<DebugView>                       m_debug_view;
    Active<DxvkAutoExposure>                m_autoExposure;
    Active<DxvkToneMapping>                 m_toneMapping;
    Active<DxvkLocalToneMapping>            m_localToneMapping;
    Active<DxvkBloom>                       m_bloom;
    Active<RtxGeometryUtils>                m_geometryUtils;
    Active<RtxImageUtils>                   m_imageUtils;
    Active<DxvkPostFx>                      m_postFx;
    Lazy<RtxReflex>                         m_reflex;

    std::atomic<HWND>                       m_lastKnownWindowHandle;
  };
}
