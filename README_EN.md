# Veyra

English | [简体中文](README.md)

<p align="center">
  <a href="https://github.com/Likely7/Veyra-NRVideo/blob/main/REAMDE%20MP4.mp4">
    <img src="assets/readme-demo.gif" alt="Veyra demo video" width="960">
  </a>
</p>

<p align="center">The demo plays automatically; click it to open the original MP4.</p>

A Windows video player and capture-card enhancement tool. Play videos, process images, and preview capture devices with optional super resolution, NR enhancement, and frame generation.

[Download 0.0.4 Portable](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v0.0.4) · [Release Notes](docs/RELEASE_NOTES_0.0.4.md) · [Report an Issue](https://github.com/Likely7/Veyra-NRVideo/issues)

## Features

| Feature | Options |
| --- | --- |
| Playback and capture | H.264 / HEVC video, PNG / JPEG images, DirectShow / UVC capture cards |
| Super resolution | DLSS SR and RTX Video SR; 1440p / 4K / 8K targets with aspect ratio preserved |
| NR enhancement | Experimental NVIDIA NR; realtime and native modes, style, intensity, and region protection |
| Frame generation | DLSS 2X / 3X / 4X; experimental XeSS 2X preview |
| Controls | Live settings, restore defaults, original/processed comparison, split view, preview zoom |
| Export | PNG / JPEG images; NVENC H.264 / HEVC video |
| Diagnostics | Completed source/generated frames, presentation submissions, stage timings, software latency |

Daily mode focuses on watching. Professional mode expands the controls, diagnostics, and export tools without reopening the video. The application currently uses a Chinese interface.

Version 0.0.4 fixes audio/video drift caused by enhancement processing, audio interruptions during small NR timing fluctuations, and audio scheduling with XeSS frame generation.

### Development branch: PS5 Remote Play

The development branch adds LAN PS5 streaming, pairing, encrypted profiles and gamepad input through the existing enhancement pipeline. Console acceptance is pending. **This is not included in the published 0.0.4 Release.** See the [local test guide and scope](docs/REMOTEPLAY_PS5_ACCEPTANCE_2026-09-11.md).

## Download and Run

1. Download **Veyra-0.0.4-win64-portable.zip** from [Releases](https://github.com/Likely7/Veyra-NRVideo/releases). The Source code archives are for developers.
2. Extract the entire archive into a writable directory and run **Veyra.exe**. No SDK, Python, or development tools are needed.
3. Use a current GPU driver. NVIDIA NR, DLSS, RTX Video SR, and NVENC require compatible NVIDIA RTX hardware; this version was primarily tested on an RTX 5070.

Requirements: Windows 11 x64 and DirectX 12. Required application runtimes are included; GPU and capture-device drivers are supplied by the system. 8K upscaling and native-resolution enhancement need more VRAM and may not run in real time.

To upgrade, close the old version and extract into a new directory. To retain settings, copy the old `runtime_local/*.v1` files and `veyra.ini`. Do not overwrite the new runtime directories with the old ones.

## Quick Guide

### Videos and Images

Choose Open on the bottom bar. Playback, seeking, volume, subtitles, and fullscreen are available there. In Professional mode, use the mouse wheel over the picture to zoom.

### Capture Cards

1. Connect the device and close other applications using the same capture card.
2. Choose Capture, then select the device, resolution, frame rate, pixel format, and audio input.
3. Confirm the picture with enhancement disabled, then enable NR, upscaling, or frame generation. Try YUY2 / NV12 when the device offers the same desired mode.
4. For a 30fps console game carried over 60fps capture, select the 60-to-30 content cadence setting in Professional mode. Keep the original cadence for actual 60fps content.

### Enhancement and Frame Generation

Enable NR, super resolution, and frame generation independently in Professional mode. Select the upscaling method and target size beneath the super-resolution switch. The frame-generation page offers DLSS / XeSS and the supported multipliers.

Start with realtime NR, a lower RTX Video SR quality, and 2X frame generation. Compare the image and watch the diagnostics. Reduce quality, multiplier, or target size if processing falls behind. Software latency measures capture callback to Present return, not complete capture-to-display latency. Frame generation does not reduce game input latency.

Audio synchronization follows the software processing chain without counting the capture card's shared input delay twice. Small timing fluctuations no longer stop audio on each frame. Sustained GPU overload can still require waiting for video; lower the enhancement workload if that happens.

### Export and Runtime Replacement

Use Professional mode to save an image or export a video, then choose the format and destination. XeSS is preview-only; supported video frame-generation export uses DLSS.

You may replace DLLs while Veyra is closed. NVIDIA components belong in `runtime/experimental/`; XeSS / XeLL belong in `runtime_local/intel/experimental/`. Keep the filenames. Veyra does not enforce hash or signature locks; manifests describe the shipped files only. Replacement versions may have incompatible APIs or hardware requirements. To uninstall, close Veyra and delete its extracted directory.

### NR Runtime Selection

In Professional mode, open Enhancement and select the NR runtime. Choose the NVIDIA original or the community RTX 40/50 experimental variant. Switching briefly interrupts playback; failed changes restore the previous configuration. The community DLL is included in `runtime/experimental/nr-community/`, separate from the original. It is a modified binary with Authenticode status `HashMismatch`, not a valid NVIDIA-signed original. Both variants have been tested on an RTX 5070; RTX 40 testing is pending.

### OBS Streaming and Recording

Add a **Window Capture** source, select Veyra, and explicitly set **Capture Method** to **Windows 10 (1903 and up)**. Automatic may choose BitBlt, capturing the controls but missing the GPU-rendered video. Switching to the Windows capture method restored video in the reported local test.

Veyra's experimental broadcast compatibility switch only changes the presentation swapchain; it does not fix BitBlt capture. Leave it off unless testing a specific capture issue. In other recording applications, prefer Windows Graphics Capture / WGC. Compatibility with every recorder and the capture cadence of generated frames have not been verified.

## Technical Approach and Limits

```text
Video / image / capture card -> color handling -> SR -> NR -> frame generation -> display / export
```

C++20, Win32, and D3D12. FFmpeg handles media files, DirectShow handles capture, and NVENC handles video encoding. Inputs share one enhancement graph; capture retains the latest frame, and optical flow supplies estimated motion.

NR and DLSS frame generation are **community-experimental integrations**, not NVIDIA certification or complete native game integration. Captured pixels lack game-engine depth and motion data; ghosting and altered detail are possible. AMD NR is unavailable, FRUC has been removed, and HDR / AV1 / ProRes export are unsupported. Capture compatibility and long-term stability remain under testing.

## Development and License

[Build Instructions](docs/BUILD.md) · [Runtime Components](docs/RUNTIME_COMPONENTS_0.0.4.md) · [Third-Party Notices](THIRD_PARTY_NOTICES.md)

Source code is licensed under [GPLv3](LICENSE). SDKs, models, and runtimes are excluded from this source repository. Release components retain their separate licenses and experimental distribution boundaries.
