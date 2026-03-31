#ifndef VOICE_DSP_H
#define VOICE_DSP_H
#include <cstddef>

#include "VoiceFilters.h"
#include "VoicePitch.h"

class VoiceDSP
{
public:
	VoiceDSP()
	{
		//filters = VoiceFilters(); // Initialize filters
		pitch = VoicePitch(); // Initialize pitch layer
	}
	void Process(short* samples, size_t count)
	{
		//filters.Apply(samples, count); // Apply filters to the audio samples
		pitch.Apply(samples, count); // Apply pitch shifting to the audio samples
	}

private:
	// Filters
	VoiceFilters filters;
	
	//Pitch layer
	VoicePitch pitch;
};	

#endif // VOICE_DSP_H