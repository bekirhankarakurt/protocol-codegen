cmake_minimum_required(VERSION 3.16)
project(protocol-codegen VERSION 1.0.0 LANGUAGES CXX)

option(PCG_BUILD_TESTS "Build unit tests" ON)
option(PCG_BUILD_EXAMPLES "Build the demo program" ON)
option(PCG_WERROR "Treat compiler warnings as errors" OFF)
option(PCG_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(pcg_warnings INTERFACE)
target_compile_features(pcg_warnings INTERFACE cxx_std_17)
if(MSVC)
  target_compile_options(pcg_warnings INTERFACE /W4 $<$<BOOL:${PCG_WERROR}>:/WX>)
else()
  target_compile_options(pcg_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion $<$<BOOL:${PCG_WERROR}>:-Werror>)
  if(PCG_SANITIZE)
    target_compile_options(pcg_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
                           -fno-sanitize-recover=undefined)
    target_link_options(pcg_warnings INTERFACE -fsanitize=address,undefined)
  endif()
endif()

add_library(pcg_core STATIC src/json.cpp src/schema.cpp src/emit_cpp.cpp src/emit_lua.cpp)
target_include_directories(pcg_core PUBLIC src)
target_link_libraries(pcg_core PUBLIC pcg_warnings)
set_target_properties(pcg_core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(protocol-codegen src/main.cpp)
target_link_libraries(protocol-codegen PRIVATE pcg_core)
target_compile_definitions(protocol-codegen PRIVATE PCG_VERSION="${PROJECT_VERSION}")
set_target_properties(protocol-codegen PROPERTIES CXX_EXTENSIONS OFF)
install(TARGETS protocol-codegen RUNTIME DESTINATION bin)

if(PCG_BUILD_EXAMPLES)
  add_executable(demo examples/demo.cpp)
  target_include_directories(demo PRIVATE examples/generated)
  target_link_libraries(demo PRIVATE pcg_warnings)
  set_target_properties(demo PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(PCG_BUILD_TESTS)
  enable_testing()
  foreach(name json schema golden roundtrip)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_include_directories(test_${name} PRIVATE tests examples/generated)
    target_link_libraries(test_${name} PRIVATE pcg_core)
    target_compile_definitions(test_${name} PRIVATE PCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(test_${name} PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ${name} COMMAND test_${name})
  endforeach()
  add_test(NAME cli_check_telemetry_link
           COMMAND protocol-codegen ${CMAKE_CURRENT_SOURCE_DIR}/examples/telemetry_link.json
                   --check -o ${CMAKE_CURRENT_SOURCE_DIR}/examples/generated)
  add_test(NAME cli_check_nav_bus
           COMMAND protocol-codegen ${CMAKE_CURRENT_SOURCE_DIR}/examples/nav_bus.json
                   --check -o ${CMAKE_CURRENT_SOURCE_DIR}/examples/generated)
endif()
