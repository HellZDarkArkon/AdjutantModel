#ifndef ADJ_ENGINE_H
#define ADJ_ENGINE_H

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <unordered_map>

#include "MessageBus.h"
#include "StateMachine.h"
#include "Speech/SpeechEngine.h"
#include "Speech/AuditoryCortex.h"
#include "Speech/LanguageInputCortex.h"
#include "Speech/Voice/Vocalics/PhonemeSynth.h"
#include "TargetProfile.h"
#include "UserProfile.h"
#include "Brainstem/BrainstemManager.h"
#include "Memories/Memory.h"
#include "Speech/Voice/Language/LanguageCortex.h"

class Framework; // Forward declaration of the Framework class to avoid circular dependency

struct StateData
{
	float idleTimer;
	float emoState;
	float contextState;
	float decValue;
};
struct DialogueOut
{
	int intent;
	float tone;
	float urgency;
	float confidence;
	int systemOK; // GPU sets to 0 when INTENT_QUIT fires; CPU reads back to trigger shutdown
};

// CPU mirror of GPUMemoryEntry (binding 8) — 12 floats/ints × 4 bytes = 48 bytes per slot.
// Named STMEntry to distinguish from the LTM MemoryEntry used by MemoryManager.
struct STMEntry
{
	int   index;           // 1-based slot index
	int   timeCode;        // HHMM at write time
	int   dateCode;        // YYYYMMDD at write time
	int   flags;           // bit0=ltmReady  bit1=eventBoundary  bit2=novel
	float idleTimer;
	float emotionalState;
	float contextValue;
	float decisionValue;
	float salienceScore;
	float priorityScore;
	float noveltyScore;
	float timeAge;         // seconds elapsed since this slot was written
};

// CPU mirror of the full MemoryData SSBO (binding 8): header + 64-slot ring buffer
struct MemoryDataSnapshot
{
	int      memCount;
	int      memWriteHead;
	int      pad0, pad1;
	STMEntry entries[64];
};

// CPU mirror of the MemoryRuntime SSBO (binding 9): 11 floats + 5 ints = 64 bytes
struct MemoryRuntimeData
{
	float avgIdleTimer;
	float avgEmotionalState;
	float avgContextValue;
	float avgDecisionValue;
	float prevIdleTimer;
	float prevEmotionalState;
	float prevContextValue;
	float prevDecisionValue;
	float salienceScore;
	float noveltyScore;
	float priorityScore;
	int   writeFlag;
	int   eventBoundaryFlag;
	int   ltmReadyCount;    // total entries with ltmReady bit set (LTM-eligible)
	int   captureBlocked;   // 1 = brainstem suppressed capture this frame
	int   rtPad1;
};

// Memory command codes — must match the constants defined in memory.shader
static constexpr int MEM_CMD_NONE    = 0;
static constexpr int MEM_CMD_CAPTURE = 1; // CPU-triggered manual snapshot
static constexpr int MEM_CMD_INJECT  = 2; // CPU-triggered state restore from a slot

// CPU mirror of the NeurochemState SSBO (binding 12): 6 active floats + 2 pad = 32 bytes.
// Field order must match the std430 layout declared in neuro.shader exactly.
	struct NeurochemState
{
	float reward;    // dopamine / endorphin   [0,1]  reinforcement signal
	float threat;    // cortisol / adrenaline  [0,1]  stress / danger response
	float novelty;   // dopamine / NE          [0,1]  curiosity / alertness
	float focus;     // NE / acetylcholine     [0,1]  attentional load
	float fatigue;   // adenosine              [0,1]  tiredness / rest drive
	float social;    // oxytocin / serotonin   [0,1]  connection need
	float neuroPad0;
	float neuroPad1;
};

// CPU mirror of the SensoryAvgState SSBO (binding 13): 4 EMA floats + halfLife + 3 pad = 32 bytes.
// slowEmotional/slowContext/slowDecision are the slow-path inputs to the decision kernel.
// halfLife (seconds) controls how quickly the slow path tracks the fast path:
//   small halfLife = more reactive; large = more inertial baseline.
struct SensoryAvgState
{
	float slowIdleTimer; // EMA of idleTimer
	float slowEmotional; // EMA of emotionalState
	float slowContext;   // EMA of contextValue
	float slowDecision;  // EMA of decisionValue
	float halfLife;      // seconds; tunable EMA time constant (default 5.0)
	float saPad0;
	float saPad1;
	float saPad2;
};

