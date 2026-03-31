#include "VoiceOutputEngine.h"
#include <iostream>

VoiceOutputEngine::VoiceOutputEngine() : pVoice(nullptr), mSampleRate(0), comInitialized(false)
{
	HRESULT hr = CoInitialize(NULL); // Initialize COM library
	if (FAILED(hr))
	{
		std::cout << "[VOICE] CoInitialize failed. hr = 0x" << std::hex << hr << std::endl;
		pVoice = nullptr; // Ensure pVoice is null if COM initialization fails
		return;
	}
	comInitialized = true;

	hr = CoCreateInstance(
		CLSID_SpVoice,
		NULL,
		CLSCTX_ALL,
		IID_ISpVoice,
		(void**)&pVoice
	);

	if (FAILED(hr))
	{
		std::cout << "[VOICE] CocreateInstance(CLSID_SpVoice) FAILED. hr = 0x" << std::hex << hr << std::endl;
		pVoice = nullptr; // Ensure pVoice is null if voice creation fails
	}

	InitAudio(); // Initialise persistent WASAPI audio client once at startup
}

VoiceOutputEngine::~VoiceOutputEngine()
{
	Clean(); // Clean up resources when the voice output engine is destroyed
}

void VoiceOutputEngine::Clean()
{
	if (mAudioClient)
	{
		mAudioClient->Stop();
		mAudioClient->Reset();
	}
	if (mRenderClient)
	{
		mRenderClient->Release();
		mRenderClient = nullptr;
	}
	if (mAudioClient)
	{
		mAudioClient->Release();
		mAudioClient = nullptr;
	}
	if (mAudioDevice)
	{
		mAudioDevice->Release();
		mAudioDevice = nullptr;
	}
	mBufferFrameCount = 0;
	mNumChannels      = 0;

	if (pVoice)
	{
		pVoice->Release(); // Release the SAPI voice interface
		pVoice = nullptr; // Set pVoice to null after releasing
	}
	if (comInitialized)
	{
		CoUninitialize(); // Uninitialize COM library only if this instance owns the call
		comInitialized = false;
	}
}

bool VoiceOutputEngine::InitAudio()
{
	IMMDeviceEnumerator* enumerator = nullptr;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (FAILED(hr) || !enumerator)
	{
		std::cout << "[VOICE] InitAudio: MMDeviceEnumerator failed. hr = 0x" << std::hex << hr << std::endl;
		return false;
	}

	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &mAudioDevice);
	enumerator->Release();
	if (FAILED(hr) || !mAudioDevice)
	{
		std::cout << "[VOICE] InitAudio: GetDefaultAudioEndpoint failed." << std::endl;
		return false;
	}

	hr = mAudioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&mAudioClient);
	if (FAILED(hr) || !mAudioClient)
	{
		std::cout << "[VOICE] InitAudio: IAudioClient activation failed." << std::endl;
		mAudioDevice->Release(); mAudioDevice = nullptr;
		return false;
	}

	WAVEFORMATEX* wfx = nullptr;
	mAudioClient->GetMixFormat(&wfx);
	mSampleRate  = (int)wfx->nSamplesPerSec;
	mNumChannels = wfx->nChannels;
	std::cout << "[VOICE] Audio device: " << wfx->nChannels << " ch, "
			  << wfx->nSamplesPerSec << " Hz, " << wfx->wBitsPerSample << " bps." << std::endl;

	hr = mAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, wfx, NULL);
	CoTaskMemFree(wfx);
	if (FAILED(hr))
	{
		std::cout << "[VOICE] InitAudio: IAudioClient::Initialize failed. hr = 0x" << std::hex << hr << std::endl;
		mAudioClient->Release(); mAudioClient = nullptr;
		mAudioDevice->Release(); mAudioDevice = nullptr;
		return false;
	}

	mAudioClient->GetBufferSize(&mBufferFrameCount);

	hr = mAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&mRenderClient);
	if (FAILED(hr) || !mRenderClient)
	{
		std::cout << "[VOICE] InitAudio: IAudioRenderClient failed." << std::endl;
		mAudioClient->Release(); mAudioClient = nullptr;
		mAudioDevice->Release(); mAudioDevice = nullptr;
		return false;
	}

	return true;
}

