#include "Framework.h"
#include "../Adjutant/Speech/Voice/Vocalics/Phoneme.h"
#include <conio.h> // For _kbhit and _getch
#include <filesystem>

Framework::Framework() : DELTA_t(0.0), isRunning(false), baseCooldown(0.0), speechCooldown(0.0), pcmCooldown(0.0), inputBuffer("")
{
}

Framework::~Framework()
{
	Clean();
}

bool Framework::Init()
{
	std::filesystem::current_path(
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());

	if (!glfwInit())
	{
		std::cerr << "Failed to initialise GLFW." << std::endl;
		return false; // Initialize GLFW and check for success
	}

	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Create an invisible window for OpenGL context (can be made visible later if needed)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); // Set OpenGL context version to 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Use core profile for modern OpenGL features

	GLFWwindow* win = glfwCreateWindow(1, 1, "", nullptr, nullptr); // Create a tiny invisible window for the OpenGL context
	if (!win) 
	{
		std::cerr << "Failed to create GLFW window." << std::endl;
		return false;
	}
	glfwMakeContextCurrent(win); // Make the OpenGL context current on the created window

	glewExperimental = GL_TRUE; // Enable experimental features for GLEW
	if (!(glewInit() == GLEW_OK))
	{
		std::cerr << "Failed to initialize GLEW." << std::endl;
		return false; // Initialize GLEW and check for success
	}

	// Initialize Framework Engine and subsystems
	// Placeholder for initialization logic
	isRunning = true; // Set to true if initialization is successful
	lastFrameTime = chrono::high_resolution_clock::now(); // Initialize last frame time
	baseCooldown = 1.5; // Set base cooldown for speech output to 1.5 seconds
	speechCooldown = baseCooldown; // Initialize speech cooldown to the base cooldown
	pcmCooldown = 0.0; // Initialize PCM cooldown to 0 to allow immediate processing of PCM messages

	GetFileLoader() = FileLoader(); // Initialize the file loader subsystem
	Phoneme::LoadPhonemeData(*this, *(new std::string("src/Adjutant/Speech/Voice/Vocalics/Phonemes.dat")));

	// Initialize Adjutant Engine and other subsystems here
	adjModel.Init(); // Initialize the Adjutant Engine
	adjModel.GetGPU(*this, *(new std::string("src/Adjutant/GPU/core.shader")));
	adjModel.InitGPU(*this); // Initialize the GPU subsystem of the Adjutant Engine
	adjModel.GetSpeechEngine().LoadLanguage(*(new std::string("src/Adjutant/Speech/Voice/Language/Dictionary/en_US.dict")));
	
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
		char c = _getch(); // Always consume the key to drain the buffer

		if (adjModel.GetStateMachine().GetState() == AdjutantState::BOOTING)
		{
			// Discard all input during boot sequence
		}
		else if (c == '\r') // Enter key — no speech cooldown gate on input
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

	// Read GPU dialogue string output back to CPU and feed it into the speech queue
	DialogueOut dOut{};
	adjModel.ReadDialogueStringBuffer(dOut);

	// Get messages from the Adjutant Engine's message bus and handle them

	SetPCMCooldown(GetPCMCooldown() - DELTA_t); // Decrease PCM cooldown timer by delta time
	if (GetPCMCooldown() < 0.0)
		SetPCMCooldown(0.0); // Ensure PCM cooldown doesn't go negative

	if(GetPCMCooldown() == 0.0) // Only process PCM messages if the PCM cooldown is 0 to prevent overlapping speech output
	{
		auto buffer = adjModel.GetMessageBus().Fetch(); // Fetch messages from the Adjutant Engine's message bus
		for (const auto& msg : buffer)
		{
			if (msg.type == "PCM")
			{
				const auto& pcm = std::get<std::vector<int16_t>>(msg.payload); // Get the PCM audio data from the message payload
				adjModel.GetSpeechEngine().GetVoiceOutput().PlayPCM(pcm.data(), pcm.size()); // Play the PCM audio data through the voice output engine
			}
		}
	}

	SetSpeechCooldown(GetSpeechCooldown() - DELTA_t); // Decrease speech cooldown timer by delta time
	if(GetSpeechCooldown() < 0.0)
		SetSpeechCooldown(0.0); // Ensure cooldown doesn't go negative

	if (GetSpeechCooldown() == 0.0 && adjModel.GetSpeechEngine().HasLine())
	{
		std::string line = adjModel.GetSpeechEngine().PopLine();
		std::cout << line << std::endl;

		auto& speech = adjModel.GetSpeechEngine();
		auto& vo     = speech.GetVoiceOutput();

		std::string speechText = line;
		if (line.size() >= ADJUTANT_TAG.size() &&
			line.substr(0, ADJUTANT_TAG.size()) == ADJUTANT_TAG)
			speechText = line.substr(ADJUTANT_TAG.size());

		double dur = 0.0;
		dur += speech.PlayBeep(BeepType::MSG_START, vo);
		dur += speech.SpeakLine(speechText, vo);

		SetBaseCooldown(dur + 0.25); // Inter-line gap after the synthesized audio finishes
		SetSpeechCooldown(GetBaseCooldown());
	}

	if (adjModel.GetShouldClose() && !adjModel.GetSpeechEngine().HasLine()) // If the Adjutant Engine signals to close and there are no more lines to speak, shut down the framework
	{
		SetRunning(false); // Set running to false to exit the main loop
	}
}

