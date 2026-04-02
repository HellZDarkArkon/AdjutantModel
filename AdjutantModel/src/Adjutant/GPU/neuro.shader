#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER  (binding 0)
// Shared read/write — neuroinfluence writes to it, neuroupdate reads it.
// =================================================================

layout(std430, binding = 0) buffer CoreMindState
{
    float idleTimer;
    float emotionalState;
    float contextValue;
    float decisionValue;
};

// =================================================================
// NEUROCHEMICAL STATE BUFFER  (binding 12)
// Six neurotransmitter / hormone axes that quantify internal drives.
//
//   reward   — dopamine / endorphin    [0,1]  reinforcement signal
//   threat   — cortisol / adrenaline   [0,1]  stress / danger response
//   novelty  — dopamine / NE           [0,1]  curiosity / alertness
//   focus    — NE / acetylcholine      [0,1]  attentional load
//   fatigue  — adenosine               [0,1]  tiredness / rest drive
//   social   — oxytocin / serotonin    [0,1]  connection need
//
// Written by #neuroupdate (Arc 6) and read by #neuroinfluence (Arc 5).
// =================================================================

layout(std430, binding = 12) buffer NeurochemState
{
    float reward;
    float threat;
    float novelty;
    float focus;
    float fatigue;
    float social;
    float neuroPad0;
    float neuroPad1;
};

// =================================================================
// SENSORY AVERAGE STATE BUFFER  (binding 13)
// Slow path: time-averaged perception. The influence pass writes
// hormone-derived tints here; it must never write to CoreMindState.
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

// =================================================================
// NEURO PARAMETERS
// Decay rates are per-second (multiplied by deltaTime each frame).
// Each axis decays toward its resting baseline.
// =================================================================

const float NEURO_REWARD_DECAY    = 0.15;  // reward fades quickly  (~4s half-life)
const float NEURO_THREAT_DECAY    = 0.10;  // threat lingers longer  (~7s)
const float NEURO_NOVELTY_DECAY   = 0.25;  // novelty is transient   (~3s)
const float NEURO_FOCUS_DECAY     = 0.05;  // focus is sustained     (~14s)
const float NEURO_FATIGUE_DECAY   = 0.02;  // fatigue very persistent (~35s)
const float NEURO_SOCIAL_BASELINE = 0.5;   // social drifts to mid-range
const float NEURO_SOCIAL_RATE     = 0.04;  // rate of drift toward social baseline
const float NEURO_IDLE_NORM       = 30.0;  // idle seconds that map to full fatigue drive
const float NEURO_SOCIAL_IDLE     = 10.0;  // idle seconds before social need begins climbing

uniform float deltaTime;

// =================================================================
// UPDATE SECTION
// Dispatched last each frame (Arc 6 — State → Neuro).
// Per-frame decay toward baselines followed by CoreMindState-driven
// excitations: the live mind state feeds back into hormonal levels.
// =================================================================

#neuroupdate

void NeuroDecay()
{
    float dt = clamp(deltaTime, 0.0, 1.0);
    reward  = max(reward  - NEURO_REWARD_DECAY  * dt, 0.0);
    threat  = max(threat  - NEURO_THREAT_DECAY  * dt, 0.0);
    novelty = max(novelty - NEURO_NOVELTY_DECAY * dt, 0.0);
    focus   = max(focus   - NEURO_FOCUS_DECAY   * dt, 0.0);
    fatigue = max(fatigue - NEURO_FATIGUE_DECAY  * dt, 0.0);
    social  = mix(social,  NEURO_SOCIAL_BASELINE, NEURO_SOCIAL_RATE * dt);
}

void NeuroDrivesFromCore()
{
    float dt = clamp(deltaTime, 0.0, 1.0);

    // Sustained idle → fatigue accumulates; focus drains
    float idleNorm = clamp(idleTimer / NEURO_IDLE_NORM, 0.0, 1.0);
    fatigue = clamp(fatigue + idleNorm * 0.05 * dt, 0.0, 1.0);
    focus   = clamp(focus   - idleNorm * 0.03 * dt, 0.0, 1.0);

    // Decision pressure → threat spike + focused attention
    if (decisionValue > 0.5)
    {
        float excess = decisionValue - 0.5;
        threat = clamp(threat + excess * 0.4 * dt, 0.0, 1.0);
        focus  = clamp(focus  + excess * 0.6 * dt, 0.0, 1.0);
    }

    // Post-decision relief (high emotional state, low decision) → reward pulse
    if (emotionalState > 0.4 && decisionValue < 0.3)
        reward = clamp(reward + emotionalState * 0.2 * dt, 0.0, 1.0);

    // Sustained emotional stress → threat
    if (emotionalState > 0.6)
        threat = clamp(threat + (emotionalState - 0.6) * 0.3 * dt, 0.0, 1.0);

    // Active context engagement → novelty + focus
    if (contextValue > 0.2)
    {
        novelty = clamp(novelty + contextValue * 0.15 * dt, 0.0, 1.0);
        focus   = clamp(focus   + contextValue * 0.10 * dt, 0.0, 1.0);
    }

    // Prolonged idle with no interaction → rising social need
    if (idleTimer > NEURO_SOCIAL_IDLE)
        social = clamp(social + 0.02 * dt, 0.0, 1.0);

    // Fatigue suppresses reward and focus
    reward = clamp(reward - fatigue * 0.05 * dt, 0.0, 1.0);
    focus  = clamp(focus  - fatigue * 0.08 * dt, 0.0, 1.0);
}

void main()
{
    NeuroDecay();
    NeuroDrivesFromCore();
}

// =================================================================
// INFLUENCE SECTION
// Dispatched first each frame (Arc 5 — Neuro → State), before the
// core pipeline. Neuro levels exert a gentle attractor pull on
// CoreMindState so accumulated hormonal state biases live perception
// without overriding real-time signals.
// =================================================================

#neuroinfluence

void NeuroInfluenceState()
{
    // Arc 5 — Neuro → Slow Path: hormone/neurotransmitter levels tint the
    // contextual (slow) perception layer. CoreMindState (binding 0) is never
    // modified here — the fast path remains immune to derived/averaged signals.
    float t = clamp(deltaTime * 0.4, 0.0, 0.4);

    // Net stress biases the slow emotional baseline upward.
    float neuroStress = clamp(threat + social * 0.15 - reward * 0.4, 0.0, 1.0);
    slowEmotional = clamp(mix(slowEmotional, max(slowEmotional, neuroStress), t), 0.0, 1.0);

    // Focus and novelty drive contextual engagement in the slow layer.
    float neuroCtx = clamp(focus * 0.5 + novelty * 0.5, 0.0, 1.0);
    slowContext    = clamp(mix(slowContext, max(slowContext, neuroCtx * 0.6), t), 0.0, 1.0);

    // Fatigue softly suppresses the slow decision baseline.
    slowDecision = clamp(slowDecision * (1.0 - fatigue * 0.15), 0.0, 1.0);
}

void main()
{
    NeuroInfluenceState();
}
