veyra_find_dxc(DIS_DXC)
set(dis_outputs "")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_gray.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_gray.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_gray.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_gray.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_gray.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_rgba_gray.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_rgba_gray.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_rgba_gray.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_rgba_gray.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_rgba_gray.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_luma.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_luma.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_luma.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_native_luma.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_native_luma.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_area.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_area.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_area.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_area.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_area.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d2_gradient.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d2_gradient.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d2_gradient.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d2_gradient.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d2_gradient.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_hor.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_hor.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_ver.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_ver.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv2.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv2.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_deriv2.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_deriv2.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv2.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_update.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_update.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_update.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_update.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_update.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_zero.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_zero.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_zero.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_zero.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_zero.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor_stride3.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -D DIS_TENSOR_STRIDE=3 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor_stride3.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_hor.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_hor.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_hor_stride3.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver_stride3.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -D DIS_TENSOR_STRIDE=3 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver_stride3.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_ver.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_d3_structure_ver.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_d3_structure_ver_stride3.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_patch_parallel.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -D DIS_SINGLE_PATCH_GROUP=1 -D DIS_ROUNDED_DIVIDE=1 -D DIS_PARALLEL_STRIPES=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_patch_parallel.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_patch_exact.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_patch_exact.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_patch_parallel.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_densify_exact.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -D DIS_DENSE_EXACT=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_densify_exact.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_densify.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_densify.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_densify_exact.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv1_cpu.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Gis -D DIS_VR_CPU_WARP=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv1_cpu.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_deriv1.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_deriv1.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_deriv1_cpu.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_system_exact.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Gis -D DIS_VR_EXACT=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_system_exact.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_system.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_system.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_system_exact.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_sor_exact.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Gis -D DIS_VR_EXACT=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_sor_exact.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_sor.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_vr_sor.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_vr_sor_exact.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_upsample_ipp.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -D DIS_IPP_UPSAMPLE=1 -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_upsample_ipp.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_upsample.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_upsample.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_upsample_ipp.dxil")
add_custom_command(
 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_final.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis"
 COMMAND "${DIS_DXC}" -nologo -T cs_6_0 -E main -Gis -Fo "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_final.dxil" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_final.hlsl"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_perfect_final.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpu-dis/src/gpu/shaders/dis_cpu_rounding.hlsli"
 VERBATIM)
list(APPEND dis_outputs "${CMAKE_CURRENT_BINARY_DIR}/shaders/dis/dis_perfect_final.dxil")
add_custom_target(veyra_shader_gpu_dis DEPENDS ${dis_outputs})
