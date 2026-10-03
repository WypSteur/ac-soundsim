cmake_minimum_required(VERSION 3.20)
project(ac_soundsim VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACSOUNDSIM_BUILD_TESTS "Build AC SoundSim tests" ON)
option(ACSOUNDSIM_ENGINE_SIM "Build pinned headless Engine-Sim adapter" ON)

include(FetchContent)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml_cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG f7320141120f720aecc4c32be25586e7da9eb978 # 0.8.0
)
FetchContent_MakeAvailable(yaml_cpp)

add_library(soundsim_core STATIC
    core/src/logger.cpp
    core/src/engine_profile.cpp
    core/src/externally_driven_crank.cpp
    core/src/externally_driven_ignition.cpp
    core/src/ipc.cpp
)
target_link_libraries(soundsim_core PRIVATE yaml-cpp::yaml-cpp)
target_compile_definitions(soundsim_core PRIVATE
    ACSOUNDSIM_FA20_PROFILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/profiles/engines/subaru_fa20.yaml")

target_include_directories(soundsim_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

target_compile_definitions(soundsim_core PUBLIC
    ACSOUNDSIM_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(soundsim_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(soundsim_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(soundsim-poc
    apps/soundsim-poc/main.cpp
)
target_link_libraries(soundsim-poc PRIVATE soundsim_core)

if(ACSOUNDSIM_ENGINE_SIM)
    include(cmake/EngineSimHeadless.cmake)
    add_library(soundsim_engine STATIC core/src/headless_engine.cpp)
    target_link_libraries(soundsim_engine PUBLIC soundsim_core PRIVATE engine_sim_headless)
    target_compile_definitions(soundsim_engine PRIVATE
        ACSOUNDSIM_FA20_IR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/vendor/engine-sim/assets/sound-library/smooth/smooth_39.wav")
    if(MSVC)
        target_compile_options(soundsim_engine PRIVATE /W4 /permissive-)
    endif()
    add_executable(soundsim-m1 apps/soundsim-m1/main.cpp)
    target_link_libraries(soundsim-m1 PRIVATE soundsim_engine)
    add_executable(soundsim-audio-audit apps/audio-audit/main.cpp)
    target_link_libraries(soundsim-audio-audit PRIVATE soundsim_engine)
    add_executable(soundsim-ignition-audit apps/ignition-audit/main.cpp)
    target_link_libraries(soundsim-ignition-audit PRIVATE soundsim_engine)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(soundsim-runtime apps/soundsim-runtime/main.cpp)
        target_link_libraries(soundsim-runtime PRIVATE soundsim_engine winmm)
        add_executable(soundsim-loopback-meter apps/loopback-meter/main.cpp)
        target_link_libraries(soundsim-loopback-meter PRIVATE ole32 uuid)
        add_executable(soundsim-m5-capture apps/m5-capture/main.cpp)
        target_link_libraries(soundsim-m5-capture PRIVATE soundsim_core ole32 uuid winmm)
        add_executable(soundsim-ipc-test tests/ipc.cpp)
        target_link_libraries(soundsim-ipc-test PRIVATE soundsim_engine)
    endif()
endif()

if(ACSOUNDSIM_BUILD_TESTS)
    enable_testing()
    if(TARGET soundsim-ipc-test)
        add_test(NAME soundsim-ipc-test COMMAND soundsim-ipc-test)
        add_test(NAME soundsim-runtime-integration COMMAND soundsim-ipc-test --runtime $<TARGET_FILE:soundsim-runtime>)
        set_tests_properties(soundsim-runtime-integration PROPERTIES TIMEOUT 30)
        add_test(NAME M5D_capture_help COMMAND soundsim-m5-capture --help)
    endif()
    add_executable(soundsim-smoke-test tests/smoke.cpp)
    target_link_libraries(soundsim-smoke-test PRIVATE soundsim_core)
    add_test(NAME soundsim-smoke-test COMMAND soundsim-smoke-test)
    add_executable(soundsim-crank-test tests/crank.cpp)
    target_link_libraries(soundsim-crank-test PRIVATE soundsim_core)
    add_test(NAME soundsim-crank-test COMMAND soundsim-crank-test)
    add_executable(soundsim-ignition-test tests/ignition.cpp)
    target_link_libraries(soundsim-ignition-test PRIVATE soundsim_core)
    add_test(NAME ignition_dynamic_regression COMMAND soundsim-ignition-test)
    add_executable(soundsim-profile-test tests/profile.cpp)
    target_link_libraries(soundsim-profile-test PRIVATE soundsim_core yaml-cpp::yaml-cpp)
    target_compile_definitions(soundsim-profile-test PRIVATE
        ACSOUNDSIM_TEST_PROFILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/profiles/engines/subaru_fa20.yaml")
    add_test(NAME soundsim-profile-test COMMAND soundsim-profile-test)
    if(ACSOUNDSIM_ENGINE_SIM)
        add_executable(soundsim-headless-test tests/headless.cpp)
        target_link_libraries(soundsim-headless-test PRIVATE soundsim_engine)
        add_test(NAME soundsim-headless-test COMMAND soundsim-headless-test)
        add_test(NAME soundsim-reference-audit COMMAND soundsim-audio-audit --output ${CMAKE_BINARY_DIR}/audio-audit-test)
        set_tests_properties(soundsim-reference-audit PROPERTIES TIMEOUT 180)
        add_test(NAME soundsim-m1-sweep COMMAND soundsim-m1 --output ${CMAKE_BINARY_DIR}/m1-test)
        set_tests_properties(soundsim-m1-sweep PROPERTIES TIMEOUT 180)
        add_executable(soundsim-dynamic-test tests/dynamic.cpp)
        target_link_libraries(soundsim-dynamic-test PRIVATE soundsim_engine)
        add_test(NAME M5A_dynamic_gate COMMAND soundsim-dynamic-test)
        set_tests_properties(M5A_dynamic_gate PROPERTIES TIMEOUT 900)
        add_test(NAME ignition_90hz_known_upstream COMMAND soundsim-ignition-audit --output ${CMAKE_BINARY_DIR}/ignition-upstream --upstream)
        add_test(NAME ignition_90hz_fixed COMMAND soundsim-ignition-audit --output ${CMAKE_BINARY_DIR}/ignition-fixed)
    endif()
endif()
