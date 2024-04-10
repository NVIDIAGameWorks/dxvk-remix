#include "dxso_ctab.h"

namespace dxvk {

  DxsoCtab::DxsoCtab(DxsoReader& reader, uint32_t commentTokenCount) {
    const char* pStart = reader.currentPtr();

    m_size          = reader.readu32();

    if (m_size != ctabHeaderSize)
      throw DxvkError("DxsoCtab: ctab size invalid");

    m_creator       = reader.readu32();
    m_version       = reader.readu32();
    m_constants     = reader.readu32();
    m_constantInfo  = reader.readu32();
    m_flags         = reader.readu32();
    m_target        = reader.readu32();

    reader.skip(m_constantInfo - ctabHeaderSize);

    struct ConstantInfo {
      uint32_t Name;             // LPCSTR offset
      uint16_t RegisterSet;      // D3DXREGISTER_SET
      uint16_t RegisterIndex;    // register number
      uint16_t RegisterCount;    // number of registers
      uint16_t Reserved;         // reserved
      uint32_t TypeInfo;         // D3DXSHADER_TYPEINFO offset
      uint32_t DefaultValue;     // offset of default value
    };

    struct TypeInfo {
      uint16_t Class;            // D3DXPARAMETER_CLASS
      uint16_t Type;             // D3DXPARAMETER_TYPE
      uint16_t Rows;             // number of rows (matrices)
      uint16_t Columns;          // number of columns (vectors and matrices)
      uint16_t Elements;         // array dimension
      uint16_t StructMembers;    // number of struct members
      uint32_t StructMemberInfo; // D3DXSHADER_STRUCTMEMBERINFO[Members] offset
    };

    for (uint32_t j = 0; j < m_constants; j++) {
      ConstantInfo info;
      info.Name = reader.readu32();
      info.RegisterSet = reader.readu16();
      info.RegisterIndex = reader.readu16();
      info.RegisterCount = reader.readu16();
      info.Reserved = reader.readu16();
      info.TypeInfo = reader.readu32();
      info.DefaultValue = reader.readu32();

      Constant constant;
      constant.name = std::string(pStart + info.Name);
      constant.registerIndex = info.RegisterIndex;
      constant.registerCount = info.RegisterCount;

      m_constantData.push_back(constant);
    }
  }
}