// CPU mirror of the LinguisticLearning SSBO (binding 15): 16 floats = 64 bytes.
// GPU-maintained EMA statistics representing Adjutant's long-horizon language model.
// Field order must match the std430 layout declared in language.shader exactly.
struct LinguisticLearningData
{
	float avgWordCount;       // EMA of words per utterance
	float avgPhonemePerWord;  // EMA of phonemes/word ratio
	float avgOovRate;         // EMA of OOV rate (vocabulary novelty trend)
	float avgComplexity;      // EMA of complexity score
	float avgF0Norm;          // EMA of realized F0 (speaker register)
	float avgSyllDurNorm;     // EMA of syllable duration (speaking rate)
	float utteranceCount;     // running count of utterances processed
	float learningRate;       // EMA alpha per utterance (default 0.10)
	float vocabularyNorm;     // vocabulary breadth estimate [0,1]
	float convergenceScore;   // stability metric [0,1]
	float llPad0, llPad1, llPad2, llPad3, llPad4, llPad5;
};

struct U32String
{
	int length;
	uint32_t codepoints[1024]; // Fixed-size array — must match DialogueString.text[1024] in dialogue1.shader
};

inline const std::string ADJUTANT_TAG = "[ADJUTANT] > "; // Global prefix for all Adjutant system messages

class AdjutantEngine
{
	static constexpr GLsizeiptr TEMPLATE_BUFFER_SIZE = (128 * sizeof(GLint)) + (8192 * sizeof(GLuint)); // Size of the DialogueTemplates SSBO: 128 int offsets + 8192 uint data entries
	public:
	AdjutantEngine();
	~AdjutantEngine();

	bool Init(); // Initialize AI OS engine
	void InitGPU(Framework& fw); // Initialize GPU subsystem for GPU compute tasks
	void UpdateMindGPU(double Dt); // Update AI OS logic related to GPU tasks

	void Update(double DELTA_t); // Update engine logic with time step
	void Clean(); // Clean up resources

	uint32_t HashString(const std::string& s)
	{
		uint32_t h = 2166136261u; // FNV-1a 32-bit hash initial value
		for (unsigned char c : s)
		{
			h ^= c; // XOR the byte with the hash
			h *= 16777619u; // Multiply by the FNV prime
		}
		return h;
	}

