# Native gate. No stub target is substituted when a dependency is absent.
# Intentionally separate from the production player's CMake graph in patch 01.
enable_language(C)
set(VEYRA_RP_CHIAKI_SOURCE_DIR "" CACHE PATH "Patched chiaki-ng staging source tree")
set(VEYRA_RP_CHIAKI_VERIFY_DIR "${VEYRA_RP_CHIAKI_SOURCE_DIR}" CACHE PATH "Clean pinned chiaki-ng checkout used to verify provenance")
if(NOT EXISTS "${VEYRA_RP_CHIAKI_SOURCE_DIR}/lib/include/chiaki/session.h")
    message(FATAL_ERROR "VEYRA_RP_BUILD_NATIVE needs the pinned chiaki-ng source tree")
endif()
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${VEYRA_RP_CHIAKI_VERIFY_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE CHIAKI_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CHIAKI_HEAD_RESULT)
if(NOT CHIAKI_HEAD_RESULT EQUAL 0 OR NOT CHIAKI_HEAD STREQUAL "0e16950165f06e5c3291537c2eeba6e852be7120")
    message(FATAL_ERROR "Chiaki source pin mismatch: ${CHIAKI_HEAD}")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${VEYRA_RP_CHIAKI_VERIFY_DIR}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE CHIAKI_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CHIAKI_STATUS_RESULT)
if(NOT CHIAKI_STATUS_RESULT EQUAL 0 OR NOT CHIAKI_STATUS STREQUAL "")
    message(FATAL_ERROR "Chiaki source tree must be clean")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${VEYRA_RP_CHIAKI_SOURCE_DIR}" diff --name-only
    OUTPUT_VARIABLE CHIAKI_STAGE_FILES OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CHIAKI_STAGE_RESULT)
string(REPLACE "\r\n" ";" CHIAKI_STAGE_FILES "${CHIAKI_STAGE_FILES}")
string(REPLACE "\n" ";" CHIAKI_STAGE_FILES "${CHIAKI_STAGE_FILES}")
list(SORT CHIAKI_STAGE_FILES)
if(NOT CHIAKI_STAGE_RESULT EQUAL 0 OR NOT CHIAKI_STAGE_FILES STREQUAL "lib/src/ctrl.c;lib/src/regist.c;lib/src/remote/holepunch.c;lib/src/remote/rudp.c;lib/src/session.c")
    message(FATAL_ERROR "Chiaki staging must contain only the reviewed MSVC compatibility patch: ${CHIAKI_STAGE_FILES}")
endif()
# Scope is this independent probe, NOT the production project. Target C++20
# requirements remain explicit, despite the upstream top-level C++11 default.
set(CHIAKI_ENABLE_GUI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_ANDROID OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_BOREALIS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_FFMPEG_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_PI_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SETSU OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_STEAMDECK_NATIVE OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_SPEEX OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_RUDP OFF CACHE STRING "" FORCE)
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "" FORCE)
set(CHIAKI_LIB_ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(CHIAKI_USE_SYSTEM_NANOPB OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_JERASURE OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_CURL OFF CACHE STRING "" FORCE)
# Disabling RUDP does NOT remove curl/json-c/libevent/miniupnpc in the pinned lib.
# Provision their REAL matching-toolchain dev libraries; never replace them with
# empty imported targets or remove FEC to force the native gate green.
add_subdirectory("${VEYRA_RP_CHIAKI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/chiaki" EXCLUDE_FROM_ALL)
if(NOT TARGET chiaki-lib)
    message(FATAL_ERROR "Upstream chiaki-lib target was not created")
endif()
# vcpkg's static miniupnpc headers default to dllimport on Windows unless the
# caller opts into the static ABI. Without this definition the static library
# is found correctly but Chiaki emits unresolved __imp_* references.
target_compile_definitions(chiaki-lib PRIVATE MINIUPNP_STATICLIB)
add_library(veyra_remoteplay_chiaki STATIC
    "${VEYRA_ROOT}/src/remoteplay/ChiakiBackend.cpp"
    "${VEYRA_ROOT}/src/remoteplay/ChiakiRegistration.cpp")
target_compile_features(veyra_remoteplay_chiaki PUBLIC cxx_std_20)
target_compile_definitions(veyra_remoteplay_chiaki PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_remoteplay_chiaki PUBLIC veyra_remoteplay_core PRIVATE chiaki-lib)
add_executable(veyra_remoteplay_native_probe "${CMAKE_CURRENT_LIST_DIR}/native_main.cpp")
target_compile_features(veyra_remoteplay_native_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_remoteplay_native_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(veyra_remoteplay_native_probe PRIVATE veyra_remoteplay_chiaki chiaki-lib)
add_test(NAME rp.native_core_initialization COMMAND veyra_remoteplay_native_probe)
set_tests_properties(rp.native_core_initialization PROPERTIES TIMEOUT 30 LABELS "native-core;not-ps5")

# Optional source regressions link the production adapter and real FFmpeg.
set(VEYRA_RP_FFMPEG_ROOT "" CACHE PATH "Existing Veyra FFmpeg installation")
if(VEYRA_RP_FFMPEG_ROOT)
    add_executable(veyra_remoteplay_source_tests
        "${VEYRA_ROOT}/tests/remoteplay/SourceTests.cpp"
        "${VEYRA_ROOT}/src/source/RemotePlaySource.cpp"
        "${VEYRA_ROOT}/src/base/Log.cpp")
    target_compile_features(veyra_remoteplay_source_tests PRIVATE cxx_std_20)
    target_compile_definitions(veyra_remoteplay_source_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(veyra_remoteplay_source_tests SYSTEM PRIVATE "${VEYRA_RP_FFMPEG_ROOT}/include")
    target_link_libraries(veyra_remoteplay_source_tests PRIVATE veyra_remoteplay_chiaki
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avcodec.lib"
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avutil.lib"
        "${VEYRA_RP_FFMPEG_ROOT}/lib/avformat.lib")
endif()
