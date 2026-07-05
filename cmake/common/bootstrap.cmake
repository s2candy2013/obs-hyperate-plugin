# OBS plugin-template-style bootstrap for this repository.

include_guard(GLOBAL)

if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
      "In-source builds are not supported. Use a separate build directory or a CMake preset."
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

string(JSON _name GET "${buildspec}" name)
string(JSON _displayName GET "${buildspec}" displayName)
string(JSON _website GET "${buildspec}" website)
string(JSON _author GET "${buildspec}" author)
string(JSON _email GET "${buildspec}" email)
string(JSON _version GET "${buildspec}" version)
string(JSON _bundleId GET "${buildspec}" platformConfig macos bundleId)

set(PLUGIN_DISPLAY_NAME "${_displayName}")
set(PLUGIN_AUTHOR "${_author}")
set(PLUGIN_WEBSITE "${_website}")
set(PLUGIN_EMAIL "${_email}")
set(PLUGIN_VERSION "${_version}")
set(MACOS_BUNDLEID "${_bundleId}")

string(REPLACE "." ";" _version_canonical "${_version}")
list(GET _version_canonical 0 PLUGIN_VERSION_MAJOR)
list(GET _version_canonical 1 PLUGIN_VERSION_MINOR)
list(GET _version_canonical 2 PLUGIN_VERSION_PATCH)
unset(_version_canonical)

if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel)
  endif()
endif()

set(CMAKE_EXPORT_PACKAGE_REGISTRY FALSE)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