	void ReadDialogueStringBuffer(DialogueOut& d)
	{
		U32String gpuOut{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, dialogueStringBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(U32String), &gpuOut); // Read the data from the dialogue string buffer into the gpuOut structure

		if (gpuOut.length > 0)
		{
			const auto flush = [&](std::string& seg)
			{
				if (!seg.empty())
				{
					GetSpeechEngine().QueueLine(*this, seg); // one push per ' - ' separated segment
					seg.clear();
				}
			};

			std::string seg;
			seg.reserve(gpuOut.length);

			for (int i = 0; i < gpuOut.length; i++)
			{
				uint32_t cp = gpuOut.codepoints[i];

				if (cp == 1u) // segment separator inserted by BakeTemplates between [ARG:] blocks
				{
					flush(seg);
					continue;
				}

				if (cp <= 0x7F)
					seg.push_back(static_cast<char>(cp)); // 1-byte character (ASCII)
				else if (cp <= 0x7FF)
				{
					seg.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F))); // First byte of 2-byte character
					seg.push_back(static_cast<char>(0x80 | (cp & 0x3F))); // Second byte of 2-byte character
				}
				else if (cp <= 0xFFFF)
				{
					seg.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F))); // First byte of 3-byte character
					seg.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); // Second byte of 3-byte character
					seg.push_back(static_cast<char>(0x80 | (cp & 0x3F))); // Third byte of 3-byte character
				}
				else
				{
					seg.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07))); // First byte of 4-byte character
					seg.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); // Second byte of 4-byte character
					seg.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); // Third byte of 4-byte character
					seg.push_back(static_cast<char>(0x80 | (cp & 0x3F))); // Fourth byte of 4-byte character
				}
			}
			flush(seg); // flush the final segment
		}
	}

	void WriteDialogueInput(const std::string& text)
	{
		U32String u32 = GetU32String(text, 256);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, dialogueInputBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(U32String), &u32);
	}

	U32String GetU32String(const std::string& s, int maxCnt)
	{
		U32String out{};
		out.length = 0; // Initialize length to 0

		const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.data()); // Get the bytes of the input string
		int size = static_cast<int>(s.size()); // Get the size of the input string in bytes
		int i = 0;

		while (i < size && out.length < maxCnt)
		{
			uint32_t cp = 0;
			unsigned char c = bytes[i];

			if (c < 0x80)
			{
				cp = c;
				i += 1; // 1-byte character (ASCII)
			}
			else if ((c >> 5) == 0x6)
			{
				if (i + 1 >= size) break; // Check for valid continuation byte
				cp = ((c & 0x1F) << 6) |
					(bytes[i + 1] & 0x3F); // 2-byte character
				i += 2; // Move to the next character
			}
			else if ((c >> 4) == 0xE)
			{
				if (i + 2 >= size) break; // Check for valid continuation bytes
				cp = ((c & 0x0F) << 12) |
					((bytes[i + 1] & 0x3F) << 6) |
					(bytes[i + 2] & 0x3F); // 3-byte character
				i += 3; // Move to the next character
			}
			else if ((c >> 3) == 0x1E)
			{
				if (i + 3 >= size) break; // Check for valid continuation bytes
				cp = ((c & 0x07) << 18) |
					((bytes[i + 1] & 0x3F) << 12) |
					((bytes[i + 2] & 0x3F) << 6) |
					(bytes[i + 3] & 0x3F); // 4-byte character
				i += 4; // Move to the next character
			}
			else
			{
				cp = 0xFFFD; // Invalid byte sequence, use replacement character
				i += 1; // Move to the next byte
			}

			out.codepoints[out.length++] = cp; // Store the decoded Unicode code point in the output structure and increment the length
		}

		return out; // Return the structure containing the decoded Unicode code points and their count
	}

	static GLuint CompileCompute(const std::string& src)
	{
		GLuint shader = glCreateShader(GL_COMPUTE_SHADER); // Create an OpenGL compute shader object
		const char* ptr = src.c_str(); // Get C-style string from the source code string
		glShaderSource(shader, 1, &ptr, nullptr); // Set the source code for the compute shader
		glCompileShader(shader); // Compile the compute shader

		GLint ok;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok); // Check if compilation was successful
		if (!ok)
		{
			char log[2048];
			glGetShaderInfoLog(shader, 2048, nullptr, log); // Get the compilation error log
			std::cerr << "Compute shader compilation failed: " << log << std::endl; // Print the error log to the console
		}

		GLuint prg = glCreateProgram(); // Create an OpenGL program object
		glAttachShader(prg, shader); // Attach the compiled compute shader to the program
		glLinkProgram(prg); // Link the program to create an executable for the compute shader
		glDeleteShader(shader); // Delete the shader object after linking since it's no longer needed

		return prg;

	}

	void WriteIntent(int intent);
	void BakeTemplates();
	void SetTargetProfile(const TargetProfile& p);
	const TargetProfile& GetTargetProfile() const { return currentTarget; }
	void SetUserProfile(const UserProfile& u) { registeredUser = u; SetTargetProfile(u); }
	const UserProfile& GetUserProfile() const { return registeredUser; }

	std::string GetGPU(Framework& fw, const std::string& filePath); // Load GPU compute shader from file

	void ProcessCommand(const std::string& cmd);
	void ProcessDialogue(const DialogueOut& d); // Process dialogue output data from the dialogue compute shader and generate appropriate responses or actions based on the intent, tone, urgency and confidence values
	void ProcessSpeech(const std::string& text, bool tts); // Process speech output, if tts is false, the text is treated as a line to be spoken by the speech engine, if tts is true, the text is treated as raw text to be synthesized into speech using the phoneme synthesizer (this can be used for testing the phoneme synthesizer with custom input)

	std::string GetStatus() const;

	void SetShouldClose(bool close) { shouldClose = close; } // Mutator for the shutdown flag

	MessageBus& GetMessageBus() { return msgBus; } // Accessor for the message bus
	AdjutantStateMachine& GetStateMachine() { return stateMachine; } // Accessor for the state machine
	SpeechEngine& GetSpeechEngine() { return speechEngine; } // Accessor for the speech engine
	AuditoryCortex& GetAuditoryCortex() { return mAuditoryCortex; } // Accessor for the auditory cortex
	LanguageInputCortex& GetLanguageInputCortex() { return mLangInputCortex; } // Accessor for the language input cortex
	bool GetShouldClose() const { return shouldClose; } // Accessor for the shutdown flag
