#include "AuditoryCortex.h"

#include <algorithm>
#include <cmath>
#include <iostream>

AuditoryCortex::AuditoryCortex()
{
	mRingBuffer.assign(kRingCapacity, 0);
}

AuditoryCortex::~AuditoryCortex()
{
	Stop();
	Clean();
}

bool AuditoryCortex::Init()
{
	IMMDeviceEnumerator* enumerator = nullptr;
	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (FAILED(hr) || !enumerator)
	{
		std::cout << "[AUDITORY] Init: MMDeviceEnumerator failed. hr=0x"
		          << std::hex << hr << std::endl;
		return false;
	}

	hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &mDevice);
	enumerator->Release();
	if (FAILED(hr) || !mDevice)
	{
		std::cout << "[AUDITORY] Init: GetDefaultAudioEndpoint(eCapture) failed." << std::endl;
		return false;
	}

	hr = mDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&mAudioClient);
	if (FAILED(hr) || !mAudioClient)
	{
		std::cout << "[AUDITORY] Init: IAudioClient activation failed." << std::endl;
		mDevice->Release(); mDevice = nullptr;
		return false;
	}

	WAVEFORMATEX* wfx = nullptr;
	mAudioClient->GetMixFormat(&wfx);
	mSampleRate    = (int)wfx->nSamplesPerSec;
	mNumChannels   = wfx->nChannels;
	mBitsPerSample = wfx->wBitsPerSample;

	hr = mAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, wfx, NULL);
	CoTaskMemFree(wfx);
	if (FAILED(hr))
	{
		std::cout << "[AUDITORY] Init: IAudioClient::Initialize failed. hr=0x"
		          << std::hex << hr << std::endl;
		mAudioClient->Release(); mAudioClient = nullptr;
		mDevice->Release();     mDevice      = nullptr;
		return false;
	}

	hr = mAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&mCaptureClient);
	if (FAILED(hr) || !mCaptureClient)
	{
		std::cout << "[AUDITORY] Init: IAudioCaptureClient failed." << std::endl;
		mAudioClient->Release(); mAudioClient = nullptr;
		mDevice->Release();     mDevice      = nullptr;
		return false;
	}

	std::cout << "[AUDITORY] Microphone ready: " << mNumChannels << " ch, "
	          << mSampleRate << " Hz, " << mBitsPerSample << " bps." << std::endl;
	return true;
}

void AuditoryCortex::Start()
{
	if (mRunning.load() || !mAudioClient) return;
	mAudioClient->Start();
	mRunning.store(true);
	mCaptureThread = std::thread(&AuditoryCortex::CaptureLoop, this);
}

void AuditoryCortex::Stop()
{
	if (!mRunning.load()) return;
	mRunning.store(false);
	if (mCaptureThread.joinable())
		mCaptureThread.join();
	if (mAudioClient)
		mAudioClient->Stop();
}

void AuditoryCortex::Clean()
{
	if (mCaptureClient) { mCaptureClient->Release(); mCaptureClient = nullptr; }
	if (mAudioClient)   { mAudioClient->Release();   mAudioClient   = nullptr; }
	if (mDevice)        { mDevice->Release();         mDevice        = nullptr; }
}

// ---------------------------------------------------------------------------
// Capture loop — runs on the dedicated thread
// ---------------------------------------------------------------------------
void AuditoryCortex::CaptureLoop()
{
	CoInitialize(NULL); // Each thread needs its own COM context

	constexpr DWORD kSleepMs  = 10;
	constexpr float kRMSDecay = 0.85f; // per-packet EMA smoothing

	while (mRunning.load())
	{
		Sleep(kSleepMs);

		UINT32 packetSize = 0;
		HRESULT hr = mCaptureClient->GetNextPacketSize(&packetSize);
		if (FAILED(hr)) break;

		while (packetSize > 0)
		{
			BYTE*  data   = nullptr;
			UINT32 frames = 0;
			DWORD  flags  = 0;

			hr = mCaptureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
			if (FAILED(hr)) { packetSize = 0; break; }

			if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
			{
				float sumSq = 0.0f;
				std::vector<int16_t> mono(frames);

				if (mBitsPerSample == 32)
				{
					// WASAPI shared-mode default: IEEE float32 interleaved
					const float* fSrc = reinterpret_cast<const float*>(data);
					for (UINT32 i = 0; i < frames; ++i)
					{
						float mix = 0.0f;
						for (UINT32 ch = 0; ch < mNumChannels; ++ch)
							mix += fSrc[i * mNumChannels + ch];
						mix /= static_cast<float>(mNumChannels);
						sumSq += mix * mix;
						mono[i] = static_cast<int16_t>(
							std::clamp(mix * 32767.0f, -32768.0f, 32767.0f));
					}
				}
				else
				{
					// 16-bit PCM interleaved
					const int16_t* iSrc = reinterpret_cast<const int16_t*>(data);
					for (UINT32 i = 0; i < frames; ++i)
					{
						int32_t mix = 0;
						for (UINT32 ch = 0; ch < mNumChannels; ++ch)
							mix += iSrc[i * mNumChannels + ch];
						mono[i] = static_cast<int16_t>(
							std::clamp(mix / static_cast<int32_t>(mNumChannels), -32768, 32767));
						float n = mono[i] / 32768.0f;
						sumSq += n * n;
					}
				}

				{
					std::lock_guard<std::mutex> lk(mBufMutex);
					for (int16_t s : mono)
					{
						mRingBuffer[mWriteHead] = s;
						mWriteHead = (mWriteHead + 1) % kRingCapacity;
					}
				}

				float rms  = (frames > 0) ? std::sqrt(sumSq / static_cast<float>(frames)) : 0.0f;
				float prev = mRMSLevel.load();
				mRMSLevel.store(prev * kRMSDecay + rms * (1.0f - kRMSDecay));
			}
			else
			{
				mRMSLevel.store(mRMSLevel.load() * kRMSDecay); // decay during silence
			}

			mCaptureClient->ReleaseBuffer(frames);
			mCaptureClient->GetNextPacketSize(&packetSize);
		}
	}

	CoUninitialize();
}

size_t AuditoryCortex::PeekPCM(int16_t* dst, size_t maxSamples) const
{
	std::lock_guard<std::mutex> lk(mBufMutex);
	size_t count = maxSamples < kRingCapacity ? maxSamples : kRingCapacity;
	for (size_t i = 0; i < count; ++i)
	{
		size_t idx = (mWriteHead + kRingCapacity - count + i) % kRingCapacity;
		dst[i] = mRingBuffer[idx];
	}
	return count;
}
