# Local development dependencies

NVIDIA DLSS/Optical Flow SDKs and experimental DLSSNR runtime: external local assets, not committed or redistributed. Existing SDK agreements remain applicable. ReShade/RenoDX addon is never loaded by the product.

## NVENC API declarations

Source: https://github.com/FFmpeg/nv-codec-headers ; checkout `eddcea9e27f6b772057c9b3f87de2cc1737faffc` (SDK 13.1.15 declarations), stored only under ignored `third_party_local/nvidia/nv-codec-headers`.

The nvEncodeAPI.h header itself has NVIDIA's permissive MIT-style notice (Copyright 2010–2026 NVIDIA Corporation). Its full notice is retained unmodified in that header. This permits using the declarations without retrieving the full developer-portal sample package; it does not grant rights to distribute NVIDIA driver/runtime binaries. Veyra uses the system NVENC library, never copies it into a package. Implementation is independently authored against these declarations; no competitor/sample implementation copied.

FFmpeg build: external `C:/veyra-deps/installed/x64-windows`; public distribution requires review of actual configuration and linked components. No license-complete installer has been produced.

## Lucide UI icons

Source: https://github.com/lucide-icons/lucide/tree/a537cb6eb323b885f4c60baf3cec1a995982d167

24 native icon mappings use 23 Lucide SVGs, retained under `assets/icons/lucide/`. Lucide is ISC-licensed (Copyright 2026 Lucide Icons and Contributors); its Feather-derived subset is MIT-licensed (Copyright 2013-present Cole Bemis). The complete upstream notices are preserved in `assets/icons/lucide/LICENSE` and must accompany any distribution containing these icons.

`assets/icons/lucide/manifest.json` records the pinned revision and per-file SHA256. `scripts/generate-lucide-icons.py` converts the SVG geometry to GDI+ paths in `apps/veyra/ui/LucideIcons.h`, using development-only fonttools 4.64.0. Veyra requires no fonttools, network request, icon font or external icon runtime.

## RTX Video SDK 1.1 local VSR adapter

User-provided official SDK from https://developer.nvidia.com/rtx-video-sdk/getting-started, kept under ignored third_party_local/nvidia/RTX_Video_SDK_1.1.0. Its NVIDIA RTX SDK license remains applicable. VideoSrBackend is independently authored against the documented VSR parameter ABI; no SDK sample implementation or proprietary header is copied into the repository. Local nvngx_vsr.dll remains excluded from Git. Under the user-authorized 2026-09-09 Release Runtime Pack policy, its pinned signed release copy may be included only as a Release asset alongside the applicable SDK license and manifest; it is never committed to source control.

## NVIDIA FRUC local adapter

Local Optical Flow SDK5.0.7 NvOFFRUC interface and CUDA declarations are consumed from ignored third_party_local, under the NVIDIA SDK terms. FrucBackend/FrucWorker are independently authored code; no sample or competitor implementation is copied. The locally provided signed NvOFFRUC.dll is never committed to Git. Under the user-authorized 2026-09-09 Release Runtime Pack policy, only its pinned signed release copy may be included as an experimental Release asset with a manifest and the applicable license. Veyra relies on the user-installed NVIDIA driver for nvcuda.dll; it does not copy that driver DLL. The build-produced veyra_fruc_worker.exe is Veyra code, not a renamed proprietary binary. FRUC SDK guide: https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html . These additions do not grant NVIDIA runtime distribution rights.
