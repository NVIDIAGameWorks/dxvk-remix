#pragma once

#include <string>

namespace dxvk {
  
  /**
   * \brief DXVK error
   * 
   * A generic exception class that stores a
   * message. Exceptions should be logged.
   */
  class DxvkError {
    
  public:
    
    DxvkError() { }
    DxvkError(std::string&& message)
    : m_message(std::move(message)) { }
    
    const std::string& message() const {
      return m_message;
    }
    
  private:
    
    std::string m_message;
    
  };

  // NV-DXVK start: Provide error code on exception
  class DxvkErrorWithId : public DxvkError {
  public:
    DxvkErrorWithId(int id, std::string&& message)
      : DxvkError { std::move(message) }
      , m_id { id } {}
    ~DxvkErrorWithId() = default;

    DxvkErrorWithId(DxvkErrorWithId&&) = delete;
    DxvkErrorWithId& operator=(DxvkErrorWithId&&) = delete;
    DxvkErrorWithId(const DxvkErrorWithId&) = delete;
    DxvkErrorWithId& operator=(const DxvkErrorWithId&&) = delete;

    int id() const {
      return m_id;
    }

  private:
    int m_id {};
  };
  // NV-DXVK end
  
}