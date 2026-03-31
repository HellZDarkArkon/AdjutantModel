#ifndef VOICE_IO_H
#define VOICE_IO_H

#include <string>
#include <sapi.h>
#include <sphelper.h>

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <comdef.h>

#include "VoiceDSP.h"

class VoiceOutputEngine // Setup Vocal Output Engine using Microsoft Speech API (SAPI)
{
public:
	VoiceOutputEngine();
	~VoiceOutputEngine();

	VoiceOutputEngine(const VoiceOutputEngine&)            = delete;
	VoiceOutputEngine& operator=(const VoiceOutputEngine&) = delete;

	VoiceOutputEngine(VoiceOutputEngine&& other) noexcept;
	VoiceOutputEngine& operator=(VoiceOutputEngine&& other) noexcept;

	void Clean(); // Clean up resources used by the voice output engine
	bool InitAudio(); // Initialise the persistent WASAPI audio client (called once after construction)

	double SpeakTimed(const std::wstring& text); // Speak a line of text using the voice engine

	void PlayPCM(const short* samples, size_t count); // Play raw PCM audio data through the voice engine

	void ListVoices(); // List available voices in the SAPI voice engine
	void SelectVoice(int index) // Select a voice by index from the list of available voices
	{
		IEnumSpObjectTokens* cpEnum = nullptr;
		ULONG count = 0;

		SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum); // Enumerate available voices in the SAPI voice engine
		cpEnum->GetCount(&count); // Get the count of available voices

		if (index >= 0 && index < (int)count)
		{
			ISpObjectToken* cpVoiceToken = nullptr;
			cpEnum->Item(index, &cpVoiceToken);
			pVoice->SetVoice(cpVoiceToken); // Set the selected voice in the SAPI voice engine
			cpVoiceToken->Release(); // Release the voice token after setting the voice
		}
		cpEnum->Release(); // Release the enumerator after selecting the voice
	}

	int GetSampleRate() const
	{
		return mSampleRate;
	}

	void SetSampleRate(int sampleRate)
	{
		mSampleRate = sampleRate;
	}

private:
	ISpVoice*           pVoice;          // Pointer to the SAPI voice interface
	VoiceDSP            dsp;             // DSP processor for applying effects to the voice output
	int                 mSampleRate;     // Sample rate reported by the audio device
	bool                comInitialized;  // Tracks whether this instance owns the CoInitialize call

	// Persistent WASAPI audio client — initialised once by InitAudio()
	IMMDevice*          mAudioDevice      = nullptr;
	IAudioClient*       mAudioClient      = nullptr;
	IAudioRenderClient* mRenderClient     = nullptr;
	UINT32              mBufferFrameCount = 0;
	UINT32              mNumChannels      = 0;

public:

};

#endif // VOICE_IO_H