#include "Framework.h"
#include <conio.h> // For _kbhit and _getch

Framework::Framework() : DELTA_t(0.0), isRunning(false), baseCooldown(0.0), speechCooldown(0.0), inputBuffer("")
{
}

Framework::~Framework()
{
	Clean();
}

bool Framework::Init()
{
	// Initialize Framework Engine and subsystems
	// Placeholder for initialization logic
	isRunning = true; // Set to true if initialization is successful
	lastFrameTime = chrono::high_resolution_clock::now(); // Initialize last frame time
	baseCooldown = 1.5; // Set base cooldown for speech output to 1.5 seconds
	speechCooldown = baseCooldown; // Initialize speech cooldown to the base cooldown

	// Initialize Adjutant Engine and other subsystems here
	adjModel.Init(); // Initialize the Adjutant Engine
	
	return true; // Return true if initialization is successful
}

void Framework::Run()
{
	// Main loop
	lastFrameTime = chrono::high_resolution_clock::now();
	while (isRunning)
	{
		// Calculate delta time
		auto currentTime = chrono::high_resolution_clock::now();
		DELTA_t = chrono::duration<double>(currentTime - lastFrameTime).count();
		lastFrameTime = currentTime;
		Update(); // Update logic for all subsystems
		// Render logic would go here
		// Placeholder for event handling and window management
	}
}

void Framework::Clean()
{
	// Clean up resources, shut down subsystems, etc.
	cout << "Cleaning up Framework resources." << endl;
	adjModel.Clean(); // Clean up the Adjutant Engine
}

void Framework::Update()
{
	// Update logic for all subsystems

	// Non-blocking input check to allow command input without freezing the main loop
	if (_kbhit()) // Check if a key has been pressed
	{
		char c = _getch(); // Get the pressed key

		if (c == '\r' && speechCooldown == 0.0) // If Enter key is pressed and speech cooldown is 0, process the command
		{
			std::cout << std::endl;
			if (!inputBuffer.empty())
			{
				cmdSystem.PushCommand(inputBuffer); // Push the command to the command system
				inputBuffer.clear(); // Clear the input buffer after pushing the command
			}
		}
		else if (c == '\b') // Backspace
		{
			if (!inputBuffer.empty())
			{
				inputBuffer.pop_back(); // Remove the last character from the input buffer
				std::cout << "\b \b"; // Erase the character from the console
			}
		}
		else {
			inputBuffer.push_back(c); // Add the character to the input buffer
			std::cout << c; // Echo the character to the console
		}
	}

	// Process commands from the command system
	while (cmdSystem.HasCommand())
	{
		std::string cmd = cmdSystem.PopCommand(); // Get the next command from the command system
		adjModel.ProcessCommand(cmd); // Process the command in the Adjutant Engine
	}

	adjModel.Update(DELTA_t); // Update the Adjutant Engine with delta time
	// Get messages from the Adjutant Engine's message bus and handle them
	speechCooldown -= DELTA_t; // Decrease speech cooldown timer by delta time
	if(speechCooldown < 0.0)
		speechCooldown = 0.0; // Ensure cooldown doesn't go negative

	if (speechCooldown == 0.0 && adjModel.GetSpeechEngine().HasLine())
	{
		std::string line = adjModel.GetSpeechEngine().PopLine();
		std::cout << "[ADJUTANT] " << line << std::endl;

		// Convert to wide string for SAPI
		std::wstring wline(line.begin(), line.end());
		double tts = adjModel.GetSpeechEngine().GetVoiceOutput().SpeakTimed(wline); // Speak the line and get the time taken for speech output

		baseCooldown = 1.5; // Set cooldown to 1.5 seconds after speaking a line
		speechCooldown = baseCooldown + tts;
	}

	if (adjModel.GetShouldClose() && !adjModel.GetSpeechEngine().HasLine()) // If the Adjutant Engine signals to close and there are no more lines to speak, shut down the framework
	{
		isRunning = false; // Set running to false to exit the main loop
	}
}

