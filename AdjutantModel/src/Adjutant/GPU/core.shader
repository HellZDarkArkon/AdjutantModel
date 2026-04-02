#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER
// This buffer is used to store the state of the GPU mind, which is
// used to control the behavior of the GPU mind and its interactions
// =================================================================

layout(std430, binding = 0) buffer CoreMindState 
{
	float idleTimer;
	float emotionalState;
	float contextValue;
	float decisionValue;
};

// =================================================================
// UNIFORMS
// These uniforms are used to pass data from the CPU to the GPU, such
// as the current time, delta time, and any other relevant information
// =================================================================

uniform float deltaTime;

// =================================================================
// SENSORY AVERAGE STATE BUFFER  (binding 13)
// Slow path: exponential moving average of CoreMindState.
// Read in the decision kernel as a second independent input alongside
// the immediate fast path (binding 0). Never written from this file.
// =================================================================

layout(std430, binding = 13) buffer SensoryAvgState
{
	float slowIdleTimer;
	float slowEmotional;
	float slowContext;
	float slowDecision;
	float halfLife;
	float saPad0;
	float saPad1;
	float saPad2;
};

#core

void CoreUpdate() 
{
	idleTimer += deltaTime;
}

void main()
{
	CoreUpdate();
}

// =================================================================
// EMOTIONAL STATE COMPUTE SHADER LOGIC
// This section is responsible for updating the emotional state of the
// GPU mind based on various factors, such as idle time, context, and
// any other relevant information
// =================================================================

#emo

void EmotionUpdate() 
{
	emotionalState = mix(emotionalState, 0.5, deltaTime * 0.1); // Example: slowly move towards a neutral emotional state
}

void main()
{
	EmotionUpdate();
}

// =================================================================
// CONTEXT COMPUTE SHADER LOGIC 
// This section is responsible for updating the context value of the
// GPU mind based on various factors, such as idle time, emotional state,
// and any other relevant information
// =================================================================

#context

void ContextUpdate() 
{
	contextValue += deltaTime *0.01; // Example: slowly increase context value over time
}

void main()
{
	ContextUpdate();
}

// =================================================================
// STATE COMPUTE SHADER LOGIC
// This section is responsible for updating the state machine of the
// GPU mind based on the current mind state, emotional state, context, and
// any other relevant information
// =================================================================

#state

void StateUpdate() 
{
	if(idleTimer > 10.0) 
	{
		decisionValue = 1.0;
	} else 
	{	
		decisionValue = 0.0;
	}
}

void main()
{
	StateUpdate();
}

// =================================================================
// DECISION COMPUTE SHADER LOGIC
// This section is responsible for making decisions based on the current
// mind state, emotional state, context, and any other relevant information
// =================================================================

#dec

// Arc 2 — State → Decisions
// Two independent inputs drive decision pressure:
//   Fast path (binding 0): immediate emotionalState × contextValue — high-bandwidth,
//     reactive; responds instantly to the current sensory frame.
//   Slow path (binding 13): slowEmotional × slowContext — low-bandwidth EMA with a
//     tunable half-life; provides an inertial contextual floor that prevents
//     reactive overreaction and anchors decisions in accumulated experience.
// The two signals are never collapsed before entering this kernel. Fast (0.7)
// dominates acute response; slow (0.3) sustains contextual baseline.
void DecisionUpdate()
{
	// Fast path: high-bandwidth reactive pressure.
	float fastPressure = clamp(emotionalState * contextValue, 0.0, 1.0);

	// Slow path: contextual pressure from the time-averaged state.
	float slowPressure = clamp(slowEmotional * slowContext, 0.0, 1.0);

	// Both paths contribute independently. decisionValue converges toward
	// their weighted combination — fast dominates, slow anchors.
	float pressure = clamp(fastPressure * 0.7 + slowPressure * 0.3, 0.0, 1.0);
	decisionValue  = mix(decisionValue, pressure, clamp(deltaTime * 1.5, 0.0, 1.0));
}

void main()
{
	DecisionUpdate();
}

#output

void main()
{
	// Output logic can be implemented here, such as writing the decision value to a buffer or texture for use by the CPU or other shaders
}

	