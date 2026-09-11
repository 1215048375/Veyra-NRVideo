# Third-party dependencies

NVIDIA SDKs and runtimes are excluded from source control. The publisher-authorized experimental Release package contains only selected runtime DLLs and applicable notices, as documented in `docs/RUNTIME_COMPONENTS_0.0.2.md`. This is not vendor endorsement or a general redistribution grant. ReShade/RenoDX add-ons are not loaded or distributed by the product.

## NVENC API declarations

Source: https://github.com/FFmpeg/nv-codec-headers ; checkout `eddcea9e27f6b772057c9b3f87de2cc1737faffc` (SDK 13.1.15 declarations), stored only under ignored `third_party_local/nvidia/nv-codec-headers`.

The nvEncodeAPI.h header itself has NVIDIA's permissive MIT-style notice (Copyright 2010–2026 NVIDIA Corporation). Its full notice is retained unmodified in that header. This permits using the declarations without retrieving the full developer-portal sample package; it does not grant rights to distribute NVIDIA driver/runtime binaries. Veyra uses the system NVENC library, never copies it into a package. Implementation is independently authored against these declarations; no competitor/sample implementation copied.

FFmpeg: dynamically linked 9.0.1#1 vcpkg build. The portable package carries five FFmpeg DLLs, the complete copyright/license notices and SPDX provenance. Corresponding upstream source and the vcpkg patch/build recipe are listed in `docs/BUILD.md`. No FFmpeg command-line executable or test-media toolchain is shipped.

## Intel XeSS / XeLL

Official XeSS SDK 3.0.2. Veyra loads `libxess_fg.dll` and `libxell.dll` for experimental preview frame generation. Unmodified binaries may be redistributed under the Intel Simplified Software License; the complete license and `third-party-programs.txt` accompany the package. User DLL replacement is allowed by Veyra without fixed identity locks; compatibility is not guaranteed.

## AMD FidelityFX Optical Flow

FidelityFX SDK 1.1.4, upstream commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK . The optical-flow and DX12 backend libraries are statically linked. Copyright (C) 2024 Advanced Micro Devices, Inc.; MIT license, reproduced in the package's `licenses/AMD_FIDELITYFX_LICENSE.txt`. This is optical flow, not AMD NR or AMD super resolution.

## Microsoft Visual C++ Runtime

The package includes unmodified x64 `vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll` from the Visual Studio 2022 C++ Redistributables directory for FFmpeg and XeSS. These are Microsoft Distributable Code under the [Visual Studio software terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/); Windows system and GPU driver DLLs are not copied. Veyra itself uses the static MSVC runtime.

## Lucide UI icons

Source: https://github.com/lucide-icons/lucide/tree/a537cb6eb323b885f4c60baf3cec1a995982d167

24 native icon mappings use 23 Lucide SVGs, retained under `assets/icons/lucide/`. Lucide is ISC-licensed (Copyright 2026 Lucide Icons and Contributors); its Feather-derived subset is MIT-licensed (Copyright 2013-present Cole Bemis). The complete upstream notices are preserved in `assets/icons/lucide/LICENSE` and must accompany any distribution containing these icons.

`assets/icons/lucide/manifest.json` records the pinned revision and per-file SHA256. `scripts/generate-lucide-icons.py` converts the SVG geometry to GDI+ paths in `apps/veyra/ui/LucideIcons.h`, using development-only fonttools 4.64.0. Veyra requires no fonttools, network request, icon font or external icon runtime.

## RTX Video SDK 1.1 local VSR adapter

User-provided official SDK from https://developer.nvidia.com/rtx-video-sdk/getting-started, kept under ignored third_party_local/nvidia/RTX_Video_SDK_1.1.0. Its NVIDIA RTX SDK license remains applicable. VideoSrBackend is independently authored against the documented VSR parameter ABI; no SDK sample implementation or proprietary header is copied into the repository. Local nvngx_vsr.dll remains excluded from Git. Under the user-authorized 2026-09-09 Release Runtime Pack policy, its pinned signed release copy may be included only as a Release asset alongside the applicable SDK license and manifest; it is never committed to source control.

## NVIDIA FRUC

FRUC was evaluated during development and has been removed from the product. No FRUC runtime, worker, SDK headers, or binaries are built or packaged.

## PS5 Remote Play / chiaki-ng (local integration)

The optional `VEYRA_ENABLE_REMOTEPLAY` build compiles chiaki-ng at
`0e16950165f06e5c3291537c2eeba6e852be7120` from https://github.com/streetpea/chiaki-ng,
with the two reviewed patches in `scripts/remoteplay/patches/`. Chiaki code and
these derived patches retain AGPL-3.0-only with the upstream OpenSSL exception;
see `licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt`. The metadata patch was adapted
from the user-supplied Code 01 change description and checked against this pin.
No Chiaki binary or third-party SDK is committed. A future distribution of the
combined Remote Play executable must provide its corresponding complete source,
these patches, dependency pins, build instructions and upstream notices; the
existing Veyra GPL file alone is not the combined program's license record.

Gamepad input uses SDL 3.4.14 (https://github.com/libsdl-org/SDL/tree/release-3.4.14),
statically built using vcpkg. SDL provides DualSense and other controller device
support; Veyra maps its public gamepad API to Chiaki semantic input. No SDL audio
or video playback path is used. Retained notices: `licenses/remoteplay/SDL3_NOTICES.txt`.
This local integration has not been pushed or released.
