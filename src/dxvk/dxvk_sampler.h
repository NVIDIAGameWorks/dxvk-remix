#pragma once

#include "dxvk_resource.h"
// NV-DXVK start
#include "../util/xxHash/xxhash.h"
// NV-DXVK end

namespace dxvk {

  class DxvkDevice;
  
  /**
   * \brief Sampler properties
   */
  struct DxvkSamplerCreateInfo {
    /// Texture filter propertoes
    VkFilter magFilter;
    VkFilter minFilter;
    
    /// Mipmapping properties
    VkSamplerMipmapMode mipmapMode;
    float               mipmapLodBias;
    float               mipmapLodMin;
    float               mipmapLodMax;
    
    /// Anisotropic filtering
    VkBool32 useAnisotropy;
    float    maxAnisotropy;
    
    /// Address modes
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
    
    /// Compare op for shadow textures
    VkBool32    compareToDepth;
    VkCompareOp compareOp;
    
    /// Texture border color
    VkClearColorValue borderColor;      // sizeof() == 4 * 4B
    
    /// Enables unnormalized coordinates
    VkBool32 usePixelCoord;

    // NV-DXVK start
    XXH64_hash_t calculateHash() const {
      static_assert(sizeof(DxvkSamplerCreateInfo) == 72 && "DxvkSamplerCreateInfo changed. Double check the struct is still fully padded and initialized. This is needed for used hashing and comparison functions.");
     
      return XXH3_64bits(this, sizeof(DxvkSamplerCreateInfo));
    }

    bool operator== (const DxvkSamplerCreateInfo& other) const {
      return memcmp(this, &other, sizeof(*this)) == 0;
    }
    // NV-DXVK end
  };

  
  /**
   * \brief Sampler
   * 
   * Manages a sampler object that can be bound to
   * a pipeline. Sampler objects provide parameters
   * for texture lookups within a shader.
   */
  class DxvkSampler : public DxvkResource {
    
  public:
    
    DxvkSampler(
            DxvkDevice*             device,
      const DxvkSamplerCreateInfo&  info);
    ~DxvkSampler();
    
    /**
     * \brief Sampler handle
     * \returns Sampler handle
     */
    const VkSampler& handle() const {
      return m_sampler;
    }

    // NV-DXVK start
    /**
     * \brief Sampler info
     * \returns Sampler create info
     */
    const DxvkSamplerCreateInfo& info() const {
      return m_createInfo;
    }

    const XXH64_hash_t hash() const {
      return m_hash;
    }
    // NV-DXVK end
    
  private:
    
    Rc<vk::DeviceFn>      m_vkd;
    VkSampler             m_sampler = VK_NULL_HANDLE;
    // NV-DXVK start
    DxvkSamplerCreateInfo   m_createInfo;
    XXH64_hash_t m_hash;
    // NV-DXVK end

    static VkBorderColor getBorderColor(
      const Rc<DxvkDevice>&         device,
      const DxvkSamplerCreateInfo&  info);
    
  };
  
}
