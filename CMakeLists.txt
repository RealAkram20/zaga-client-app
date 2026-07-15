cmake_minimum_required(VERSION 3.20)

project(ZagaDeviceClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

add_library(zaga_core STATIC
    src/core/Base32.cpp
    src/core/Sha256Hmac.cpp
    src/core/TokenCodec.cpp
    src/core/Verifier.cpp
)
target_include_directories(zaga_core PUBLIC src/core)
target_link_libraries(zaga_core PUBLIC bcrypt)

enable_testing()

add_executable(core_tests tests/test_main.cpp)
target_link_libraries(core_tests PRIVATE zaga_core)

add_test(NAME core_tests COMMAND core_tests)
