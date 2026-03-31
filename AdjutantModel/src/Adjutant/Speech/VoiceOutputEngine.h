#ifndef VOICE_IO_H
#define VOICE_IO_H

#include <string>
#include <sapi.h>
#include <sphelper.h>

class VoiceOutputEngine // Setup Vocal Output Engine using Microsoft Speech API (SAPI)
{
public:
	VoiceOutputEngine();
	
	double SpeakTimed(const std::wstring& text); // Speak a line of text using the voice engine

	void PlayPCM(const float* samples, size_t count); // Play raw PCM audio data through the voice engine

private:
	ISpVoice* pVoice; // Pointer to the SAPI voice interface

public:

};

#endif // VOICE_IO_H