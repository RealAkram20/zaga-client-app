cmake_minimum_required(VERSION 3.20)

project(ZagaDeviceClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# Target Windows 10 so the SDK exposes the DPAPI and credential-provider APIs.
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(zaga_core STATIC
    src/core/Base32.cpp
    src/core/Sha256Hmac.cpp
    src/core/TokenCodec.cpp
    src/core/Verifier.cpp
    src/store/DataProtection.cpp
    src/store/LocalStore.cpp
)
target_include_directories(zaga_core PUBLIC src/core src/store)
target_link_libraries(zaga_core PUBLIC bcrypt crypt32 shell32)

enable_testing()

add_executable(core_tests tests/test_main.cpp)
target_link_libraries(core_tests PRIVATE zaga_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(store_tests tests/store_tests.cpp)
target_link_libraries(store_tests PRIVATE zaga_core)
add_test(NAME store_tests COMMAND store_tests)
