#version 430
layout(local_size_x = 1) in;

layout(std430, binding= 0) buffer CoreMindState 
{ 
	float idleTimer;
	float emotionalState;
	float contextValue;
	float decisionValue;
};
layout (std430, binding = 1) buffer DialogueSemantic 
{ 
	int intent; //what the GPU mind intends to say
	float tone; // emotional tone of the dialogue, from -1 (negative) to 1 (positive)
	float urgency; // how urgent the dialogue is, from 0 (not urgent) to 1 (very urgent)
	float confidence; // how confident the GPU mind is in its dialogue, from 0 (not confident) to 1 (very confident)
	int systemOK; // 1 = system running; GPU sets to 0 when INTENT_QUIT fires to signal CPU shutdown

};

layout (std430, binding = 2) buffer DialogueString
{
	int charCount;
	uint text[1024]; // UTF-32 encoded text for the dialogue — sized to hold multi-segment intent responses
};

layout(std430, binding = 3) buffer CommandInput
{
	int commandCode;
	int arg0;
	int arg1;
};

layout(std430, binding = 4) buffer DialogueTemplates
{
	int offsets[128]; // Offsets for different dialogue templates in the text buffer
	uint data[8192]; // UTF-32 encoded dialogue templates, allowing for a wide range of characters and symbols
};

layout(std430, binding = 5) buffer StateNameBuffer
{
	int stateLen;
	uint stateText[65]; // UTF-32 encoded text for the current state name, allowing for a wide range of characters and symbols
};

layout(std430, binding = 6) buffer DialogueInput 
{
	int inputLen;
	uint inputText[256]; // UTF-32 encoded text for the dialogue input, allowing for a wide range of characters and symbols
};

// Arc 4 — Memory → Interpretation
// STM running averages from the Memory Mind Kernel are read here so that
// accumulated emotional and contextual experience can colour intent formation.
layout(std430, binding = 9) buffer MemoryRuntime
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
	int   ltmReadyCount;
	int   captureBlocked;     // 1 = brainstem blocked capture this frame
	int   rtPad1;
};

// INTENT CONSTANTS : these constants represent different types of dialogue intents that the GPU mind can have. They can be used to control the flow of dialogue and determine how the GPU mind responds to different situations.

const int INTENT_NONE         = 0; // No specific intent, neutral dialogue
const int INTENT_STATUS       = 1; // Providing information about the current status or situation
const int INTENT_IDLE_CHECK   = 2; // Checking for idle status or inactivity
const int INTENT_ACTION_FLAG  = 3; // Indicating a desire to take action or perform a task
const int INTENT_CONTEXT_SHIFT= 4; // Indicating a shift in context or topic of conversation
const int INTENT_UPTIME       = 5; // Providing information about uptime or system status
const int INTENT_HELP         = 6; // Offering help or assistance
const int INTENT_QUIT         = 7; // Indicating a desire to quit or end the conversation
const int INTENT_PHONEME      = 8; // Used for generating phoneme data for speech synthesis

const int TONE_NEUTRAL   = 0;
const int TONE_POSITIVE  = 1;
const int TONE_NEGATIVE  = 2;
const int TONE_ANGRY     = 3;
const int TONE_HAPPY     = 4;
const int TONE_SAD       = 5;
const int TONE_FEARFUL   = 6;
const int TONE_DISGUSTED = 7;
const int TONE_SURPRISED = 8;

const int URGENCY_NONE   = 0;
const int URGENCY_LOW    = 1;
const int URGENCY_MEDIUM = 2;
const int URGENCY_HIGH   = 3;

const int CONFIDENCE_NONE   = 0;
const int CONFIDENCE_LOW    = 1;
const int CONFIDENCE_MEDIUM = 2;
const int CONFIDENCE_HIGH   = 3;

const int CMD_NONE   = 0;
const int CMD_STATUS = 1;
const int CMD_HELP   = 2;
const int CMD_UPTIME = 3;
const int CMD_QUIT   = 4;
const int CMD_PLAYX  = 5;

uniform float deltaTime;

#diasem

