#pragma once

#include <cassert>
#include <climits>
#include <type_traits>

#include "util_bit.h"

namespace dxvk {
  
  // Takes an enum or enum class type to use as a set of bits, the value of each enum member representing
  // the index of the bit they represent in the flags bitset.
  // Warning: All values in the enum/enum class which are intended to be used in setting/testing/etc
  // operations must have a value less than the number of bits in the underlying enum type. This is only
  // a problem when manually setting enum values, e.g. Foo = 0xFFFFFFFF will certianly cause an issue if
  // ever used (though an assertion will at least guard against this at runtime).
  template<typename T>
  class Flags {
    
  public:
    
    using IntType = std::underlying_type_t<T>;
    
    Flags() { }
    
    constexpr Flags(IntType t)
    : m_bits(t) { }
    
    template<typename... Tx>
    Flags(T f, Tx... fx) {
      this->set(f, fx...);
    }
    
    template<typename... Tx>
    void set(Tx... fx) {
      m_bits |= bits(fx...);
    }
    
    void set(Flags flags) {
      m_bits |= flags.m_bits;
    }
    
    template<typename... Tx>
    void clr(Tx... fx) {
      m_bits &= ~bits(fx...);
    }
    
    void clr(Flags flags) {
      m_bits &= ~flags.m_bits;
    }
    
    template<typename... Tx>
    bool any(Tx... fx) const {
      return (m_bits & bits(fx...)) != 0;
    }
    
    template<typename... Tx>
    bool all(Tx... fx) const {
      const IntType mask = bits(fx...);
      return (m_bits & mask) == mask;
    }
    
    bool test(T f) const {
      return this->any(f);
    }
    
    bool isClear() const {
      return m_bits == 0;
    }
    
    void clrAll() {
      m_bits = 0;
    }
    
    IntType raw() const {
      return m_bits;
    }
    
    Flags operator & (const Flags& other) const {
      return Flags(m_bits & other.m_bits);
    }
    
    Flags operator | (const Flags& other) const {
      return Flags(m_bits | other.m_bits);
    }
    
    Flags operator ^ (const Flags& other) const {
      return Flags(m_bits ^ other.m_bits);
    }

    bool operator == (const Flags& other) const {
      return m_bits == other.m_bits;
    }
    
    bool operator != (const Flags& other) const {
      return m_bits != other.m_bits;
    }
    
  private:
    
    IntType m_bits = 0;
    
    static IntType bit(T f) {
      // NV-DXVK start: Flags safety improvements
      // Note: This check exists to ensure that undefined behavior is not invoked when attempting to set a bit,
      // as left shifts greater or equal to the number of bits in the type is undefined behavior in C++ and
      // numerous bugs in the past due to invalid usage of the Flags class.
      // Do note though that this is a conservative check as C++ does not give an easy way to get the exact
      // number of bits in a type (numeric_limits<T>::digits has to be adjusted by sign). It will be at
      // least less than this value though (and in practice cursed 29 bit integers are not a thing anyways).
      assert(static_cast<IntType>(f) < (sizeof(IntType) * CHAR_BIT));
      // NV-DXVK end

      return IntType(1) << static_cast<IntType>(f);
    }
    
    template<typename... Tx>
    static IntType bits(T f, Tx... fx) {
      return bit(f) | bits(fx...);
    }
    
    static IntType bits() {
      return 0;
    }
    
  };
  
}

// Save assembly instructions for flag-related queries
#define FLAGS(name, T, ...) \
  struct name { \
    enum Flag : T { \
      __VA_ARGS__, \
      kNumFlags \
    }; \
    bool isClear() const { return val.load() == 0x0; } \
    void clear() { val.store(0x0); } \
    template<Flag flag, bool b> \
    void set() { \
      static_assert(flag < kNumFlags); \
      constexpr auto flagged = 1 << flag; \
      if constexpr (b) val.fetch_or(flagged); \
      else val.fetch_and(~flagged); \
    } \
    void set(const Flag flag, const bool b) { \
      assert(flag < kNumFlags); \
      const auto flagged = 1 << flag; \
      if (b) val.fetch_or(flagged); \
      else val.fetch_and(~flagged); \
    } \
    template<Flag flag> \
    bool has() const { \
      static_assert(flag < kNumFlags); \
      constexpr auto flagged = 1 << flag; \
      return val.load() & flagged; \
    } \
    bool has(const Flag flag) const { \
      assert(flag < kNumFlags); \
      const auto flagged = 1 << flag; \
      return val.load() & flagged; \
    } \
    void operator=(const name& other) { \
      return val.store(other.val.load()); \
    } \
    bool operator==(const name& other) const { \
      return val.load() == other.val.load(); \
    } \
    bool operator!=(const name& other) const { \
      return val.load() != other.val.load(); \
    } \
  private: \
    std::atomic<T> val = 0x0; \
  };