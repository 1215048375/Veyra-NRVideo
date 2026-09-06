# Local development dependencies

NVIDIA DLSS/Optical Flow SDKs and experimental DLSSNR runtime: external local assets, not committed or redistributed. Existing SDK agreements remain applicable. ReShade/RenoDX addon is never loaded by the product.

## NVENC API declarations

Source: https://github.com/FFmpeg/nv-codec-headers ; checkout `eddcea9e27f6b772057c9b3f87de2cc1737faffc` (SDK 13.1.15 declarations), stored only under ignored `third_party_local/nvidia/nv-codec-headers`.

The nvEncodeAPI.h header itself has NVIDIA's permissive MIT-style notice (Copyright 2010–2026 NVIDIA Corporation). Its full notice is retained unmodified in that header. This permits using the declarations without retrieving the full developer-portal sample package; it does not grant rights to distribute NVIDIA driver/runtime binaries. Veyra uses the system NVENC library, never copies it into a package. Implementation is independently authored against these declarations; no competitor/sample implementation copied.

FFmpeg build: external `C:/veyra-deps/installed/x64-windows`; public distribution requires review of actual configuration and linked components. No license-complete installer has been produced.
