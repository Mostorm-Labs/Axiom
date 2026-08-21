set(prefix "${ARC_BINARY_DIR}/external-prefix")
set(build "${ARC_BINARY_DIR}/external-consumer-build")
file(REMOVE_RECURSE "${prefix}" "${build}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${ARC_BINARY_DIR}" --prefix "${prefix}"
          --config "${ARC_CONFIG}"
  RESULT_VARIABLE install_status)
if(NOT install_status EQUAL 0)
  message(FATAL_ERROR "Arc install failed: ${install_status}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -S "${ARC_SOURCE_DIR}/tests/external_consumer"
          -B "${build}"
          -G "${ARC_GENERATOR}"
          -DCMAKE_PREFIX_PATH=${prefix}
  RESULT_VARIABLE configure_status)
if(NOT configure_status EQUAL 0)
  message(FATAL_ERROR "external Arc consumer configure failed: ${configure_status}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${build}" --config "${ARC_CONFIG}"
  RESULT_VARIABLE build_status)
if(NOT build_status EQUAL 0)
  message(FATAL_ERROR "external Arc consumer build failed: ${build_status}")
endif()

execute_process(
  COMMAND "${build}/arc_external_consumer"
  RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
  message(FATAL_ERROR "external Arc consumer failed: ${run_status}")
endif()
