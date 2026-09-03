if(NOT DEFINED MODE OR NOT DEFINED FIXTURE_SOURCE_DIR OR NOT DEFINED FIXTURE_BINARY_DIR)
  message(FATAL_ERROR "MODE and fixture directories are required")
endif()

file(REMOVE_RECURSE "${FIXTURE_BINARY_DIR}")

# Every configuration-dependent step below -- building coverwise's install tree,
# building the fixture, locating the consumer executable -- runs against the
# configuration the test itself runs in, never a multi-config generator's
# implicit default.
set(config_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  list(APPEND config_args --config "${CONFIG}")
endif()

set(configure_args "-DCOVERWISE_SOURCE_DIR=${COVERWISE_SOURCE_DIR}")
if(DEFINED COVERWISE_EXPECTED_VERSION)
  list(APPEND configure_args "-DCOVERWISE_EXPECTED_VERSION=${COVERWISE_EXPECTED_VERSION}")
endif()

# An add_subdirectory fixture needs nothing beyond the source dir it embeds.
if(MODE STREQUAL "find_package")
  set(install_dir "${FIXTURE_BINARY_DIR}/install")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${COVERWISE_BINARY_DIR}" --prefix "${install_dir}"
            ${config_args}
    RESULT_VARIABLE install_result
  )
  if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "coverwise install failed")
  endif()
  list(APPEND configure_args "-DCMAKE_PREFIX_PATH=${install_dir}"
                             "-DCOVERWISE_INSTALL_DIR=${install_dir}")
elseif(NOT MODE STREQUAL "add_subdirectory")
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

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${FIXTURE_BINARY_DIR}" ${config_args}
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

if(MODE STREQUAL "add_subdirectory")
  # The embedding fixture declares no install rules of its own, so anything that
  # lands in its prefix came from coverwise -- which must contribute nothing.
  set(parent_prefix "${FIXTURE_BINARY_DIR}/parent-install")
  file(REMOVE_RECURSE "${parent_prefix}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${FIXTURE_BINARY_DIR}" --prefix "${parent_prefix}"
            ${config_args}
    RESULT_VARIABLE parent_install_result
  )
  if(NOT parent_install_result EQUAL 0)
    message(FATAL_ERROR "Consumer install failed")
  endif()
  file(GLOB_RECURSE parent_installed LIST_DIRECTORIES TRUE "${parent_prefix}/*")
  if(parent_installed)
    string(REPLACE ";" "\n  " parent_installed_lines "${parent_installed}")
    message(FATAL_ERROR
      "coverwise contributed to the consumer's install prefix:\n  ${parent_installed_lines}")
  endif()
endif()
