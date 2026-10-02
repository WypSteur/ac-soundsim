# Explicit dependency slice: no application, SDL, graphics, scripting, dyno,
# vehicle, transmission, mechanical solver or audio-device backend.
set(ES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/engine-sim")
set(ES_REVISION "85f7c3b959a908ed5232ede4f1a4ac7eafe6b630")
set(SCS_REVISION "e009f4ff1c9c4c5874e865e893cdb62e208fb2b3")
set(SCS_ROOT "${ES_ROOT}/dependencies/submodules/simple-2d-constraint-solver")
if(NOT EXISTS "${SCS_ROOT}/src/rigid_body.cpp")
    message(FATAL_ERROR "Run scripts/bootstrap_engine_sim.ps1 before configuring M1")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${ES_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE ES_ACTUAL OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SCS_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE SCS_ACTUAL OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT ES_ACTUAL STREQUAL ES_REVISION OR NOT SCS_ACTUAL STREQUAL SCS_REVISION)
    message(FATAL_ERROR "Unexpected upstream revision: re-audit headless adapter before updating pins")
endif()
file(READ "${ES_ROOT}/include/synthesizer.h" ES_SYNTH_HEADER)
if(NOT ES_SYNTH_HEADER MATCHES "renderAudioSynchronous")
    message(FATAL_ERROR "Headless patch missing: run scripts/bootstrap_engine_sim.ps1")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${ES_ROOT}" apply --reverse --check
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/engine_sim/patches/0001-headless-block-renderer.patch"
    RESULT_VARIABLE ES_PATCH_RESULT ERROR_VARIABLE ES_PATCH_ERROR)
if(NOT ES_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Headless patch does not match: ${ES_PATCH_ERROR}")
endif()
set(ES_SOURCES
    camshaft combustion_chamber connecting_rod convolution_filter crankshaft
    cylinder_bank cylinder_head delay_filter derivative_filter direct_throttle_linkage
    engine exhaust_system filter fuel function gas_system gaussian_filter
    ignition_module intake jitter_filter leveling_filter low_pass_filter part
    piston standard_valvetrain synthesizer throttle utilities valvetrain)
list(TRANSFORM ES_SOURCES PREPEND "${ES_ROOT}/src/")
list(TRANSFORM ES_SOURCES APPEND ".cpp")
add_library(engine_sim_headless STATIC ${ES_SOURCES}
    "${SCS_ROOT}/src/force_generator.cpp" "${SCS_ROOT}/src/rigid_body.cpp"
    "${SCS_ROOT}/src/system_state.cpp" "${SCS_ROOT}/src/utilities.cpp")
target_include_directories(engine_sim_headless PUBLIC "${ES_ROOT}/include")
target_compile_definitions(engine_sim_headless PRIVATE ACSOUNDSIM_HEADLESS_ONLY NOMINMAX)
target_compile_definitions(engine_sim_headless PUBLIC ACSOUNDSIM_ENGINE_SIM_REVISION="${ES_REVISION}")
if(MSVC)
    target_compile_options(engine_sim_headless PRIVATE /W3 /FIcmath /FIcfloat /FIcstring /FIalgorithm)
endif()
