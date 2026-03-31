#ifndef COMMANDSYSTEM_H
#define COMMANDSYSTEM_H

#include <string>
#include <vector>

class CommandSystem
{
public:
	CommandSystem();
	~CommandSystem();

	void PushCommand(const std::string& cmd);
	bool HasCommand() const;
	std::string PopCommand();

private:
	std::vector<std::string> queue; // Queue to hold commands
};

#endif // COMMANDSYSTEM_H