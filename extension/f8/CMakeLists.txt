cmake_minimum_required(VERSION 3.5)

project(F8Extension)

include(ExternalProject)

include_directories(include runtime/include)

set(F8_EXTENSION_FILES f8_extension.cpp f8_module_runner.cpp)

# The wasm engine lives in runtime/rust and is built with cargo, so this
# extension needs a Rust toolchain. wasmtime's dependencies need edition2024,
# i.e. cargo >= 1.85, and a distro or Homebrew cargo is often older while still
# being first on PATH - the failure it produces ("feature `edition2024` is
# required") does not name the real cause. So search rustup's toolchains too and
# take the newest cargo that is new enough.
set(F8_MIN_CARGO_VERSION 1.85)

file(GLOB F8_CARGO_CANDIDATES "$ENV{HOME}/.rustup/toolchains/*/bin/cargo")
find_program(F8_CARGO_ON_PATH cargo HINTS "$ENV{HOME}/.cargo/bin")
if(F8_CARGO_ON_PATH)
  list(APPEND F8_CARGO_CANDIDATES ${F8_CARGO_ON_PATH})
endif()

set(CARGO_EXECUTABLE "")
set(F8_BEST_CARGO_VERSION 0)
foreach(candidate ${F8_CARGO_CANDIDATES})
  execute_process(
    COMMAND ${candidate} --version
    OUTPUT_VARIABLE candidate_output
    ERROR_QUIET
    RESULT_VARIABLE candidate_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(candidate_result EQUAL 0 AND candidate_output MATCHES
                                  "cargo ([0-9]+\\.[0-9]+)")
    set(candidate_version ${CMAKE_MATCH_1})
    if(candidate_version VERSION_GREATER_EQUAL F8_MIN_CARGO_VERSION
       AND candidate_version VERSION_GREATER F8_BEST_CARGO_VERSION)
      set(CARGO_EXECUTABLE ${candidate})
      set(F8_BEST_CARGO_VERSION ${candidate_version})
    endif()
  endif()
endforeach()

if(NOT CARGO_EXECUTABLE)
  message(
    FATAL_ERROR
      "the f8 extension needs cargo >= ${F8_MIN_CARGO_VERSION} to build its wasm engine, and none was found. Install a Rust toolchain, or drop f8 from EXTENSION_CONFIGS."
  )
endif()

message(
  STATUS
    "Using cargo ${F8_BEST_CARGO_VERSION} at ${CARGO_EXECUTABLE} - building the f8 wasm engine"
)

# Invoked by absolute path, so put its own bin directory first to make rustc
# resolve to the matching toolchain rather than to an older one on PATH.
get_filename_component(F8_CARGO_BIN_DIR ${CARGO_EXECUTABLE} DIRECTORY)

set(F8_RUST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/runtime/rust)
# Keep cargo's output out of the source tree.
set(F8_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust)
set(F8_LIB ${F8_TARGET_DIR}/release/libf8.a)

ExternalProject_Add(
  f8_engine
  SOURCE_DIR ${F8_RUST_DIR}
  CONFIGURE_COMMAND ""
  UPDATE_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_COMMAND
    ${CMAKE_COMMAND} -E env "PATH=${F8_CARGO_BIN_DIR}:$ENV{PATH}"
    CARGO_TARGET_DIR=${F8_TARGET_DIR} ${CARGO_EXECUTABLE} build --release
    --manifest-path ${F8_RUST_DIR}/Cargo.toml
  BUILD_BYPRODUCTS ${F8_LIB}
  BUILD_ALWAYS 1
  LOG_BUILD ON)

build_static_extension(f8 ${F8_EXTENSION_FILES})
set(PARAMETERS "-warnings")
build_loadable_extension(f8 ${PARAMETERS} ${F8_EXTENSION_FILES})

# A Rust staticlib needs the platform libraries its std was linked against.
if(APPLE)
  set(F8_PLATFORM_LIBS "-framework CoreFoundation" "-framework Security")
elseif(UNIX)
  set(F8_PLATFORM_LIBS pthread dl m)
endif()

foreach(target f8_extension f8_loadable_extension)
  add_dependencies(${target} f8_engine)
  target_link_libraries(${target} ${F8_LIB} ${F8_PLATFORM_LIBS})
endforeach()

install(
  TARGETS f8_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
