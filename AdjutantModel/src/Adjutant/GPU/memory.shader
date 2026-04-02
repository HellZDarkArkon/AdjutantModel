#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER  (binding 0)
// Shared read/write -- capture reads from it, inject writes to it.
// =================================================================

layout(std430, binding = 0) buffer CoreMindState
{
    float idleTimer;
    float emotionalState;
    float contextValue;
    float decisionValue;
};

// =================================================================
// MEMORY COMMAND BUFFER  (binding 7)
// CPU writes before dispatching; GPU consumes and resets memCommand.
// =================================================================

layout(std430, binding = 7) buffer MemoryCommand
{
    int memCommand;
    int memSlot;     // slot index for MEM_CMD_INJECT
    int memTimeCode; // HHMM supplied by CPU
    int memDateCode; // YYYYMMDD supplied by CPU
};

// =================================================================
// MEMORY DATA BUFFER  (binding 8)
// STM ring buffer. CPU reads back for .mem serialisation and LTM export.
// =================================================================

#define MAX_MEM_ENTRIES 64

struct GPUMemoryEntry
{
    int   index;          // 1-based slot index
    int   timeCode;       // HHMM at write time
    int   dateCode;       // YYYYMMDD at write time
    int   flags;          // bit0=ltmReady  bit1=eventBoundary  bit2=novel
    float idleTimer;      // delta-t averaged at write time
    float emotionalState; // delta-t averaged at write time
    float contextValue;   // delta-t averaged at write time
    float decisionValue;  // delta-t averaged at write time
    float salienceScore;
    float priorityScore;
    float noveltyScore;
    float timeAge;        // seconds elapsed since this slot was written
};

layout(std430, binding = 8) buffer MemoryData
{
    int            memCount;
    int            memWriteHead; // ring-buffer write cursor
    int            memPad0;
    int            memPad1;
    GPUMemoryEntry memEntries[MAX_MEM_ENTRIES];
};

// =================================================================
// MEMORY RUNTIME BUFFER  (binding 9)
// Internal per-frame state. CPU polls writeFlag / ltmReadyCount.
// =================================================================

layout(std430, binding = 9) buffer MemoryRuntime
{
    float avgIdleTimer;
    float avgEmotionalState;
    float avgContextValue;
    float avgDecisionValue;
    float prevIdleTimer;      // previous-frame snapshot for boundary detection
    float prevEmotionalState;
    float prevContextValue;
    float prevDecisionValue;
    float salienceScore;
    float noveltyScore;
    float priorityScore;
    int   writeFlag;          // 1 = auto-capture triggered this frame
    int   eventBoundaryFlag;  // 1 = boundary detected this frame
    int   ltmReadyCount;      // total entries with ltmReady bit set
    int   captureBlocked;     // 1 = brainstem blocked capture this frame (set by #brainstem, read here)
    int   rtPad1;
};

// =================================================================
// SENSORY AVERAGE STATE BUFFER  (binding 13)
// Slow path: time-averaged perception. The influence pass may add a
// contextual tint here; it must never write averaged data to binding 0.
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
// COMMAND CONSTANTS
// =================================================================

const int   MEM_CMD_NONE    = 0;
const int   MEM_CMD_CAPTURE = 1; // CPU-triggered manual capture
const int   MEM_CMD_INJECT  = 2; // CPU-triggered state injection

// =================================================================
// TEMPORAL INTEGRATION AND SALIENCE PARAMETERS
// =================================================================

const float MEM_SMOOTH            = 2.0;  // delta-t smoothing rate for rolling averages
const float SALIENCE_EMO_WEIGHT   = 0.4;  // emotional state contribution to salience
const float SALIENCE_DEC_WEIGHT   = 0.4;  // decision value contribution to salience
const float SALIENCE_CTX_WEIGHT   = 0.2;  // context value contribution to salience
const float EVENT_BOUNDARY_DELTA  = 0.1;  // min avg-state change to flag an event boundary
const float NOVELTY_THRESHOLD     = 0.2;  // min L1 distance from recent STM to flag novel
const int   NOVELTY_WINDOW        = 8;    // recent STM entries to compare against
const float WRITE_PRIORITY_THRESH = 0.4;  // minimum priority score to trigger auto-write
const float LTM_AGE_THRESHOLD     = 5.0;  // seconds before entry becomes LTM-eligible
const float LTM_PRIORITY_FLOOR    = 0.5;  // minimum priority for LTM promotion

