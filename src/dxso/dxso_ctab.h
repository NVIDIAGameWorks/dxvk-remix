#pragma once

#include "dxso_common.h"

#include "dxso_reader.h"

namespace dxvk {

  /**
    * \brief DXSO CTAB
    *
    * Stores meta information about the shader constant table
    */
  class DxsoCtab {

  public:
    DxsoCtab() = default;

    DxsoCtab(DxsoReader& reader, uint32_t commentTokenCount);

    inline static const uint32_t ctabHeaderSize = 0x1c;

    uint32_t m_size = 0;
    uint32_t m_creator;
    uint32_t m_version;
    uint32_t m_constants;
    uint32_t m_constantInfo;
    uint32_t m_flags;
    uint32_t m_target;

    struct Constant {
      std::string name;
      uint32_t registerIndex;
      uint32_t registerCount;
    };

    std::vector<Constant> m_constantData;
  };

}