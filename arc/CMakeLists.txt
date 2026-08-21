cmake_minimum_required(VERSION 3.30)

project(arc VERSION 0.1.0 LANGUAGES C CXX)

option(ARC_BUILD_TESTS "Build Arc protocol/core tests" ON)
option(ARC_BUILD_PLATFORM_TARGETS "Build all platform conformance targets" ON)
option(ARC_BUILD_EXTERNAL_CONSUMER_TEST "Build an installed-package consumer smoke test" ON)
option(ARC_WARNINGS_AS_ERRORS "Treat Arc warnings as errors" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(arc_protocol INTERFACE)
add_library(Arc::Protocol ALIAS arc_protocol)
set_target_properties(arc_protocol PROPERTIES EXPORT_NAME Protocol)
target_include_directories(arc_protocol INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")

add_library(arc_core STATIC
  src/bridge.cpp
  src/host_adapter.cpp)
add_library(Arc::Core ALIAS arc_core)
set_target_properties(arc_core PROPERTIES EXPORT_NAME Core)
target_include_directories(arc_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_link_libraries(arc_core PUBLIC Arc::Protocol)
set_target_properties(arc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(arc_core PRIVATE /W4 /permissive- /EHsc)
  if(ARC_WARNINGS_AS_ERRORS)
    target_compile_options(arc_core PRIVATE /WX)
  endif()
else()
  target_compile_options(arc_core PRIVATE -Wall -Wextra -Wpedantic)
  if(ARC_WARNINGS_AS_ERRORS)
    target_compile_options(arc_core PRIVATE -Werror)
  endif()
endif()

function(arc_add_platform_target name source)
  add_library(arc_${name} STATIC "${source}")
  add_library(Arc::${name} ALIAS arc_${name})
  set_target_properties(arc_${name} PROPERTIES EXPORT_NAME ${name})
  target_include_directories(arc_${name} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
  target_link_libraries(arc_${name} PUBLIC Arc::Core)
  set_target_properties(arc_${name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(arc_${name} PRIVATE /W4 /permissive- /EHsc)
    if(ARC_WARNINGS_AS_ERRORS)
      target_compile_options(arc_${name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(arc_${name} PRIVATE -Wall -Wextra -Wpedantic)
    if(ARC_WARNINGS_AS_ERRORS)
      target_compile_options(arc_${name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

set(ARC_APPLE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/apple_backend.cpp")
if(APPLE)
  enable_language(OBJCXX)
  set(ARC_APPLE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/apple_backend.mm")
endif()

if(ARC_BUILD_PLATFORM_TARGETS)
  arc_add_platform_target(Web
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/web_backend.cpp")
  arc_add_platform_target(Windows
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/windows_backend.cpp")
  arc_add_platform_target(Android
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/android_backend.cpp")
  arc_add_platform_target(macOS "${ARC_APPLE_SOURCE}")
  target_compile_definitions(arc_macOS PRIVATE
    ARC_APPLE_PLATFORM_KIND=ARC_PLATFORM_MACOS
    ARC_CREATE_BACKEND=CreateMacOSBackend
    ARC_CREATE_INPUT_SOURCE=CreateMacOSInputSource)
  arc_add_platform_target(iOS "${ARC_APPLE_SOURCE}")
  target_compile_definitions(arc_iOS PRIVATE
    ARC_APPLE_PLATFORM_KIND=ARC_PLATFORM_IOS
    ARC_CREATE_BACKEND=CreateIOSBackend
    ARC_CREATE_INPUT_SOURCE=CreateIOSInputSource)
  arc_add_platform_target(iPadOS "${ARC_APPLE_SOURCE}")
  target_compile_definitions(arc_iPadOS PRIVATE
    ARC_APPLE_PLATFORM_KIND=ARC_PLATFORM_IPADOS
    ARC_CREATE_BACKEND=CreateIPadOSBackend
    ARC_CREATE_INPUT_SOURCE=CreateIPadOSInputSource)
  arc_add_platform_target(Headless
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/headless/headless_backend.cpp")
  # ChromiumOS consumes the Web implementation; the distinct target makes
  # the reuse contract visible to the dependency graph and CI.
  arc_add_platform_target(ChromiumOS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/chromiumos_backend.cpp")
  # Device is deliberately a conformance stub until a BSP/permission target
  # is available. It is never linked into ordinary application targets.
  arc_add_platform_target(Device
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/device/device_backend.cpp")
endif()

include(CTest)
if(ARC_BUILD_TESTS AND BUILD_TESTING)
  add_executable(arc_core_tests tests/arc_core_test.cpp)
  target_link_libraries(arc_core_tests PRIVATE Arc::Core)
  if(NOT MSVC)
    target_compile_options(arc_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(ARC_WARNINGS_AS_ERRORS)
      target_compile_options(arc_core_tests PRIVATE -Werror)
    endif()
  endif()
  add_test(NAME arc_core_tests COMMAND arc_core_tests)
  add_executable(arc_host_adapter_tests tests/host_adapter_test.cpp)
  target_link_libraries(arc_host_adapter_tests PRIVATE Arc::Core)
  if(NOT MSVC)
    target_compile_options(arc_host_adapter_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(ARC_WARNINGS_AS_ERRORS)
      target_compile_options(arc_host_adapter_tests PRIVATE -Werror)
    endif()
  endif()
  add_test(NAME arc_host_adapter_tests COMMAND arc_host_adapter_tests)
  add_executable(arc_protocol_c_smoke tests/protocol_c_smoke.c)
  target_link_libraries(arc_protocol_c_smoke PRIVATE Arc::Protocol)
  add_test(NAME arc_protocol_c_smoke COMMAND arc_protocol_c_smoke)
  if(ARC_BUILD_PLATFORM_TARGETS)
    add_executable(arc_platform_targets_tests tests/platform_targets_test.cpp)
    target_link_libraries(arc_platform_targets_tests PRIVATE
      Arc::Web Arc::Windows Arc::Android Arc::macOS Arc::iOS Arc::iPadOS
      Arc::ChromiumOS Arc::Headless Arc::Device)
    add_test(NAME arc_platform_targets_tests COMMAND arc_platform_targets_tests)
  endif()
  if(ARC_BUILD_EXTERNAL_CONSUMER_TEST AND PROJECT_IS_TOP_LEVEL)
    add_test(NAME arc_external_consumer
      COMMAND "${CMAKE_COMMAND}"
        -DARC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DARC_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DARC_GENERATOR=${CMAKE_GENERATOR}
        -DARC_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_external_consumer.cmake)
  endif()
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS arc_protocol arc_core
  EXPORT ArcTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(ARC_BUILD_PLATFORM_TARGETS)
  install(TARGETS arc_Web arc_Windows arc_Android arc_macOS arc_iOS arc_iPadOS arc_Headless arc_ChromiumOS arc_Device
    EXPORT ArcTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT ArcTargets NAMESPACE Arc:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Arc)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ArcConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArcConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ArcConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Arc")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ArcConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ArcConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Arc")
