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

add_library(zaga_lock_provider SHARED
    src/provider/Dll.cpp
    src/provider/LockGate.cpp
    src/provider/ZagaProvider.cpp
    src/provider/ZagaCredential.cpp
)
target_include_directories(zaga_lock_provider PUBLIC src/provider)
target_link_libraries(zaga_lock_provider PRIVATE
    zaga_core shlwapi ole32 oleaut32 advapi32 gdi32 user32 uuid)
set_target_properties(zaga_lock_provider PROPERTIES
    LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/src/provider/exports.def\"")

add_executable(provider_host tests/provider_host.cpp)
target_include_directories(provider_host PRIVATE src/provider)
target_link_libraries(provider_host PRIVATE zaga_core ole32 uuid)
add_dependencies(provider_host zaga_lock_provider)
add_test(NAME provider_host COMMAND provider_host)
