#include "AdjutantEngine.h"
#include <iostream>

AdjutantEngine::AdjutantEngine() : upTime(0.0), init(false) 
{

}

AdjutantEngine::~AdjutantEngine()
{
	Clean();
}

bool AdjutantEngine::Init()
{
	// Initialize AI OS engine
	upTime = 0.0;
	init = true;
	msgBus = MessageBus(); // Initialize the message bus
	speechEngine = SpeechEngine(); // Initialize the speech engine
	stateMachine = AdjutantStateMachine(); // Initialize the state machine
	speechEngine.QueueLine("Adjutant online. Beginning boot sequence."); // Queue initial boot message in the speech engine
	if (!(stateMachine.GetStateName(stateMachine.GetState()) == "UNKNOWN")) // Check if the state machine initialized correctly
		stateMachine.SetState(AdjutantState::BOOTING); // Transition to BOOTING state
	else speechEngine.QueueLine("Adjutant State Machine failed to initialize."); // Queue error message if state machine failed to initialize
	speechEngine.QueueLine("Boot sequence finalized. Adjutant online. entering Idle state."); // Queue initial status message in the speech engine
	if(!(stateMachine.GetState() == AdjutantState::IDLE))
		stateMachine.SetState(AdjutantState::IDLE); // Transition to IDLE state after booting
	return true; // Return true if initialization is successful
}

void AdjutantEngine::Update(double DELTA_t)
{
	if (!init) return; // Don't update if not initialized

	upTime += DELTA_t; // Increment up time by the delta time
	// Update AI OS logic here
	if (upTime > 5.0 && stateMachine.GetState() == AdjutantState::IDLE) // Example logic: Transition to DIAGNOSTICS state after 5 seconds in IDLE
	{
		stateMachine.SetState(AdjutantState::DIAGNOSTICS); // Transition to DIAGNOSTICS state after 5 seconds in IDLE
	}
}

void AdjutantEngine::Clean()
{
	// Clean up resources
	upTime = 0.0;
	init = false;
}

std::string AdjutantEngine::GetStatus() const
{
	if(stateMachine.GetStateName(stateMachine.GetState()) == "BOOTING")
		return init
			? "Adjutant online." + std::to_string(upTime)
			: "Adjutant not initialized.";
	else if (stateMachine.GetStateName(stateMachine.GetState()) == "UNKNOWN")
		return "Adjutant State Machine failed to initialize.";
	else
		return "State: " + stateMachine.GetStateName(stateMachine.GetState()) + " | Uptime: " + std::to_string(upTime);
}

void AdjutantEngine::ProcessCommand(const std::string& cmd)
{
	// implement here.

	if (cmd == "status")
	{
		speechEngine.QueueLine("Adjutant online.");
		speechEngine.QueueLine("Current State: " + stateMachine.GetStateName(stateMachine.GetState()));
		if (!(stateMachine.GetStateName(stateMachine.GetState()) == "UNKNOWN"))
			speechEngine.QueueLine("All systems nominal.");
		else
			speechEngine.QueueLine("State machine failed to initialize. Diagnostics recommended.");
	}
	else if (cmd == "uptime")
	{
		speechEngine.QueueLine("Uptime: " + std::to_string(upTime) + " seconds.");
	}
	else if (cmd == "help")
	{
		speechEngine.QueueLine("Currently available commands: 'status', 'uptime', 'help'.");
	}
	else if (cmd == "quit")
	{
		speechEngine.QueueLine("Affirmative.");
		speechEngine.QueueLine("Entering sleep mode. Goodbye.");

		SetShouldClose(true); // Signal the engine to shut down
	}
	else
	{
		speechEngine.QueueLine("Unrecognized command: " + cmd);
	}
		
}