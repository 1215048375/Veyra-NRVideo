#pragma once

// Every Feature 18 parameter name, centralized (Playbook sections 8.5/8.6).
// Types are encoded by the ParameterBlock setter used at the call site.

namespace veyra::ngx::dlssnr {

// --- Create parameters (uint32 via setU32 / int via setI32 / float via setF32)
constexpr const char* kWidth = "DLSSNR.Width";
constexpr const char* kHeight = "DLSSNR.Height";
constexpr const char* kInputWidth = "DLSSNR.InputWidth";
constexpr const char* kInputHeight = "DLSSNR.InputHeight";
constexpr const char* kOutputWidth = "DLSSNR.OutputWidth";
constexpr const char* kOutputHeight = "DLSSNR.OutputHeight";
constexpr const char* kOutputDotWidth = "DLSSNR.Output.Width";
constexpr const char* kOutputDotHeight = "DLSSNR.Output.Height";
constexpr const char* kUpscaling = "DLSSNR.Upscaling";
constexpr const char* kScale = "DLSSNR.Scale";
constexpr const char* kScalingRatio = "DLSSNR.ScalingRatio";
constexpr const char* kComputeScalingRatioCallback = "DLSSNRComputeScalingRatioCallback";
constexpr const char* kHintRenderPreset = "DLSSNR.Hint.Render.Preset";
constexpr const char* kStdWidth = "Width";
constexpr const char* kStdHeight = "Height";
constexpr const char* kPerfQualityValue = "PerfQualityValue";
constexpr const char* kCreationNodeMask = "CreationNodeMask";
constexpr const char* kVisibilityNodeMask = "VisibilityNodeMask";

// --- Evaluate resource parameters (ID3D12Resource* via setD3D12Resource)
constexpr const char* kColor = "DLSSNR.Color";
constexpr const char* kOutput = "DLSSNR.Output";
constexpr const char* kMVec = "DLSSNR.MVec";
constexpr const char* kDepth = "DLSSNR.Depth";

// --- Evaluate subrects (uint32 via setU32)
constexpr const char* kColorSubrectBaseX = "DLSSNR.ColorSubrectBaseX";
constexpr const char* kColorSubrectBaseY = "DLSSNR.ColorSubrectBaseY";
constexpr const char* kColorSubrectWidth = "DLSSNR.ColorSubrectWidth";
constexpr const char* kColorSubrectHeight = "DLSSNR.ColorSubrectHeight";
constexpr const char* kOutputSubrectBaseX = "DLSSNR.OutputSubrectBaseX";
constexpr const char* kOutputSubrectBaseY = "DLSSNR.OutputSubrectBaseY";
constexpr const char* kOutputSubrectWidth = "DLSSNR.OutputSubrectWidth";
constexpr const char* kOutputSubrectHeight = "DLSSNR.OutputSubrectHeight";
constexpr const char* kMVecSubrectBaseX = "DLSSNR.MVecSubrectBaseX";
constexpr const char* kMVecSubrectBaseY = "DLSSNR.MVecSubrectBaseY";
constexpr const char* kMVecSubrectWidth = "DLSSNR.MVecSubrectWidth";
constexpr const char* kMVecSubrectHeight = "DLSSNR.MVecSubrectHeight";
constexpr const char* kDepthSubrectBaseX = "DLSSNR.DepthSubrectBaseX";
constexpr const char* kDepthSubrectBaseY = "DLSSNR.DepthSubrectBaseY";
constexpr const char* kDepthSubrectWidth = "DLSSNR.DepthSubrectWidth";
constexpr const char* kDepthSubrectHeight = "DLSSNR.DepthSubrectHeight";

// --- Evaluate controls
constexpr const char* kMVecScaleX = "DLSSNR.MVecScaleX";
constexpr const char* kMVecScaleY = "DLSSNR.MVecScaleY";
constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
constexpr const char* kIndicatorInvertX = "DLSS.Indicator.Invert.X.Axis";
constexpr const char* kIndicatorInvertY = "DLSS.Indicator.Invert.Y.Axis";
constexpr const char* kEnabled = "DLSSNR.Enabled";
constexpr const char* kReset = "DLSSNR.Reset";
constexpr const char* kStyle = "DLSSNR.Style";
constexpr const char* kIntensity = "DLSSNR.Intensity";
constexpr const char* kLocalToneStrength = "DLSSNR.LocalToneStrength";
constexpr const char* kLocalStructureStrength = "DLSSNR.LocalStructureStrength";
constexpr const char* kSkinStructureStrength = "DLSSNR.SkinStructureStrength";
constexpr const char* kUseAutoMask = "DLSSNR.UseAutoMask";
constexpr const char* kUICorrection = "DLSSNR.UICorrection";

} // namespace veyra::ngx::dlssnr
