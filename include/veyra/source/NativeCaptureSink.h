#pragma once
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <functional>
namespace veyra::source {
// Native terminal filter: no legacy SampleGrabber orientation/VideoInfo2
// restriction, conversion filter or extra queue. Callback borrows the sample.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
HRESULT createNativeAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Advisory request on the upstream output pin, before ConnectDirect. Failure
// does not invalidate the device; callers retain the compatible connection.
HRESULT suggestCaptureAudioBuffering(IPin*,const WAVEFORMATEX&);
}
