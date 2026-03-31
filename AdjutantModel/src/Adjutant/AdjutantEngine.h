#ifndef ADJ_ENGINE_H
#define ADJ_ENGINE_H

#include <string>
#include <vector>

#include "MessageBus.h"
#include "StateMachine.h"
#include "Speech/SpeechEngine.h"

class AdjutantEngine
{
	public:
	AdjutantEngine();
	~AdjutantEngine();

	bool Init(); // Initialize AI OS engine
	void Update(double DELTA_t); // Update engine logic with time step
	void Clean(); // Clean up resources

	void ProcessCommand(const std::string& cmd); // Process a command input

	std::string GetStatus() const;

	void SetShouldClose(bool close) { shouldClose = close; } // Mutator for the shutdown flag

	MessageBus& GetMessageBus() { return msgBus; } // Accessor for the message bus
	AdjutantStateMachine& GetStateMachine() { return stateMachine; } // Accessor for the state machine
	SpeechEngine& GetSpeechEngine() { return speechEngine; } // Accessor for the speech engine
	bool GetShouldClose() const { return shouldClose; } // Accessor for the shutdown flag
private:
	double upTime; // Total time since engine start
	bool init; // Flag to indicate if engine is initialized
	MessageBus msgBus; // Message bus for inter-subsystem communication
	AdjutantStateMachine stateMachine; // State machine to manage engine states
	SpeechEngine speechEngine; // Speech engine for handling dialogue
	bool shouldClose; // Flag to indicate if the engine should shut down
};

#endif