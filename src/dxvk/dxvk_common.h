class DxvkCommon {
public:
  DxvkDLSS& metaDLSS() { return m_dlss; }
  DxvkFSR& metaFSR() { return m_fsr; }

private:
  DxvkDLSS m_dlss;
  DxvkFSR m_fsr;
}; 