private:
	std::vector<std::string> computeList; // List of compute shader file paths for GPU tasks
	double upTime; // Total time since engine start
	bool init; // Flag to indicate if engine is initialized
	MessageBus msgBus; // Message bus for inter-subsystem communication
	AdjutantStateMachine stateMachine; // State machine to manage engine states
	SpeechEngine speechEngine; // Speech engine for handling dialogue
	bool shouldClose; // Flag to indicate if the engine should shut down
	std::optional<Phoneme> phoneme; // Example phoneme for testing the phoneme synthesizer
	PhonemeSynth synth; // Phoneme synthesizer for generating audio from phonemes
	TargetProfile currentTarget;
	UserProfile registeredUser;
	std::vector<std::string> rawTemplates;
	CoreState lastBakedCoreState = CoreState::STATE_OK;
	
	
	GLuint coreProgram = 0; // OpenGL program object for the core compute shader set to 0 as a placeholder until initialized
	GLuint emoProgram = 0; // OpenGL program object for the emotion compute shader set to 0 as a placeholder until initialized
	GLuint contextProgram = 0; // OpenGL program object for the context compute shader set to 0 as a placeholder until initialized
	GLuint stateProgram = 0; // OpenGL program object for the state compute shader set to 0 as a placeholder until initialized
	GLuint decProgram = 0; // OpenGL program object for the decision compute shader set to 0 as a placeholder until initialized
	GLuint outputProgram = 0; // OpenGL program object for the output compute shader set to 0 as a placeholder until initialized
	GLuint diasemProgram = 0;    // OpenGL program object for the dialogue semantic compute shader
	GLuint dianeuroProgram = 0;  // OpenGL program object for the dialogue personality/memory compute shader
	GLuint dialogue1Program = 0; // OpenGL program object for the dialogue string compute shader
	GLuint diainpProgram = 0;    // OpenGL program object for the dialogue input compute shader

	// Memory Mind Kernel programs (bindings 7-9)
	GLuint memInfluenceProgram = 0; // Arc 1 — Memory → State: primes CoreMindState from STM averages each frame
	GLuint memUpdateProgram    = 0; // Arc 3 — per-frame STM pipeline: temporal integration, salience, auto-capture
	GLuint memCaptureProgram   = 0; // CPU-triggered manual capture
	GLuint memInjectProgram    = 0; // CPU-triggered state injection from a stored entry

	GLuint stateBuffer = 0; // OpenGL SSBO ID for state data set to 0 as a placeholder until initialized
	GLuint dialogueBuffer = 0; // OpenGL SSBO ID for dialogue output data set to 0 as a placeholder until initialized
	GLuint commandInputBuffer = 0; // OpenGL SSBO ID for command input data set to 0 as a placeholder until initialized
	GLuint dialogueTemplateBuffer = 0; // OpenGL SSBO ID for dialogue template data set to 0 as a placeholder until initialized
	GLuint dialogueStringBuffer = 0; // OpenGL SSBO ID for dialogue string data set to 0 as a placeholder until initialized
	GLuint dialogueInputBuffer = 0; // OpenGL SSBO ID for dialogue input data set to 0 as a placeholder until initialized

	// Memory Mind Kernel SSBOs
	GLuint memCommandBuffer = 0; // binding 7 — MemoryCommand: CPU writes command/slot/time/date codes
	GLuint memDataBuffer    = 0; // binding 8 — MemoryData: STM ring buffer (64 entries)
	GLuint memRuntimeBuffer = 0; // binding 9 — MemoryRuntime: temporal averages, scores, flags

	// Brainstem Kernel programs (bindings 10-11)
	GLuint brainstemProgram     = 0; // #brainstem — per-frame directive enforcement
	GLuint brainstemEditProgram = 0; // #brainstemedit — CPU-triggered edit validation

	// Brainstem Kernel SSBOs
	GLuint brainstemBuffer     = 0; // binding 10 — BrainstemData (directives + session auth + version)
	GLuint brainstemEditBuffer = 0; // binding 11 — BrainstemEditCmd (incoming edit request)

	// Neurochemical Kernel programs (binding 12)
	GLuint neuroUpdateProgram    = 0; // Arc 6 — State → Neuro: per-frame decay + CoreMindState excitations
	GLuint neuroInfluenceProgram = 0; // Arc 5 — Neuro → Slow Path: tints SensoryAvgState before dec kernel

	// Neurochemical SSBO
	GLuint neuroBuffer = 0; // binding 12 — NeurochemState (reward, threat, novelty, focus, fatigue, social)

	// Sensory Avg Kernel program (binding 13)
	GLuint sensorAvgProgram = 0; // Arc 7 — Fast → Slow: per-frame EMA update (CoreMindState → SensoryAvgState)

	// Sensory Avg SSBO
	GLuint sensorAvgBuffer  = 0; // binding 13 — SensoryAvgState (slowEmotional, slowContext, slowDecision, halfLife)

	// Language Cortex Kernel programs (bindings 14-15)
	GLuint langUpdateProgram    = 0; // Arc 8 — CPU → GPU: integrate utterance features into LinguisticLearning
	GLuint langInfluenceProgram = 0; // Arc 9 — Language → Slow Path: linguistic stats tint SensoryAvgState

	// Language Cortex SSBOs
	GLuint linguisticStateBuffer    = 0; // binding 14 — LinguisticState (per-utterance feature vector)
	GLuint linguisticLearningBuffer = 0; // binding 15 — LinguisticLearning (GPU-maintained running EMA stats)

	BrainstemManager brainstemMgr;
	MemoryManager    memoryMgr;    // CPU SSD long-term memory bank
	LanguageCortex   mLangCortex;  // CPU-side linguistic feature extractor + novelty window
	AuditoryCortex      mAuditoryCortex;  // WASAPI microphone stream capture
	LanguageInputCortex mLangInputCortex; // VAD + acoustic analysis bridge to LanguageCortex
	std::unordered_map<GLuint, GLint> deltaTimeLocs;


	enum IntentCode
	{
		INTENT_NONE     = 0, // CMD_NONE   = 0
		INTENT_STATUS   = 1, // CMD_STATUS = 1
		INTENT_HELP     = 2, // CMD_HELP   = 2
		INTENT_UPTIME   = 3, // CMD_UPTIME = 3
		INTENT_QUIT     = 4, // CMD_QUIT   = 4
		INTENT_PHONEME  = 5, // CMD_PLAYX  = 5
	};

	std::unordered_map<std::string, IntentCode> intentMap =
	{
		{"status", INTENT_STATUS},
		{"uptime", INTENT_UPTIME},
		{"help", INTENT_HELP},
		{"quit", INTENT_QUIT},

		{"play a1", INTENT_PHONEME},
		{"play a2", INTENT_PHONEME},
		{"play a3", INTENT_PHONEME},
		{"play e1", INTENT_PHONEME},
		{"play e2", INTENT_PHONEME},
		{"play e3", INTENT_PHONEME},
		{"play i1", INTENT_PHONEME},
		{"play i2", INTENT_PHONEME},
		{"play i3", INTENT_PHONEME},
		{"play o1", INTENT_PHONEME},
		{"play o2", INTENT_PHONEME},
		{"play o3", INTENT_PHONEME},
		{"play u1", INTENT_PHONEME},
		{"play u2", INTENT_PHONEME},
		{"play u3", INTENT_PHONEME},
		{"play y1", INTENT_PHONEME},
		{"play y2", INTENT_PHONEME}
	};


