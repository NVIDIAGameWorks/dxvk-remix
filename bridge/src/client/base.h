/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include <d3d9.h>

#include "pch.h"
#include "log/log.h"

#include <unknwn.h>
#include <sstream>
#include <assert.h>
#include <atomic>
#include <functional>

using ShadowMap = std::unordered_map<uintptr_t, IUnknown*>;
extern ShadowMap gShadowMap;

extern std::mutex gShadowMapMutex;

enum class D3D9ObjectType: char {
  Module,
  Device,
  StateBlock,
  SwapChain,
  Resource,
  VertexDeclaration,
  VertexShader,
  PixelShader,
  BaseTexture,
  Texture,
  VolumeTexture,
  CubeTexture,
  VertexBuffer,
  IndexBuffer,
  Surface,
  Volume,
  Query,
  Invalid
};

template<typename T>
static constexpr D3D9ObjectType toD3D9ObjectType() {
  if (__uuidof(T) == __uuidof(IDirect3D9) || __uuidof(T) == __uuidof(IDirect3D9Ex))
    return D3D9ObjectType::Module;

  if (__uuidof(T) == __uuidof(IDirect3DDevice9) || __uuidof(T) == __uuidof(IDirect3DDevice9Ex))
    return D3D9ObjectType::Device;

  if (__uuidof(T) == __uuidof(IDirect3DStateBlock9))
    return D3D9ObjectType::StateBlock;

  if (__uuidof(T) == __uuidof(IDirect3DSwapChain9))
    return D3D9ObjectType::SwapChain;

  if (__uuidof(T) == __uuidof(IDirect3DResource9))
    return D3D9ObjectType::Resource;

  if (__uuidof(T) == __uuidof(IDirect3DVertexDeclaration9))
    return D3D9ObjectType::VertexDeclaration;

  if (__uuidof(T) == __uuidof(IDirect3DVertexShader9))
    return D3D9ObjectType::VertexShader;

  if (__uuidof(T) == __uuidof(IDirect3DPixelShader9))
    return D3D9ObjectType::PixelShader;

  if (__uuidof(T) == __uuidof(IDirect3DBaseTexture9))
    return D3D9ObjectType::BaseTexture;

  if (__uuidof(T) == __uuidof(IDirect3DTexture9))
    return D3D9ObjectType::Texture;

  if (__uuidof(T) == __uuidof(IDirect3DVolumeTexture9))
    return D3D9ObjectType::VolumeTexture;

  if (__uuidof(T) == __uuidof(IDirect3DCubeTexture9))
    return D3D9ObjectType::CubeTexture;

  if (__uuidof(T) == __uuidof(IDirect3DVertexBuffer9))
    return D3D9ObjectType::VertexBuffer;

  if (__uuidof(T) == __uuidof(IDirect3DIndexBuffer9))
    return D3D9ObjectType::IndexBuffer;

  if (__uuidof(T) == __uuidof(IDirect3DSurface9))
    return D3D9ObjectType::Surface;

  if (__uuidof(T) == __uuidof(IDirect3DVolume9))
    return D3D9ObjectType::Volume;

  if (__uuidof(T) == __uuidof(IDirect3DQuery9))
    return D3D9ObjectType::Query;

  Logger::warn("Missing D3D9 type");
  return D3D9ObjectType::Invalid;
}

template<typename T>
static constexpr const char* toD3D9ObjectTypeName() {
  static constexpr const char* g_typeNames[] = {
    "Module", "Device", "StateBlock", "SwapChain", "Resource",
    "VertexDeclaration", "VertexShader", "PixelShader", "BaseTexture",
    "Texture", "VolumeTexture", "CubeTexture", "VertexBuffer",
    "IndexBuffer", "Surface", "Volume", "Query", "Invalid"
  };

  return g_typeNames[static_cast<uint32_t>(toD3D9ObjectType<T>())];
}

