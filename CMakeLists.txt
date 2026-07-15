cmake_minimum_required(VERSION 3.20)

project(ZagaDeviceClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC runtime so the installer and the credential-provider
# DLL run on a clean Windows machine with no Visual C++ redistributable, and so the
# provider DLL does not depend on a CRT version inside the winlogon process.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
    src/config/DeviceConfig.cpp
    src/gate/LockGate.cpp
    src/net/Json.cpp
    src/net/HttpClient.cpp
    src/net/PortalClient.cpp
)
target_include_directories(zaga_core PUBLIC src/core src/store src/config src/gate src/net)
target_link_libraries(zaga_core PUBLIC bcrypt crypt32 shell32 advapi32 winhttp)

enable_testing()

add_executable(core_tests tests/test_main.cpp)
target_link_libraries(core_tests PRIVATE zaga_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(store_tests tests/store_tests.cpp)
target_link_libraries(store_tests PRIVATE zaga_core)
add_test(NAME store_tests COMMAND store_tests)

add_library(zaga_lock_provider SHARED
    src/provider/Dll.cpp
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

add_executable(config_tests tests/config_tests.cpp)
target_link_libraries(config_tests PRIVATE zaga_core)
add_test(NAME config_tests COMMAND config_tests)

add_executable(json_tests tests/json_tests.cpp)
target_link_libraries(json_tests PRIVATE zaga_core)
add_test(NAME json_tests COMMAND json_tests)

# Manual connectivity harness; needs a live portal, so it is not a ctest.
add_executable(net_host tests/net_host.cpp)
target_link_libraries(net_host PRIVATE zaga_core)

add_executable(zaga_installer src/installer/Installer.cpp)
target_link_libraries(zaga_installer PRIVATE zaga_core ole32 shell32)
set_target_properties(zaga_installer PROPERTIES
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
