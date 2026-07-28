cmake_minimum_required(VERSION 3.5)

project(AnybloxExtension)

include_directories(include)

# The AnyBlox C++ runtime (anyblox-cpplib from the AnyBlox/anyblox repo) is
# optional at configure time: point CMAKE_PREFIX_PATH at its install prefix to
# build the real scan, otherwise a stub is compiled so that the extension still
# builds and its registration/settings can be tested.
find_package(AnyBloxCpp 0.10.0 QUIET)

if(AnyBloxCpp_FOUND)
  message(
    STATUS
      "Found AnyBlox CPP library '${AnyBloxCpp_VERSION}' - building AnyBlox scan"
  )
  set(ANYBLOX_SCAN_FILE anyblox_scan.cpp)
else()
  message(
    STATUS
      "AnyBloxCpp not found - building anyblox extension with a stub scan. Set CMAKE_PREFIX_PATH to the anyblox-cpplib install prefix to enable reading bundles."
  )
  set(ANYBLOX_SCAN_FILE anyblox_scan_stub.cpp)
endif()

set(ANYBLOX_EXTENSION_FILES anyblox_extension.cpp ${ANYBLOX_SCAN_FILE})

build_static_extension(anyblox ${ANYBLOX_EXTENSION_FILES})
set(PARAMETERS "-warnings")
build_loadable_extension(anyblox ${PARAMETERS} ${ANYBLOX_EXTENSION_FILES})

if(AnyBloxCpp_FOUND)
  target_link_libraries(anyblox_extension AnyBlox::AnyBloxCpp)
  target_link_libraries(anyblox_loadable_extension AnyBlox::AnyBloxCpp)

  # AnyBloxCpp installs its headers into <prefix>/include/anyblox, but the
  # sources include them as <anyblox/anyblox.hpp>. Also add the parent of each
  # interface include dir so both spellings resolve.
  get_target_property(_anyblox_inc AnyBlox::AnyBloxCpp
                      INTERFACE_INCLUDE_DIRECTORIES)
  foreach(_dir ${_anyblox_inc})
    if(EXISTS "${_dir}")
      get_filename_component(_parent "${_dir}" DIRECTORY)
      target_include_directories(anyblox_extension PRIVATE "${_parent}")
      target_include_directories(anyblox_loadable_extension
                                 PRIVATE "${_parent}")
    endif()
  endforeach()
endif()

install(
  TARGETS anyblox_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
