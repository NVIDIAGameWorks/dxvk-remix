#pragma once

#include <vector>
#include <d3d9types.h>
#include "d3d9_include.h"

namespace dxvk {
  struct DxvkVertexInputState;
  class DxvkBuffer;


  /**
    * \brief: Gets the min and max bone indices referenced in a vertex buffer
    *
    * \param [in] indexPtr: Base pointer for the bone indices vertex region
    * \param [in] stride: Stride of the vertex buffer
    * \param [in] vertexCount: Number of vertices
    * \param [in] numBonesPerVertex: Number of bones per vertex
    * \param [out] minBoneIndex: Minimum referenced bone index
    * \param [out] maxBoneIndex: Maximum referenced bone index
    *
    * \returns: False if unable to determine the min/max
    */
  bool getMinMaxBoneIndices(const uint8_t* indexPtr, uint32_t stride, uint32_t vertexCount, uint32_t numBonesPerVertex, int& minBoneIndex, int& maxBoneIndex);

  /**
    * \brief: Determines of a render target can be considered primary.
    *
    * \param [in] presenterParams: Current present params
    * \param [in] renderTargetDesc: Descriptor of desired render target
    *
    * \returns: True if render target is primary, false otherwise
    */
  bool isRenderTargetPrimary(const D3DPRESENT_PARAMETERS& presenterParams, const D3D9_COMMON_TEXTURE_DESC* renderTargetDesc);

  /**
    * \brief: This function creates a legacy state object by taking the Direct3D 9 state information
    *         from the input device.
    * 
    * \param [in] pDevice: parent D3D9 device
    * 
    * \returns: desired legacy state object
    */
  DxvkRtxLegacyState createLegacyState(D3D9DeviceEx* pDevice);

  /**
    * \brief: This function creates a legacy state object by taking the Direct3D 9 state information
    *         from the input device.
    * 
    * \param [in] d3d9State: state object from the parent D3D9 device
    * \param [in] stageIdx: desired texture stage (0~7)
    * 
    * \returns: desired texture stage state object
    */
  DxvkRtxTextureStageState createTextureStageState(const Direct3DState9& d3d9State, const uint32_t stageIdx);
}
