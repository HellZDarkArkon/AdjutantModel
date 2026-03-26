#include "VoiceOutputEngine.h"
#include <iostream>

VoiceOutputEngine::VoiceOutputEngine() : pVoice(nullptr)
{
	HRESULT hr = CoInitialize(NULL); // Initialize COM library
	if (FAILED(hr))
	{
		std::cout << "[VOICE] CoInitialize failed. hr = 0x" << std::hex << hr << std::endl;
		pVoice = nullptr; // Ensure pVoice is null if COM initialization fails
		return;
	}

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

void VoiceOutputEngine::PlayPCM(const float* samples, size_t count)
{
	// Placeholder for playing raw PCM audio data through the voice engine
	// Implementing this would require creating a custom audio stream and feeding it to SAPI
	std::cout << "[VOICE] PlayPCM is not implemented yet." << std::endl;
}
