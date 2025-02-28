# Install script for directory: C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/sdk

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/FFX_API_")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/opticalflow/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/frameinterpolation/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/fsr3/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/fsr3upscaler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/fsr2/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/fsr1/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/spd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/cacao/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/lpm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/blur/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/vrs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/cas/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/dof/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/lens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/parallelsort/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/denoiser/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/sssr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/brixelizer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/brixelizergi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/classifier/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/src/components/breadcrumbs/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/lj--/OneDrive/Desktop/Dev/FidelityFX-SDK-v1.1.3/ffx-api/build/sdk/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