void DialogueSemanticUpdate()
{
	intent = INTENT_NONE;
	tone = emotionalState;
	urgency = decisionValue;
	confidence = 0.5;

	if(idleTimer > 5.0 && idleTimer < 10.0)
	{
		intent = INTENT_IDLE_CHECK;
		confidence = 0.7;
	}

	if(decisionValue > 0.5)
	{
		intent = INTENT_ACTION_FLAG;
		urgency = 1.0;
		confidence = 0.9;
	}

	if(contextValue > 0.3)
	{
		intent = INTENT_CONTEXT_SHIFT;
		confidence = 0.6;
	}
}

void main()
{
	DialogueSemanticUpdate();
}

#dianeuro

void DialoguePersonalityUpdate()
{
}

void DialogueMemoryUpdate()
{
	// Tone: map avg emotional state [0,1] → [-1,1] and blend it into the current tone.
	// High emotional memory pushes tone toward stress; low pushes toward neutrality.
	float memTone = clamp(avgEmotionalState * 2.0 - 1.0, -1.0, 1.0);
	tone = clamp(mix(tone, memTone, 0.3), -1.0, 1.0);

	// Urgency floor: emotional memory raises the minimum urgency so that
	// even a low-priority intent carries the weight of accumulated stress.
	urgency = clamp(max(urgency, avgEmotionalState * 0.5), 0.0, 1.0);

	// Confidence: a rich contextual memory provides interpretive grounding,
	// increasing confidence in whatever intent was formed.
	confidence = clamp(confidence + avgContextValue * 0.2, 0.0, 1.0);
}

void main()
{
	DialoguePersonalityUpdate();
	DialogueMemoryUpdate();
}

#dialogue1

void ClearOutput()
{
	charCount = 0;
}

void PushChar(uint c)
{
	if(charCount < 1024)
		text[charCount++] = c;
}

void WriteLiteralUnknown()
{
	ClearOutput();
	const uint msg[] = uint[]('U','N','K','N','O','W','N');
	for(int i = 0; i < msg.length(); i++)
		PushChar(msg[i]);
}

// Looks up the pre-processed display text for the current intent
// from the DialogueTemplates buffer (indexed directly by intent value).
// offsets[intent] == -1 means no template is registered for that intent.
void WriteIntent()
{
	ClearOutput();
	if(intent < 0 || intent >= 128) return;
	int off = offsets[intent];
	if(off < 0) return;
	int i = off;
	while(i < 8192 && data[i] != 0u)
	{
		PushChar(data[i]);
		i++;
	}
}

void DialogueStringUpdate()
{
	charCount = 0;

	switch(intent)
	{
		case INTENT_NONE:
		break;
		case INTENT_IDLE_CHECK:
		case INTENT_ACTION_FLAG:
		case INTENT_CONTEXT_SHIFT:
		case INTENT_PHONEME:
			ClearOutput();
			break;

		case INTENT_STATUS:
			WriteIntent();
			break;
		case INTENT_UPTIME:
			WriteIntent();
			break;
		case INTENT_HELP:
			WriteIntent();
			break;
		case INTENT_QUIT:
			WriteIntent();
			break;

		default:
			WriteLiteralUnknown();
			break;
	};
}

void main()
{
	DialogueStringUpdate();
}



#diainp

void DialogueInputUpdate()
{
	switch(commandCode)
	{
		case CMD_NONE:
			break;
		case CMD_STATUS:
			intent = INTENT_STATUS;
			commandCode = CMD_NONE;
			break;
		case CMD_HELP:
			intent = INTENT_HELP;
			commandCode = CMD_NONE;
			break;
		case CMD_UPTIME:
			intent = INTENT_UPTIME;
			commandCode = CMD_NONE;
			break;
		case CMD_QUIT:
			intent = INTENT_QUIT;
			systemOK = 0;
			commandCode = CMD_NONE;
			break;
		case CMD_PLAYX:
			intent = INTENT_PHONEME;
			commandCode = CMD_NONE;
			break;
		default:
			break;
	}
}

void main()
{
	DialogueInputUpdate();
}