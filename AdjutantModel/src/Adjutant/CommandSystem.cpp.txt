#include "CommandSystem.h"

CommandSystem::CommandSystem()
{
}

CommandSystem::~CommandSystem()
{
}

void CommandSystem::PushCommand(const std::string& cmd)
{
	queue.push_back(cmd); // Add command to the queue
}

bool CommandSystem::HasCommand() const
{
	return !queue.empty(); // Check if the queue is not empty
}

std::string CommandSystem::PopCommand()
{
	if (queue.empty())
		return ""; // Return empty string if no commands are available
	std::string cmd = queue.front(); // Get the next command from the front of the queue
	queue.erase(queue.begin()); // Remove the command from the queue
	return cmd; // Return the command
}