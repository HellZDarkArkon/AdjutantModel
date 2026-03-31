#ifndef MSGBUS_H
#define MSGBUS_H

#include <string>
#include <vector>

struct Message
{
	std::string type;
	std::string payload;
};

class MessageBus
{
public:
	MessageBus() = default;
	void Send(const Message& msg);
	std::vector<Message> Fetch();

private:
	std::vector<Message> queue; // Simple in-memory message queue
};

#endif // MSGBUS_H