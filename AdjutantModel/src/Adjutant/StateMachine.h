#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <string>

enum class AdjutantState
{
	BOOTING,
	IDLE,
	DIAGNOSTICS,
	ALERT,
	SHUTDOWN
};

class AdjutantStateMachine
{
public:
	AdjutantStateMachine();

	void SetState(AdjutantState newState);
	AdjutantState GetState() const;

	std::string GetStateName(AdjutantState state) const;

private:
	AdjutantState state;
};

#endif // STATE_MACHINE_H