public:
	GLuint GetCommandInputBuffer() const { return commandInputBuffer; } // Accessor for the command input buffer

	void SetCommandInputBuffer(GLuint buffer) { commandInputBuffer = buffer; } // Mutator for the command input buffer

	int LookupIntent(const std::string& cmd)
	{
		std::string lower = cmd;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

		auto it = intentMap.find(lower);
		if (it != intentMap.end())
			return it->second; // Return the intent code if found in the map
		else
			return INTENT_NONE; // Return INTENT_NONE if the command is not recognized
	}

	// ---------------------------------------------------------------
	// Short-Term Memory Bank API
	// ---------------------------------------------------------------

	// Write MEM_CMD_CAPTURE to the GPU command buffer.
	// timeCode = HHMM (e.g. 1430 for 14:30), dateCode = YYYYMMDD.
	// The GPU #memcapture pass will consume the command and reset it to MEM_CMD_NONE.
	void TriggerMemoryCapture(int timeCode, int dateCode)
	{
		struct { int cmd, slot, tc, dc; } mCmd = { MEM_CMD_CAPTURE, 0, timeCode, dateCode };
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, memCommandBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(mCmd), &mCmd);
	}

	// Write MEM_CMD_INJECT to the GPU command buffer for the given slot index.
	// The GPU #meminject pass will restore that slot into CoreMindState and reset the command.
	void TriggerMemoryInject(int slot, int timeCode, int dateCode)
	{
		struct { int cmd, slot, tc, dc; } mCmd = { MEM_CMD_INJECT, slot, timeCode, dateCode };
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, memCommandBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(mCmd), &mCmd);
	}

	// Read back the MemoryRuntime SSBO (binding 9) from GPU VRAM to CPU.
	// Returns temporal averages, salience/novelty/priority scores, and status flags.
	MemoryRuntimeData ReadMemoryRuntime()
	{
		MemoryRuntimeData rt{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, memRuntimeBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(MemoryRuntimeData), &rt);
		return rt;
	}

	// Read back the full MemoryData SSBO (binding 8) from GPU VRAM to CPU.
	// Returns the ring-buffer header and all 64 STM entries. Use ltmReady entries for LTM promotion.
	MemoryDataSnapshot ReadMemoryData()
	{
		MemoryDataSnapshot snap{};
		constexpr GLsizeiptr MEM_DATA_SIZE = 4 * sizeof(GLint) + 64 * 12 * sizeof(GLfloat);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, memDataBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, MEM_DATA_SIZE, &snap);
		return snap;
	}

	// ---------------------------------------------------------------
	// Long-Term Memory Bank API
	// ---------------------------------------------------------------

	// Restore a persisted LTM entry directly into CoreMindState (binding 0).
	// For full pipeline restoration (syncs STM running averages) use TriggerMemoryInject instead.
	void InjectFromLTM(int index)
	{
		memoryMgr.InjectToGPU(index, stateBuffer);
	}

	const std::vector<MemoryEntry>& GetLTMEntries() const { return memoryMgr.GetEntries(); }

	// ---------------------------------------------------------------
	// Neurochemical State API
	// ---------------------------------------------------------------

	// Read back the NeurochemState SSBO (binding 12) from GPU VRAM to CPU.
	// Returns the six hormone / neurotransmitter axis values.
	NeurochemState ReadNeurochemState()
	{
		NeurochemState ns{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, neuroBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(NeurochemState), &ns);
		return ns;
	}

	// Add a CPU-side delta to one or more neuro axes (e.g. reward pulse on task completion).
	// Each value is clamped to [0, 1] after addition. Pass 0.0f for axes to leave unchanged.
	void SpikeNeuro(float reward, float threat, float novelty,
					float focus,  float fatigue, float social)
	{
		NeurochemState ns = ReadNeurochemState();
		auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
		ns.reward  = clamp01(ns.reward  + reward);
		ns.threat  = clamp01(ns.threat  + threat);
		ns.novelty = clamp01(ns.novelty + novelty);
		ns.focus   = clamp01(ns.focus   + focus);
		ns.fatigue = clamp01(ns.fatigue + fatigue);
		ns.social  = clamp01(ns.social  + social);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, neuroBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(NeurochemState), &ns);
	}

	// ---------------------------------------------------------------
	// Sensory Avg State API  (binding 13 — slow path)
	// ---------------------------------------------------------------

	// Read back the SensoryAvgState SSBO (binding 13) from GPU VRAM to CPU.
	SensoryAvgState ReadSensoryAvgState()
	{
		SensoryAvgState sa{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, sensorAvgBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(SensoryAvgState), &sa);
		return sa;
	}

	// Sets the EMA half-life (seconds) for the slow sensory path.
	// Larger values make the slow path more inertial; smaller values make it
	// track the fast path more closely. Minimum is clamped to 0.1s on the GPU.
	void SetHalfLife(float seconds)
	{
		SensoryAvgState sa = ReadSensoryAvgState();
		sa.halfLife = seconds > 0.0f ? seconds : 0.1f;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, sensorAvgBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(SensoryAvgState), &sa);
	}

	// ---------------------------------------------------------------
	// Language Cortex API  (bindings 14-15)
	// ---------------------------------------------------------------

	// Read back the LinguisticLearning SSBO (binding 15) from GPU VRAM to CPU.
	// Returns the GPU-maintained EMA statistics of communication patterns.
	LinguisticLearningData ReadLinguisticLearning()
	{
		LinguisticLearningData ll{};
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, linguisticLearningBuffer);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(LinguisticLearningData), &ll);
		return ll;
	}

	// Force-push the current cortex features to GPU binding 14 immediately.
	// Normally called automatically at the start of each UpdateMindGPU frame
	// when the cortex is dirty; use this for synchronous testing only.
	void SubmitLinguisticState()
	{
		if (!mLangCortex.IsDirty()) return;
		const LinguisticFeatures& feats = mLangCortex.GetFeatures();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, linguisticStateBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(LinguisticFeatures), &feats);
		mLangCortex.ClearDirty();
	}

	// Accessor for the language cortex (e.g. to tune novelty window size).
	LanguageCortex& GetLanguageCortex() { return mLangCortex; }

};

#endif