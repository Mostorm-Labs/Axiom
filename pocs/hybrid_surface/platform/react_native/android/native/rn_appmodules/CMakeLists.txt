# Reuse React Native's version-matched application glue. Keep this target
# separate from canvas_poc05_android: RN owns module/component registration,
# while the Canvas target owns rendering, input, and placement.
set(POC05_SAVED_CMAKE_PROJECT_NAME "${CMAKE_PROJECT_NAME}")
set(CMAKE_PROJECT_NAME appmodules)
include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)
set(CMAKE_PROJECT_NAME "${POC05_SAVED_CMAKE_PROJECT_NAME}")

target_link_options(appmodules PRIVATE
  "-Wl,-z,max-page-size=16384"
  "-Wl,-z,common-page-size=16384")
