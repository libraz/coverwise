# WASM bindings via Emscripten embind

if(BUILD_WASM)
  add_executable(coverwise_wasm bindings.cpp)
  target_link_libraries(coverwise_wasm PRIVATE coverwise_lib)

  set_target_properties(coverwise_wasm PROPERTIES
    OUTPUT_NAME "coverwise"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
  )

  # Enable C++ exception catching at the JS<->C++ boundary. Without this,
  # Emscripten defaults to DISABLE_EXCEPTION_CATCHING=1: a thrown std::exception
  # is not caught by the try/catch in bindings.cpp and instead traps, aborting
  # the module and poisoning the singleton. -fexceptions lets the boundary
  # catch handlers convert malformed input into a structured error object.
  target_compile_options(coverwise_wasm PRIVATE "-fexceptions")
  target_link_options(coverwise_wasm PRIVATE
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_ES6=1"
    "SHELL:-s EXPORT_NAME='createCoverwise'"
    "SHELL:-s ENVIRONMENT='web,node'"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "-fexceptions"
    "--bind"
  )
endif()
