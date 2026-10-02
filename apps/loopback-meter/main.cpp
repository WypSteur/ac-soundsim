// Read-only output diagnostic. No microphone, playback, recording file or upload.
// Microsoft WASAPI shared-mode loopback, default render endpoint, scalar metrics.
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr) {if(FAILED(hr)) throw std::runtime_error("WASAPI HRESULT="+std::to_string(static_cast<unsigned long>(hr)));}
int main() {
    try {
        checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(enumerator.GetAddressOf())));
        ComPtr<IMMDevice> device;
        checked(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device));
        ComPtr<IAudioClient> client;
        checked(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
        WAVEFORMATEX* format{}; checked(client->GetMixFormat(&format));
        struct FreeFormat {WAVEFORMATEX* p; ~FreeFormat(){CoTaskMemFree(p);}} freeFormat{format};
        const bool floating=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT ||
            (format->wFormatTag==WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if(!((floating && format->wBitsPerSample==32) || (!floating && (format->wBitsPerSample==16 || format->wBitsPerSample==32))))
            throw std::runtime_error("unsupported endpoint format");
        checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,10000000,0,format,nullptr));
        ComPtr<IAudioCaptureClient> capture;
        checked(client->GetService(IID_PPV_ARGS(capture.GetAddressOf())));
        checked(client->Start());
        std::uint64_t count=0; double energy=0,peak=0,real=0,imaginary=0;
        const auto start=std::chrono::steady_clock::now();
        while(std::chrono::steady_clock::now()-start<std::chrono::seconds(2)) {
            UINT32 available{}; checked(capture->GetNextPacketSize(&available));
            while(available) {
                BYTE* data{}; UINT32 frames{}; DWORD flags{};
                checked(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr));
                for(UINT32 i=0;i<frames;++i) {
                    double mono=0;
                    for(UINT32 ch=0;ch<format->nChannels;++ch) {
                        double sample=0;
                        if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) {
                            const auto item=i*format->nChannels+ch;
                            sample=floating ? reinterpret_cast<float*>(data)[item] : format->wBitsPerSample==16
                                ? reinterpret_cast<short*>(data)[item]/32768.0 : reinterpret_cast<long*>(data)[item]/2147483648.0;
                        }
                        mono+=sample/format->nChannels;
                    }
                    energy+=mono*mono; peak=std::max(peak,std::abs(mono));
                    const double phase=count*(2*3.14159265358979323846*440/format->nSamplesPerSec);
                    real+=mono*std::cos(phase); imaginary+=mono*std::sin(phase); ++count;
                }
                checked(capture->ReleaseBuffer(frames)); checked(capture->GetNextPacketSize(&available));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        checked(client->Stop());
        if(!count) throw std::runtime_error("no loopback frames");
        std::cout.precision(10);
        std::cout<<"endpointRate="<<format->nSamplesPerSec<<" channels="<<format->nChannels<<" frames="<<count
            <<" rms="<<std::sqrt(energy/count)<<" peak="<<peak<<" amplitude440Hz="<<2*std::hypot(real,imaginary)/count<<'\n';
        // Includes any other application playing to this endpoint. No acoustic
        // claims without a matching SoundSim mute/unmute comparison.
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