uniform float deltaTime;

// =================================================================
// UPDATE SECTION
// Per-frame: temporal integration, salience, event boundary,
// novelty, priority scoring, auto-capture trigger, LTM pipeline.
// =================================================================

#memupdate

void MemoryUpdateTemporal()
{
    float t = clamp(deltaTime * MEM_SMOOTH, 0.0, 1.0);
    avgIdleTimer      = mix(avgIdleTimer,      idleTimer,      t);
    avgEmotionalState = mix(avgEmotionalState, emotionalState, t);
    avgContextValue   = mix(avgContextValue,   contextValue,   t);
    avgDecisionValue  = mix(avgDecisionValue,  decisionValue,  t);
}

void MemoryComputeSalience()
{
    salienceScore = clamp(
        avgEmotionalState * SALIENCE_EMO_WEIGHT +
        avgDecisionValue  * SALIENCE_DEC_WEIGHT +
        avgContextValue   * SALIENCE_CTX_WEIGHT,
        0.0, 1.0);
}

void MemoryDetectEventBoundary()
{
    float dEmo = abs(avgEmotionalState - prevEmotionalState);
    float dDec = abs(avgDecisionValue  - prevDecisionValue);
    float dCtx = abs(avgContextValue   - prevContextValue);
    eventBoundaryFlag = (dEmo > EVENT_BOUNDARY_DELTA ||
                         dDec > EVENT_BOUNDARY_DELTA ||
                         dCtx > EVENT_BOUNDARY_DELTA) ? 1 : 0;
    prevIdleTimer      = avgIdleTimer;
    prevEmotionalState = avgEmotionalState;
    prevContextValue   = avgContextValue;
    prevDecisionValue  = avgDecisionValue;
}

void MemoryComputeNovelty()
{
    int n = min(memCount, NOVELTY_WINDOW);
    if (n == 0) { noveltyScore = 1.0; return; }
    float minDist = 999.0;
    for (int j = 0; j < n; j++)
    {
        int i = (memWriteHead - 1 - j + MAX_MEM_ENTRIES) % MAX_MEM_ENTRIES;
        float d = (abs(avgEmotionalState - memEntries[i].emotionalState) +
                   abs(avgContextValue   - memEntries[i].contextValue)   +
                   abs(avgDecisionValue  - memEntries[i].decisionValue)) / 3.0;
        if (d < minDist) minDist = d;
    }
    noveltyScore = clamp(minDist / NOVELTY_THRESHOLD, 0.0, 1.0);
}

void MemoryComputePriority()
{
    priorityScore = clamp(
        salienceScore * 0.4 +
        noveltyScore  * 0.4 +
        float(eventBoundaryFlag) * 0.2,
        0.0, 1.0);
}

void MemoryCheckWriteTrigger()
{
    // Arc 3 — Decisions → Memory: a critical decision (decisionValue >= 0.75)
    // always forces a write, independent of salience / event-boundary scoring.
    writeFlag = (priorityScore >= WRITE_PRIORITY_THRESH ||
                 eventBoundaryFlag == 1 ||
                 decisionValue >= 0.75) ? 1 : 0;
}

void MemoryAutoCapture()
{
    if (writeFlag == 0 || captureBlocked != 0) return; // brainstem may suppress capture via BSACT_BLOCK_CAPTURE
    writeFlag = 0;
    int slot = memWriteHead % MAX_MEM_ENTRIES;
    if (memCount < MAX_MEM_ENTRIES) memCount++;
    memWriteHead = (memWriteHead + 1) % MAX_MEM_ENTRIES;
    int entryFlags = eventBoundaryFlag;
    if (noveltyScore > 0.5) entryFlags |= 2;
    memEntries[slot].index          = slot + 1;
    memEntries[slot].timeCode       = memTimeCode;
    memEntries[slot].dateCode       = memDateCode;
    memEntries[slot].flags          = entryFlags;
    memEntries[slot].idleTimer      = avgIdleTimer;
    memEntries[slot].emotionalState = avgEmotionalState;
    memEntries[slot].contextValue   = avgContextValue;
    memEntries[slot].decisionValue  = avgDecisionValue;
    memEntries[slot].salienceScore  = salienceScore;
    memEntries[slot].priorityScore  = priorityScore;
    memEntries[slot].noveltyScore   = noveltyScore;
    memEntries[slot].timeAge        = 0.0;
}

