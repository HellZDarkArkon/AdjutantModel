#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <string>
#include <iostream>
#include <chrono>

#include "../Adjutant/AdjutantEngine.h"
#include "../Adjutant/CommandSystem.h"

using namespace std;

class Framework
{
public:
	Framework();
	~Framework();
	
	bool Init();
	void Run();
	void Clean();

private:
	// Timing
	chrono::high_resolution_clock::time_point lastFrameTime;
	double DELTA_t; // Time between frames in seconds
	AdjutantEngine adjModel;
	CommandSystem cmdSystem;
	double baseCooldown; // Base cooldown for speech output
	double speechCooldown; // Combined cooldown for speech output, including any additional time from the last spoken line
	std::string inputBuffer; // Buffer to hold user input for commands

	// Adjutant Model Subsystems


	// Subsystem hooks
	void Update(); // Called every frame to update logic

	// Utility
	bool isRunning; // Flag to control the main loop

public:
	// Accessors for subsystems can be added here

	bool GetRunning() const { return isRunning; }
	double GetDeltaTime() const { return DELTA_t; }
	AdjutantEngine& GetAdjutantEngine() { return adjModel; }
	CommandSystem& GetCommandSystem() { return cmdSystem; }

	void SetRunning(bool running) { isRunning = running; }
	void SetDeltaTime(double deltaTime) { DELTA_t = deltaTime; }
	void SetAdjutantEngine(const AdjutantEngine& engine) { adjModel = engine; }
	void SetCommandSystem(const CommandSystem& cmdSys) { cmdSystem = cmdSys; }
};

#endif // FRAMEWORK_H