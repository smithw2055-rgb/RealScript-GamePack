cmake_minimum_required(VERSION 3.20)

project(RealScriptGamePack VERSION 0.98.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(REALSCRIPT_GAMEPACK_BUILD_TESTS "Build RealScript GamePack conformance tests" ON)
option(REALSCRIPT_GAMEPACK_BUILD_BENCHMARKS "Build RealScript GamePack benchmarks" ON)
option(REALSCRIPT_GAMEPACK_INSTALL_SDK "Generate installable RealScriptGamePack SDK targets" ON)
option(REALSCRIPT_GAMEPACK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(REALSCRIPT_SOURCE_DIR "" CACHE PATH "Optional path to a local RealScript source tree when building bundled")

if(NOT TARGET RealScript::Runtime)
    if(REALSCRIPT_SOURCE_DIR AND EXISTS "${REALSCRIPT_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${REALSCRIPT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/realscript")
    else()
        find_package(RealScript CONFIG REQUIRED)
    endif()
endif()

if(NOT TARGET RealScript::BindGen)
    message(FATAL_ERROR "RealScriptGamePack requires RealScript::BindGen (configure RealScript with REALSCRIPT_ENABLE_BINDGEN=ON)")
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

add_library(realscript_app_game_sdk STATIC
    src/game/GameScripting.cpp
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
set_target_properties(realscript_app_game_sdk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LINKER_LANGUAGE CXX
    EXPORT_NAME GameSdk
)
target_include_directories(realscript_app_game_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(realscript_app_game_sdk PUBLIC cxx_std_17)
realscript_gamepack_enable_cxx_warnings(realscript_app_game_sdk)
target_link_libraries(realscript_app_game_sdk PUBLIC
    RealScript::BindGen
    RealScript::Runtime
    RealScript::Compiler
    RealScript::NativeBackends
)
if(TARGET RealScript::Debug)
    target_link_libraries(realscript_app_game_sdk PUBLIC RealScript::Debug)
    target_compile_definitions(realscript_app_game_sdk PUBLIC REALSCRIPT_HAS_DEBUG_SERVICES=1)
else()
    target_compile_definitions(realscript_app_game_sdk PUBLIC REALSCRIPT_HAS_DEBUG_SERVICES=0)
endif()
add_library(RealScript::GameSdk ALIAS realscript_app_game_sdk)

add_library(realscript_game_runtime INTERFACE)
target_link_libraries(realscript_game_runtime INTERFACE realscript_app_game_sdk)
set_target_properties(realscript_game_runtime PROPERTIES EXPORT_NAME GameRuntime)
add_library(RealScript::GameRuntime ALIAS realscript_game_runtime)

add_library(realscript_game_compiler INTERFACE)
target_link_libraries(realscript_game_compiler INTERFACE realscript_app_game_sdk)
set_target_properties(realscript_game_compiler PROPERTIES EXPORT_NAME GameCompiler)
add_library(RealScript::GameCompiler ALIAS realscript_game_compiler)

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

if(REALSCRIPT_GAMEPACK_INSTALL_SDK)
    install(
        TARGETS
            realscript_app_game_sdk
            realscript_game_runtime
            realscript_game_compiler
        EXPORT RealScriptGamePackTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    configure_package_config_file(
        cmake/RealScriptGamePackConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    install(
        EXPORT RealScriptGamePackTargets
        FILE RealScriptGamePackTargets.cmake
        NAMESPACE RealScript::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/RealScriptGamePackConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RealScriptGamePack
    )
endif()
