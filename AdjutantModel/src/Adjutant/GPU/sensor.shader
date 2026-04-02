#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER  (binding 0)
// Fast path: immediate sensory data.
// Read-only in this pass — SensorAvgUpdate never writes to binding 0.
// =================================================================

layout(std430, binding = 0) buffer CoreMindState
{
    float idleTimer;
    float emotionalState;
    float contextValue;
    float decisionValue;
};

// =================================================================
// SENSORY AVERAGE STATE BUFFER  (binding 13)
// Slow path: exponential moving average (EMA) of CoreMindState.
//
// halfLife controls the EMA time constant (seconds):
//   halfLife = 1s  → highly responsive; closes 50% of the gap in 1s
//   halfLife = 5s  → nominal; smooth contextual perception baseline
//   halfLife = 20s → very inertial; long-horizon mood accumulation
//
// Design contract:
//   - #sensoravg is the sole writer of the slow* fields.
//   - Influence passes (#meminfluence, #neuroinfluence) may add
//     contextual tints on top after the EMA step.
//   - Nothing in the pipeline may write averaged or derived data
//     back to CoreMindState (binding 0).
// =================================================================

layout(std430, binding = 13) buffer SensoryAvgState
{
    float slowIdleTimer;   // EMA of idleTimer
    float slowEmotional;   // EMA of emotionalState
    float slowContext;     // EMA of contextValue
    float slowDecision;    // EMA of decisionValue
    float halfLife;        // seconds; tunable EMA time constant (default 5.0)
    float saPad0;
    float saPad1;
    float saPad2;
};

uniform float deltaTime;

// =================================================================
// SENSOR AVG UPDATE
// Arc 7 — Fast → Slow: per-frame EMA update using a tunable half-life.
// Dispatched immediately after the full core pipeline (core/emo/ctx/state)
// and before any influence passes or the decision kernel, so the slow
// path always tracks the fully-resolved fast state of the current frame.
//
// EMA formula: alpha = 1 - exp(-ln(2) * dt / halfLife)
// At t = halfLife the slow value has closed 50% of its gap to the
// current fast value, matching the standard definition of a half-life.
// =================================================================

#sensoravg

void SensorAvgUpdate()
{
    float hl    = max(halfLife, 0.01);
    float alpha = clamp(1.0 - exp(-0.693147 * deltaTime / hl), 0.0, 1.0);

    slowIdleTimer = mix(slowIdleTimer, idleTimer,      alpha);
    slowEmotional = mix(slowEmotional, emotionalState, alpha);
    slowContext   = mix(slowContext,   contextValue,   alpha);
    slowDecision  = mix(slowDecision,  decisionValue,  alpha);
}

void main()
{
    SensorAvgUpdate();
}
