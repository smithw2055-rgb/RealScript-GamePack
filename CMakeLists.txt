cmake_minimum_required(VERSION 3.20)

project(RealScriptGamePack VERSION 0.98.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(REALSCRIPT_GAMEPACK_BUILD_TESTS "Build RealScript GamePack conformance tests" ON)
option(REALSCRIPT_GAMEPACK_BUILD_BENCHMARKS "Build RealScript GamePack benchmarks" ON)
option(REALSCRIPT_GAMEPACK_INSTALL_SDK "Generate installable RealScriptGamePack SDK targets" ON)
option(REALSCRIPT_GAMEPACK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(REALSCRIPT_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build"
    CACHE PATH "Path to a RealScript build tree or installed SDK prefix containing RealScriptConfig.cmake")

if(DEFINED REALSCRIPT_SOURCE_DIR AND NOT "${REALSCRIPT_SOURCE_DIR}" STREQUAL "")
    message(WARNING "REALSCRIPT_SOURCE_DIR is deprecated and ignored; RealScriptGamePack consumes prebuilt RealScript outputs via RealScript_DIR / REALSCRIPT_BUILD_DIR / CMAKE_PREFIX_PATH.")
endif()

if(NOT TARGET RealScript::SlimRuntime)
    find_package(RealScript CONFIG REQUIRED
        HINTS
            "${REALSCRIPT_BUILD_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build"
            "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build/sdk"
    )
endif()

if(DEFINED RealScript_SLIM_RUNTIME)
    set(REALSCRIPT_SLIM_RUNTIME "${RealScript_SLIM_RUNTIME}")
endif()

if(NOT TARGET RealScript::SlimRuntime OR NOT TARGET RealScript::BindGenRuntime OR
   NOT TARGET RealScript::StencilJit OR NOT TARGET RealScript::HotReload)
    message(FATAL_ERROR "RealScriptGamePack requires RealScript::SlimRuntime, RealScript::BindGenRuntime, RealScript::StencilJit, and RealScript::HotReload")
endif()

if(NOT REALSCRIPT_SLIM_RUNTIME)
    if(NOT TARGET RealScript::Frontend)
        message(FATAL_ERROR "Full RealScriptGamePack build requires RealScript::Frontend")
    endif()
    if(NOT TARGET RealScript::BindGen)
        message(FATAL_ERROR "Full RealScriptGamePack build requires RealScript::BindGen (configure RealScript with REALSCRIPT_ENABLE_BINDGEN=ON)")
    endif()
endif()

function(realscript_gamepack_enable_cxx_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /wd4324 /permissive-)
        if(REALSCRIPT_GAMEPACK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(REALSCRIPT_GAMEPACK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(MSVC)
    set_source_files_properties(
        src/game/GameProductization.cpp
        src/game/GameplayStateCodec.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4456"
    )
endif()

add_library(realscript_game_runtime STATIC
    src/game/ScriptObjectRuntime.cpp
    src/game/ScriptInvokeRuntime.cpp
    src/game/ScriptValueRuntime.cpp
    src/game/SceneLifecycleRuntime.cpp
    src/game/SceneEventRuntime.cpp
    src/game/GameProductization.cpp
    src/game/GameplayPrimitives.cpp
    src/game/GameplayMetadata.cpp
    src/game/GameplayScripting.cpp
    src/game/GameplayStateCodec.cpp
)
set_target_properties(realscript_game_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LINKER_LANGUAGE CXX
    EXPORT_NAME GameRuntime
)
target_include_directories(realscript_game_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(realscript_game_runtime PUBLIC cxx_std_17)
realscript_gamepack_enable_cxx_warnings(realscript_game_runtime)
target_link_libraries(realscript_game_runtime PUBLIC
    RealScript::SlimRuntime
    RealScript::StencilJit
    RealScript::HotReload
    RealScript::BindGenRuntime
)
target_compile_definitions(realscript_game_runtime PRIVATE REALSCRIPT_HAS_DEBUG_SERVICES=0)
add_library(RealScript::GameRuntime ALIAS realscript_game_runtime)

if(NOT REALSCRIPT_SLIM_RUNTIME)
    add_library(realscript_game_compiler STATIC
        src/game/GameScriptCompiler.cpp
    )
    set_target_properties(realscript_game_compiler PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        LINKER_LANGUAGE CXX
        EXPORT_NAME GameCompiler
    )
    target_include_directories(realscript_game_compiler
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(realscript_game_compiler PUBLIC cxx_std_17)
    realscript_gamepack_enable_cxx_warnings(realscript_game_compiler)
    target_link_libraries(realscript_game_compiler PUBLIC
        RealScript::GameRuntime
        RealScript::Frontend
        RealScript::BindGen
        RealScript::NativeBackends
    )
    if(TARGET RealScript::Debug)
        target_link_libraries(realscript_game_compiler PUBLIC RealScript::Debug)
        target_compile_definitions(realscript_game_compiler PUBLIC REALSCRIPT_HAS_DEBUG_SERVICES=1)
    else()
        target_compile_definitions(realscript_game_compiler PUBLIC REALSCRIPT_HAS_DEBUG_SERVICES=0)
    endif()
    add_library(RealScript::GameCompiler ALIAS realscript_game_compiler)

    add_library(realscript_app_game_sdk INTERFACE)
    target_link_libraries(realscript_app_game_sdk INTERFACE
        realscript_game_compiler
        RealScript::Runtime
        RealScript::Compiler
    )
    set_target_properties(realscript_app_game_sdk PROPERTIES EXPORT_NAME GameSdk)
    add_library(RealScript::GameSdk ALIAS realscript_app_game_sdk)

    if(REALSCRIPT_GAMEPACK_BUILD_BENCHMARKS)
        add_executable(rsbench_product benchmarks/product_macro_driver.cpp)
        target_link_libraries(rsbench_product PRIVATE RealScript::GameSdk RealScript::Full)
        target_compile_features(rsbench_product PRIVATE cxx_std_17)
        realscript_gamepack_enable_cxx_warnings(rsbench_product)
    endif()

    if(REALSCRIPT_GAMEPACK_BUILD_TESTS)
        enable_testing()
        add_executable(realscript_app_game_sdk_and_scene_tests
            tests/app_game_sdk_and_scene_tests.cpp)
        target_link_libraries(realscript_app_game_sdk_and_scene_tests PRIVATE RealScript::GameSdk)
        target_compile_features(realscript_app_game_sdk_and_scene_tests PRIVATE cxx_std_17)
        realscript_gamepack_enable_cxx_warnings(realscript_app_game_sdk_and_scene_tests)
        add_test(
            NAME realscript.gamepack.sdk-and-scene
            COMMAND realscript_app_game_sdk_and_scene_tests)
    endif()
endif()

if(REALSCRIPT_GAMEPACK_BUILD_TESTS)
    enable_testing()
    add_executable(realscript_game_runtime_slim_tests
        tests/game_runtime_slim_tests.cpp)
    target_link_libraries(realscript_game_runtime_slim_tests PRIVATE RealScript::GameRuntime)
    target_compile_features(realscript_game_runtime_slim_tests PRIVATE cxx_std_17)
    target_compile_definitions(realscript_game_runtime_slim_tests PRIVATE REALSCRIPT_HAS_DEBUG_SERVICES=0)
    realscript_gamepack_enable_cxx_warnings(realscript_game_runtime_slim_tests)
    add_test(
        NAME realscript.gamepack.runtime-slim
        COMMAND realscript_game_runtime_slim_tests)
endif()

set(realscript_gamepack_export_targets realscript_game_runtime)
if(NOT REALSCRIPT_SLIM_RUNTIME)
    list(APPEND realscript_gamepack_export_targets
        realscript_game_compiler
        realscript_app_game_sdk
    )
endif()

file(TO_CMAKE_PATH "${RealScript_DIR}" REALSCRIPT_CONFIG_HINT_DIR)
export(
    TARGETS ${realscript_gamepack_export_targets}
    NAMESPACE RealScript::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackTargets.cmake"
)
configure_package_config_file(
    cmake/RealScriptGamePackConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfig.cmake"
    INSTALL_DESTINATION .
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

if(REALSCRIPT_GAMEPACK_INSTALL_SDK)
    set(REALSCRIPT_CONFIG_HINT_DIR "")
    install(
        TARGETS ${realscript_gamepack_export_targets}
        EXPORT RealScriptGamePackTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    configure_package_config_file(
        cmake/RealScriptGamePackConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/InstallConfig/RealScriptGamePackConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
    install(
        EXPORT RealScriptGamePackTargets
        FILE RealScriptGamePackTargets.cmake
        NAMESPACE RealScript::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/InstallConfig/RealScriptGamePackConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
endif()
