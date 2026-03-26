#ifndef SPEECH_ENGINE_H
#define SPEECH_ENGINE_H

#include <string>
#include <vector>
#include "VoiceOutputEngine.h"

class SpeechEngine
{
public:
	SpeechEngine();
	~SpeechEngine();

	void QueueLine(const std::string& line); // Add a line of dialogue to the queue
	bool HasLine() const; // Check if there are lines in the queue
	std::string PopLine(); // Get the next line of dialogue from the queue
	VoiceOutputEngine& GetVoiceOutput() { return voiceOutput; } // Accessor for the voice output engine

private:
	std::vector<std::string> queue; // Queue to hold lines of dialogue
	VoiceOutputEngine voiceOutput; // Voice output engine to speak lines
};

#endif // SPEECH_ENGINE_H