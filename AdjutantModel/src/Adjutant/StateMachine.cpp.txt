#include "StateMachine.h"

AdjutantStateMachine::AdjutantStateMachine()
{
	state = AdjutantState::BOOTING; // Default initial state
}

void AdjutantStateMachine::SetState(AdjutantState newState)
{
	state = newState;
}

AdjutantState AdjutantStateMachine::GetState() const
{
	return state;
}

std::string AdjutantStateMachine::GetStateName(AdjutantState state) const
{
	switch (state)
	{
	case AdjutantState::BOOTING: return "BOOTING";
	case AdjutantState::IDLE: return "IDLE";
	case AdjutantState::DIAGNOSTICS: return "DIAGNOSTICS";
	case AdjutantState::ALERT: return "ALERT";
	case AdjutantState::SHUTDOWN: return "SHUTDOWN";
	default: return "UNKNOWN";
	}
}