void MemoryAgeLTMPipeline()
{
    ltmReadyCount = 0;
    for (int i = 0; i < memCount; i++)
    {
        memEntries[i].timeAge += deltaTime;
        if (memEntries[i].timeAge >= LTM_AGE_THRESHOLD &&
            memEntries[i].priorityScore >= LTM_PRIORITY_FLOOR)
            memEntries[i].flags |= 1;
        if ((memEntries[i].flags & 1) != 0) ltmReadyCount++;
    }
}

void main()
{
    MemoryUpdateTemporal();
    MemoryComputeSalience();
    MemoryDetectEventBoundary();
    MemoryComputeNovelty();
    MemoryComputePriority();
    MemoryCheckWriteTrigger();
    MemoryAutoCapture();
    MemoryAgeLTMPipeline();
}

// =================================================================
// CAPTURE SECTION
// CPU-triggered manual snapshot using the current averaged state
// and the CPU-supplied time / date codes.
// =================================================================

#memcapture

void MemoryCaptureUpdate()
{
    if (memCommand != MEM_CMD_CAPTURE) return;
    int slot = memWriteHead % MAX_MEM_ENTRIES;
    if (memCount < MAX_MEM_ENTRIES) memCount++;
    memWriteHead = (memWriteHead + 1) % MAX_MEM_ENTRIES;
    int entryFlags = eventBoundaryFlag;
    if (noveltyScore > 0.5) entryFlags |= 2;
    memEntries[slot].index          = slot + 1;
    memEntries[slot].timeCode       = memTimeCode;
    memEntries[slot].dateCode       = memDateCode;
    memEntries[slot].flags          = entryFlags;
    memEntries[slot].idleTimer      = avgIdleTimer;
    memEntries[slot].emotionalState = avgEmotionalState;
    memEntries[slot].contextValue   = avgContextValue;
    memEntries[slot].decisionValue  = avgDecisionValue;
    memEntries[slot].salienceScore  = salienceScore;
    memEntries[slot].priorityScore  = priorityScore;
    memEntries[slot].noveltyScore   = noveltyScore;
    memEntries[slot].timeAge        = 0.0;
    memCommand = MEM_CMD_NONE;
}

void main()
{
    MemoryCaptureUpdate();
}

// =================================================================
// INJECT SECTION
// Restores a previously captured entry into CoreMindState.
// Syncs temporal averages and prev-frame snapshot to prevent a
// false event boundary on the next memupdate pass.
// =================================================================

#meminject

void MemoryInjectUpdate()
{
    if (memCommand != MEM_CMD_INJECT)       return;
    if (memSlot < 0 || memSlot >= memCount) return;
    idleTimer      = memEntries[memSlot].idleTimer;
    emotionalState = memEntries[memSlot].emotionalState;
    contextValue   = memEntries[memSlot].contextValue;
    decisionValue  = memEntries[memSlot].decisionValue;
    avgIdleTimer      = memEntries[memSlot].idleTimer;
    avgEmotionalState = memEntries[memSlot].emotionalState;
    avgContextValue   = memEntries[memSlot].contextValue;
    avgDecisionValue  = memEntries[memSlot].decisionValue;
    prevIdleTimer      = avgIdleTimer;
    prevEmotionalState = avgEmotionalState;
    prevContextValue   = avgContextValue;
    prevDecisionValue  = avgDecisionValue;
    memCommand = MEM_CMD_NONE;
}

void main()
{
    MemoryInjectUpdate();
}

// =================================================================
// INFLUENCE SECTION
// Dispatched first each frame, before the core pipeline.
// Arc 1 — Memory → State: the STM running averages exert a gentle
// attractor pull on the live CoreMindState, so accumulated experience
// leaves a persistent tint on perception without overriding real-time signals.
// =================================================================

#meminfluence

void MemoryInfluenceState()
{
    // Arc 1 — Memory → Slow Path: STM running averages tint the contextual
    // (slow) perception layer. CoreMindState (binding 0) is never touched here—
    // the fast path remains the sole domain of the core reactive pipeline.
    float t = clamp(deltaTime * 0.2, 0.0, 0.1);
    slowEmotional = clamp(mix(slowEmotional, avgEmotionalState, t), 0.0, 1.0);
    slowContext   = clamp(mix(slowContext,   avgContextValue,   t), 0.0, 1.0);
    slowDecision  = clamp(mix(slowDecision,  avgDecisionValue,  t), 0.0, 1.0);
}

void main()
{
    MemoryInfluenceState();
}
