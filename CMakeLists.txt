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

# ---------------------------------------------------------------------------
# Targets grow phase by phase; only what actually compiles is declared.
# ---------------------------------------------------------------------------
add_executable(veyra_runtime_probe
  tools/runtime_probe/main.cpp
)
veyra_apply_warnings(veyra_runtime_probe)
target_compile_features(veyra_runtime_probe PRIVATE cxx_std_20)
target_compile_definitions(veyra_runtime_probe PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