// A special type of refcount object catered towards D3D object lifecycle
// emulation. The class is not templated in order to track the underlying
// templated D3D object lifecycle without knowing the implementation. The
// refcount is NOT intrusive and so the refcount storage must be provided by
// the derived client class using a reference. The D3D9 refcounts may be shared
// across multiple objects to emulate D3D9 container/children relationship.
//
// The D3DRefCounted object has two actual refcounts fused into one:
//
//   1) Interface refcount. The public refcount to be used in
//      IUnknown::AddRef() and IUnknown::Release() methods.
//   2) Object refcount. The actual object refcount that
//      includes all external object interface references and
//      all internal references to the object.
//
// Rules:
//
// When object's COM interface is referenced using the IUnknown methods
// both Object and Interface refcounts are adjusted, and the Interface
// refcount is returned to the client. When the object is used internally
// only the Object refcount is adjusted.
// The object (and so its interface) is considered alive as long as its
// Object refcount is not 0. Public Interface refcount is allowed to be zero.
//
class D3DRefCounted {
  // Using a 64-bit storage type by default to fully cover IUnknown's ULONG.
  // If we ever believe that a 16-bit external refcount is enough we may
  // switch to a 32-bit storage type for speed.
  typedef uint64_t FusedStorageType;
  static constexpr size_t StorageBitwidth = sizeof(FusedStorageType) * 8;
  static constexpr size_t RefBitwidth = StorageBitwidth / 2;

protected:
  typedef std::atomic<FusedStorageType> RefCountType;
  typedef std::function<void()> DeleterType;

  D3DRefCounted(RefCountType& refCount, DeleterType&& deleter)
    : m_fusedRefCnt{refCount}
    , m_deleter(std::move(deleter)) {
  }

  virtual ~D3DRefCounted() {
  }

  enum class Ref: uint32_t {
    Object    = 0,
    Interface = 1,
    Both      = Interface
  };

  template<Ref type>
  inline ULONG getRef() const {
    return toRefValue<type>(m_fusedRefCnt.load());
  }

  template<Ref type>
  inline ULONG incRef(ULONG adj = 1u) {
    return toRefValue<type>(m_fusedRefCnt += toAdjValue<type>(adj));
  }

  template<Ref type>
  inline ULONG decRef(ULONG adj = 1u) {
    const auto fusedCnt = m_fusedRefCnt -= toAdjValue<type>(adj);

    if (ULONG cnt = toRefValue<type>(fusedCnt)) {
      return cnt;
    }

    if (toRefValue<Ref::Object>(fusedCnt) == 0) {
      // Object refcount is zero - destroy the object.
      m_deleter();
    }

    return 0;
  }

  inline void adjustRefs(int32_t ref) {
    if (ref > 0) {
      incRef<Ref::Both>(ref);
    } else {
      decRef<Ref::Both>(-ref);
    }
  }

private:
  // Extracts the actual refcount value from fused refcount.
  template<Ref type>
  static constexpr ULONG toRefValue(FusedStorageType cnt) {
    return static_cast<ULONG>(cnt >> (static_cast<uint32_t>(type) * RefBitwidth));
  }

  // Creates the adjustment value for fused refcount depending on 
  // the incoming refcount type. The rules are:
  //   1) adjust only the Object part for Object refcount.
  //   2) adjust both Object and Interface parts for Interface refcount.
  template<Ref type>
  static constexpr FusedStorageType toAdjValue(ULONG v) {
    if (type == Ref::Both) {
      return v | (static_cast<FusedStorageType>(v) << RefBitwidth);
    }
    return v;
  }

  RefCountType& m_fusedRefCnt;
  DeleterType m_deleter;

  friend class D3DAutoPtr;
};

// A smart-pointer object that operates on D3DRefCounted objects.
class D3DAutoPtr {
  D3DRefCounted* m_obj = nullptr;
public:
  D3DAutoPtr() = default;

  D3DAutoPtr(std::nullptr_t) {
    reset(nullptr);
  }

  D3DAutoPtr(D3DAutoPtr& b) {
    reset(b.m_obj);
  }

  D3DAutoPtr(D3DAutoPtr&& b) noexcept {
    reset(nullptr);
    std::swap(m_obj, b.m_obj);
  }

  explicit D3DAutoPtr(D3DRefCounted* obj) {
    reset(obj);
  }

  ~D3DAutoPtr() {
    if (m_obj) {
      m_obj->decRef<D3DRefCounted::Ref::Object>();
    }
  }

  D3DRefCounted* operator *() const {
    return m_obj;
  }

  D3DAutoPtr& operator = (const D3DAutoPtr& b) {
    if (this != &b) {
      reset(b.m_obj);
    }

    return *this;
  }

  D3DAutoPtr& operator = (D3DAutoPtr&& b) noexcept {
    reset(nullptr);
    std::swap(m_obj, b.m_obj);
    return *this;
  }

  void reset(D3DRefCounted* obj) {
    if (m_obj) {
      m_obj->decRef<D3DRefCounted::Ref::Object>();
    }

    if (obj) {
      obj->incRef<D3DRefCounted::Ref::Object>();
    }

    m_obj = obj;
  }
};

