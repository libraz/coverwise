if(NOT DEFINED MODE OR NOT DEFINED FIXTURE_SOURCE_DIR OR NOT DEFINED FIXTURE_BINARY_DIR)
  message(FATAL_ERROR "MODE and fixture directories are required")
endif()

file(REMOVE_RECURSE "${FIXTURE_BINARY_DIR}")
set(configure_args)

if(MODE STREQUAL "add_subdirectory")
  list(APPEND configure_args "-DCOVERWISE_SOURCE_DIR=${COVERWISE_SOURCE_DIR}")
elseif(MODE STREQUAL "find_package")
  set(install_dir "${FIXTURE_BINARY_DIR}/install")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${COVERWISE_BINARY_DIR}" --prefix "${install_dir}"
    RESULT_VARIABLE install_result
  )
  if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "coverwise install failed")
  endif()
  list(APPEND configure_args "-DCMAKE_PREFIX_PATH=${install_dir}")
else()
  message(FATAL_ERROR "Unknown fixture mode: ${MODE}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE_SOURCE_DIR}" -B "${FIXTURE_BINARY_DIR}"
          ${configure_args}
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Fixture configure failed")
endif()

set(build_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  list(APPEND build_args --config "${CONFIG}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${FIXTURE_BINARY_DIR}" ${build_args}
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Fixture build failed")
endif()

set(consumer "${FIXTURE_BINARY_DIR}/consumer${CMAKE_EXECUTABLE_SUFFIX}")
if(DEFINED CONFIG AND EXISTS "${FIXTURE_BINARY_DIR}/${CONFIG}/consumer${CMAKE_EXECUTABLE_SUFFIX}")
  set(consumer "${FIXTURE_BINARY_DIR}/${CONFIG}/consumer${CMAKE_EXECUTABLE_SUFFIX}")
endif()
execute_process(COMMAND "${consumer}" RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "Fixture executable failed")
endif()