double VoiceOutputEngine::SpeakTimed(const std::wstring& text)
{
	if (!pVoice)
		return 0.0; // Return 0 if the voice engine is not initialized
	LARGE_INTEGER freq, start, end;
	QueryPerformanceFrequency(&freq); // Get the frequency of the high-resolution performance counter
	QueryPerformanceCounter(&start); // Get the starting timestamp

	// Speak the text synchronously to measure the time taken for speech output
	HRESULT hr = pVoice->Speak(text.c_str(), SPF_DEFAULT, NULL); // Speak the text synchronously

	QueryPerformanceCounter(&end); // Get the ending timestamp

	if (FAILED(hr))
	{
		std::cout << "[VOICE] Speech failed. hr = 0x" << std::hex << hr << std::endl;
		return 0.0; // Return 0 if speech failed
	}

	double seconds = double(end.QuadPart - start.QuadPart) / double(freq.QuadPart); // Calculate the time taken for speech output in seconds
	return seconds; // Return the time taken for speech output
}

void VoiceOutputEngine::PlayPCM(const short* samples, size_t count)
{
	if (!mAudioClient || !mRenderClient || count == 0) return;

	mAudioClient->Start();

	const short* src = samples;
	size_t remaining = count;
	while (remaining > 0)
	{
		UINT32 padding = 0;
		mAudioClient->GetCurrentPadding(&padding);
		UINT32 available = mBufferFrameCount - padding;
		if (available == 0)
		{
			Sleep(1);
			continue;
		}

		UINT32 toWrite = (UINT32)(remaining < (size_t)available ? remaining : (size_t)available);
		BYTE* data = nullptr;
		HRESULT hr = mRenderClient->GetBuffer(toWrite, &data);
		if (FAILED(hr) || data == nullptr)
			break;

		float* floatData = reinterpret_cast<float*>(data);
		for (UINT32 i = 0; i < toWrite; i++)
		{
			float sample = src[i] / 32768.0f;
			for (UINT32 ch = 0; ch < mNumChannels; ch++)
				floatData[i * mNumChannels + ch] = sample;
		}
		mRenderClient->ReleaseBuffer(toWrite, 0);
		src += toWrite;
		remaining -= toWrite;
	}

	// Drain: wait for the hardware to finish playing
	UINT32 padding = 0;
	do {
		Sleep(1);
		mAudioClient->GetCurrentPadding(&padding);
	} while (padding > 0);

	mAudioClient->Stop();
	mAudioClient->Reset(); // Flush buffer so the client is ready for the next call
}

void VoiceOutputEngine::ListVoices()
{
	IEnumSpObjectTokens* cpEnum = nullptr;
	ULONG count = 0;

	SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum); // Enumerate available voices in the SAPI voice engine
	cpEnum->GetCount(&count); // Get the count of available voices

	for (ULONG i = 0; i < count; i++)
	{
		ISpObjectToken* cpVoiceToken = nullptr;
		cpEnum->Next(1, &cpVoiceToken, NULL); // Get the next voice token

		LPWSTR description = nullptr;
		SpGetDescription(cpVoiceToken, &description);

		wprintf(L"Voice %u: %s\n", i, description); // Print the description of the voice

		CoTaskMemFree(description); // Free the memory allocated for the description
		cpVoiceToken->Release(); // Release the voice token
	}
	cpEnum->Release(); // Release the enumerator
}

VoiceOutputEngine::VoiceOutputEngine(VoiceOutputEngine&& other) noexcept
	: pVoice(other.pVoice), mSampleRate(other.mSampleRate), comInitialized(other.comInitialized),
	  mAudioDevice(other.mAudioDevice), mAudioClient(other.mAudioClient),
	  mRenderClient(other.mRenderClient), mBufferFrameCount(other.mBufferFrameCount),
	  mNumChannels(other.mNumChannels)
{
	other.pVoice          = nullptr;
	other.comInitialized  = false;
	other.mAudioDevice    = nullptr;
	other.mAudioClient    = nullptr;
	other.mRenderClient   = nullptr;
	other.mBufferFrameCount = 0;
	other.mNumChannels    = 0;
}

VoiceOutputEngine& VoiceOutputEngine::operator=(VoiceOutputEngine&& other) noexcept
{
	if (this != &other)
	{
		Clean();
		pVoice            = other.pVoice;
		mSampleRate       = other.mSampleRate;
		comInitialized    = other.comInitialized;
		mAudioDevice      = other.mAudioDevice;
		mAudioClient      = other.mAudioClient;
		mRenderClient     = other.mRenderClient;
		mBufferFrameCount = other.mBufferFrameCount;
		mNumChannels      = other.mNumChannels;

		other.pVoice          = nullptr;
		other.comInitialized  = false;
		other.mAudioDevice    = nullptr;
		other.mAudioClient    = nullptr;
		other.mRenderClient   = nullptr;
		other.mBufferFrameCount = 0;
		other.mNumChannels    = 0;
	}
	return *this;
}