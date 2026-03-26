#include "SpeechEngine.h"

SpeechEngine::SpeechEngine()
{
	
}

SpeechEngine::~SpeechEngine()
{
}

void SpeechEngine::QueueLine(const std::string& line)
{
	queue.push_back(line); // Add line to the queue
}

bool SpeechEngine::HasLine() const
{
	return !queue.empty(); // Check if the queue is not empty
}

std::string SpeechEngine::PopLine()
{
	if (queue.empty())
		return ""; // Return empty string if no lines are available

	std::string line = queue.front(); // Get the next line from the front of the queue
	queue.erase(queue.begin()); // Remove the line from the queue
	return line; // Return the line
}