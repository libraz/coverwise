# WASM bindings via Emscripten embind

if(BUILD_WASM)
  add_executable(coverwise_wasm bindings.cpp)
  target_link_libraries(coverwise_wasm PRIVATE coverwise_lib)

  set_target_properties(coverwise_wasm PROPERTIES
    OUTPUT_NAME "coverwise"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
  )

  target_link_options(coverwise_wasm PRIVATE
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_ES6=1"
    "SHELL:-s EXPORT_NAME='createCoverwise'"
    "SHELL:-s ENVIRONMENT='web,node'"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "--bind"
  )
endif()
