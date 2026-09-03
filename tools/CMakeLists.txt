# coverwise CLI tool

add_executable(coverwise main.cpp)
target_compile_features(coverwise PRIVATE cxx_std_17)
target_link_libraries(coverwise PRIVATE coverwise::coverwise)
coverwise_enable_warnings(coverwise)
coverwise_enable_sanitizers(coverwise)
set_target_properties(coverwise PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${COVERWISE_RUNTIME_OUTPUT_DIRECTORY}"
)
# An embedded coverwise never adds to the parent project's install manifest.
if(COVERWISE_TOP_LEVEL)
  install(TARGETS coverwise RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
