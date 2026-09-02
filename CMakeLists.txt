cmake_minimum_required(VERSION 3.24)

project(veyra VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Veyra cache variables and feature switches (Playbook section 5.1).
# Machine-specific absolute paths are resolved by scripts/build.ps1 at
# configure time; CMakePresets.json stays free of local paths.
# ---------------------------------------------------------------------------
set(VEYRA_DLSS_SDK_ROOT "" CACHE PATH "NVIDIA DLSS SDK root")
set(VEYRA_NVOF_SDK_ROOT "" CACHE PATH "NVIDIA Optical Flow SDK root")
set(VEYRA_RUNTIME_ROOT "" CACHE PATH "Local NVIDIA runtime root")

option(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR
  "Enable local-only experimental Feature 18 adapter" OFF)
option(VEYRA_ENABLE_DLSS_SR "Enable public DLSS SR backend" OFF)
option(VEYRA_ENABLE_DLSS_FG "Enable public DLSSG backend" OFF)
option(VEYRA_ENABLE_NVOF "Enable NVIDIA Optical Flow guidance" OFF)
option(VEYRA_ENABLE_D3D12_DEBUG "Enable D3D12 debug layer" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VeyraWarnings)

# NVIDIA's static NGX core shim (nvsdk_ngx_s*.lib) is built against the
# static CRT (MT/MTd); match it for the diagnostic tools that link it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Targets grow phase by phase; only what actually compiles is declared.
# ---------------------------------------------------------------------------
add_library(veyra_base STATIC
  src/base/Log.cpp
  src/base/NgxResult.cpp
  src/base/FileIdentity.cpp
)
veyra_apply_warnings(veyra_base)
target_compile_features(veyra_base PUBLIC cxx_std_20)
target_compile_definitions(veyra_base PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(veyra_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(veyra_base PUBLIC bcrypt wintrust crypt32 version)

add_library(veyra_gfx STATIC
  src/gfx/D3D12DeviceContext.cpp
  src/gfx/CommandSlotRing.cpp
)
veyra_apply_warnings(veyra_gfx)
target_compile_features(veyra_gfx PUBLIC cxx_std_20)
target_link_libraries(veyra_gfx PUBLIC veyra_base d3d12 dxgi dxguid)
if(VEYRA_ENABLE_D3D12_DEBUG)
  target_compile_definitions(veyra_gfx PUBLIC VEYRA_D3D12_DEBUG=1)
endif()

# ---------------------------------------------------------------------------
# veyra_ngx: NGX core host + local experimental DLSSNR adapter.
# Compiled only with the explicit experimental switch; missing SDK paths are
# a configure-time failure, never a runtime surprise (Playbook 5.1/8.1).
# ---------------------------------------------------------------------------
if(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR)
  if(NOT VEYRA_DLSS_SDK_ROOT OR NOT EXISTS "${VEYRA_DLSS_SDK_ROOT}/include/nvsdk_ngx.h")
    message(FATAL_ERROR "VEYRA_ENABLE_EXPERIMENTAL_DLSSNR requires VEYRA_DLSS_SDK_ROOT pointing at NVIDIA DLSS SDK 310.7 (include/nvsdk_ngx.h missing)")
  endif()
  add_library(veyra_ngx STATIC
    src/ngx/NgxCoreHost.cpp
    src/ngx/DlssNrRuntimeAdapter.cpp
  )
  veyra_apply_warnings(veyra_ngx)
  target_compile_features(veyra_ngx PUBLIC cxx_std_20)
  target_link_libraries(veyra_ngx PUBLIC veyra_base veyra_gfx)
  target_include_directories(veyra_ngx PUBLIC
    "${VEYRA_DLSS_SDK_ROOT}/include")
  # NVIDIA ships the static core shim as MT-Release; the _dbg variants match
  # the MDd debug runtime. Pick per configuration to avoid LNK2038.
  target_link_libraries(veyra_ngx PRIVATE
    "$<IF:$<CONFIG:Debug>,${VEYRA_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib,${VEYRA_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>")
  target_compile_definitions(veyra_ngx PUBLIC
    VEYRA_EXPERIMENTAL_LOCAL_DLSSNR=1)
endif()

add_executable(veyra_runtime_probe
  tools/runtime_probe/main.cpp
)
veyra_apply_warnings(veyra_runtime_probe)
target_compile_features(veyra_runtime_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_runtime_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(veyra_runtime_probe PRIVATE veyra_gfx)

if(VEYRA_ENABLE_EXPERIMENTAL_DLSSNR)
  add_executable(veyra_nr_harness
    tools/nr_harness/main.cpp
  )
  veyra_apply_warnings(veyra_nr_harness)
  target_compile_features(veyra_nr_harness PRIVATE cxx_std_20)
  target_compile_definitions(veyra_nr_harness PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(veyra_nr_harness PRIVATE veyra_gfx veyra_ngx)
endif()
