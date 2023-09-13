#pragma once

#define ENUM_NAME(name) \
  case name: return os << #name

#define ENUM_DEFAULT(name) \
  default: return os << static_cast<int32_t>(e)

// Somewhere between a regular enum and an `enum class`. Namespacing
//   is not allowed within classes. If we want to make sure that enum
//   names must be scoped WITHOUT the pain caused by enum classes'
//   explicit casting, this is the alternative.
#define NS_Enum(name, ...) \
  struct name { \
    enum __##name { \
      __VA_ARGS__ \
    }; \
    template<typename T> \
    void operator=(const T _val) { val = _val; } \
    template<typename T> \
    bool operator==(const T _val) { return val == _val; } \
    template<typename T> \
    bool operator!=(const T _val) { return !(val == _val); } \
  private: \
    __##name val; \
  };