// Helper for making an D3DAutoPtr from a D3DRefCounted child.
template<typename T>
static inline D3DAutoPtr MakeD3DAutoPtr(T* obj) {
  return D3DAutoPtr(static_cast<D3DRefCounted*>(obj));
}

class D3dBaseIdFactory {
private:
  static std::atomic<uintptr_t> id_counter;
public:
  static uintptr_t getNextId();
};

// The base object for every D3D object. Implements IUnknown::AddRef() and
// IUnknown::Release() methods for proper object and interface lifecycle
// tracking. May hold a reference to the parent object and may adjust its
// refcount appropriately. Provides refcount storage for non-intrusive
// D3DRefCounted class. The refcount storage is hidden using private
// inheritance.
template<typename T>
class D3DBase: public T, public D3DRefCounted, private D3DRefCounted::RefCountType {
  template<typename T, typename C>
  friend class Direct3DContainer9_LSS;

  IUnknown* m_pParent;
  const D3D9ObjectType m_type;
  const bool m_standalone;
  const size_t m_id;

  void onConstruct() {
    AddRef();
#ifdef _DEBUG
    Logger::debug(format_string("%s object [%p/%p] created",
                                toD3D9ObjectTypeName<T>(), this, getId()));
#endif
  }

  // Called when the object is about to be destroyed unconditionally.
  // Must not be used from outside of the lifecycle tracking
  // mechanism thus this method is private.
  // TODO(refactoring): move this functionality to overriden destructors
  // of the derived classes and remove destroy() method.
  virtual void onDestroy() = 0;

  void destroy() {
    onDestroy();
    delete this;
  }

protected:
  typedef T BaseD3DType;

  // Constructor for all standalone D3D objects.
  // Initializes the refcount with the reference to this object's refcount
  // and deleter functor.
  D3DBase(T* pD3DObject, IUnknown* pParent)
    : D3DRefCounted(*static_cast<RefCountType*>(this),
                    DeleterType([this]() { destroy(); }))
    , m_pParent(pParent)
    , m_type(toD3D9ObjectType<T>())
    , m_standalone(true)
    , m_id(D3dBaseIdFactory::getNextId()) {
    onConstruct();
    }

  // Constructor for non-standalone D3D objects.
  // Initializes the refcount with the reference to container's refcount
  // and deleter functor.
  template<typename ContainerType>
  D3DBase(T* pD3DObject, IUnknown* const pDevice, ContainerType* pContainer)
    : D3DRefCounted(*pContainer)
    , m_pParent(pContainer)
    , m_type(toD3D9ObjectType<T>())
    , m_standalone(false)
    , m_id(D3dBaseIdFactory::getNextId()) {
    onConstruct();
  }

  ~D3DBase() override {
    gShadowMapMutex.lock();
    gShadowMap.erase(m_id);
    gShadowMapMutex.unlock();
#ifdef _DEBUG
    Logger::debug(format_string("%s object [%p/%p] destroyed",
                                toD3D9ObjectTypeName<T>(), this, m_id));
#endif
  }

public:

  D3D9ObjectType getType() {
    return m_type;
  }

  template<typename U>
  U* D3D() const {
    return static_cast<U*>((void*) m_id);
  }

  size_t getId() const {
    return m_id;
  }

  IUnknown* getParent() const {
    return m_pParent;
  }

  bool isStandalone() const {
    return m_standalone;
  }

  STDMETHOD_(ULONG, AddRef)(THIS) {
    // Non-standalone objects use parent's container method.
    if (!isStandalone()) {
      return m_pParent->AddRef();
    }

    const ULONG cnt = incRef<D3DRefCounted::Ref::Interface>();
    // Reviving the object from the "dead": when the object is still
    // alive but its Interface refcount is 0 we need to increase its
    // parent refcount when adding a reference to the object.
    if (cnt == 1 && m_pParent) {
      m_pParent->AddRef();
    }
    return cnt;
  }

  STDMETHOD_(ULONG, Release)(THIS) {
    // Non-standalone objects use parent's container method.
    if (!isStandalone()) {
      return m_pParent->Release();
    }

    if (ULONG cnt = getRef<D3DRefCounted::Ref::Interface>()) {
      IUnknown* pParent = m_pParent;

      cnt = decRef<D3DRefCounted::Ref::Interface>();

      if (cnt == 0 && pParent) {
        // Dereference parent
        pParent->Release();
      }

      return cnt;
    }

    return 0;
  }
};