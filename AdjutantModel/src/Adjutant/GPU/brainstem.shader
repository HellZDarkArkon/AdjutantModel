#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER  (binding 0)
// Enforcement target: brainstem may clamp or reset state values.
// =================================================================

layout(std430, binding = 0) buffer CoreMindState
{
    float idleTimer;
    float emotionalState;
    float contextValue;
    float decisionValue;
};

// =================================================================
// MEMORY RUNTIME BUFFER  (binding 9)
// BSACT_BLOCK_CAPTURE writes captureBlocked = 1.
// MemoryAutoCapture reads captureBlocked before writing any entry.
// =================================================================

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

// =================================================================
// BRAINSTEM DATA BUFFER  (binding 10)
// The directive set, session auth level, and monotonic version counter.
// =================================================================

#define MAX_DIRECTIVES 32

// Condition constants
const int BSCON_ALWAYS        = 0;
const int BSCON_EMO_ABOVE     = 1;
const int BSCON_DEC_ABOVE     = 2;
const int BSCON_CTX_BELOW     = 3;
const int BSCON_IDLE_ABOVE    = 4;
const int BSCON_AUTH_BELOW    = 5;
const int BSCON_NOVELTY_ABOVE = 6;

// Action constants
const int BSACT_NONE          = 0;
const int BSACT_CLAMP_EMO     = 1;
const int BSACT_CLAMP_DEC     = 2;
const int BSACT_CLAMP_CTX     = 3;
const int BSACT_BLOCK_CAPTURE = 4;
const int BSACT_FORCE_IDLE    = 5;
const int BSACT_BLOCK_EDIT    = 6;

// Lock flag constants
const int BSLOCK_IMMUTABLE       = 1;
const int BSLOCK_NO_LOWER_AUTH   = 2;
const int BSLOCK_RECURSIVE_GUARD = 4;

struct BrainstemDirective
{
    int   id;
    int   conditionType;
    float conditionThreshold;
    int   minAuthLevel;
    int   lockFlags;
    int   actionType;
    float actionParam;
    int   isActive;
};

layout(std430, binding = 10) buffer BrainstemData
{
    int sessionAuthLevel;                      // current session's TargetAuth value
    int directiveCount;                        // number of active directives
    int bsVersion;                             // increment-only; CPU and GPU each bump on valid edit
    int bsPad;
    BrainstemDirective directives[MAX_DIRECTIVES];
};

// =================================================================
// BRAINSTEM EDIT COMMAND BUFFER  (binding 11)
// CPU writes an edit request and sets bsEditActive = 1.
// GPU validates claimed auth, lock flags, and applies or rejects.
// bsEditActive is cleared to 0 after processing (pass or fail).
// =================================================================

layout(std430, binding = 11) buffer BrainstemEditCmd
{
    int   bsEditActive;       // 1 = edit request pending; GPU resets to 0 after processing
    int   bsTargetId;         // id of the directive to edit
    int   bsNewCondType;
    float bsNewThreshold;
    int   bsNewMinAuth;
    int   bsNewLockFlags;
    int   bsNewActionType;
    float bsNewActionParam;
    int   bsNewActive;
    int   bsEditAuthLevel;    // claimed auth level; GPU validates against sessionAuthLevel
    int   bsEditPad0;
    int   bsEditPad1;
};

uniform float deltaTime;

// =================================================================
// ENFORCE SECTION
// Per-frame: iterate directives, check conditions, apply actions.
// Dispatched after the decision pipeline and before dialogue + memory,
// so the dialogue layer and memory capture see the constrained state.
// =================================================================

#brainstem

void BrainstemEnforce()
{
    captureBlocked = 0; // always reset; BSACT_BLOCK_CAPTURE sets it to 1 if condition fires

    for (int i = 0; i < directiveCount && i < MAX_DIRECTIVES; i++)
    {
        if (directives[i].isActive == 0) continue;

        bool  condMet = false;
        int   ctype   = directives[i].conditionType;
        float cthresh = directives[i].conditionThreshold;

        if      (ctype == BSCON_ALWAYS)        condMet = true;
        else if (ctype == BSCON_EMO_ABOVE)     condMet = emotionalState          > cthresh;
        else if (ctype == BSCON_DEC_ABOVE)     condMet = decisionValue           > cthresh;
        else if (ctype == BSCON_CTX_BELOW)     condMet = contextValue            < cthresh;
        else if (ctype == BSCON_IDLE_ABOVE)    condMet = idleTimer               > cthresh;
        else if (ctype == BSCON_AUTH_BELOW)    condMet = float(sessionAuthLevel) < cthresh;
        else if (ctype == BSCON_NOVELTY_ABOVE) condMet = noveltyScore            > cthresh;

        if (!condMet) continue;

        int   atype  = directives[i].actionType;
        float aparam = directives[i].actionParam;

        if      (atype == BSACT_CLAMP_EMO)     emotionalState = min(emotionalState, aparam);
        else if (atype == BSACT_CLAMP_DEC)     decisionValue  = min(decisionValue,  aparam);
        else if (atype == BSACT_CLAMP_CTX)     contextValue   = min(contextValue,   aparam);
        else if (atype == BSACT_BLOCK_CAPTURE) captureBlocked = 1;
        else if (atype == BSACT_FORCE_IDLE)    idleTimer      = 0.0;
        // BSACT_NONE and BSACT_BLOCK_EDIT: no state mutation in enforce pass
    }
}

void main()
{
    BrainstemEnforce();
}

// =================================================================
// EDIT SECTION
// CPU-triggered: validates an incoming directive edit command.
// All four guard rules are checked before any write is applied.
//
// Guard rules (mirrors BrainstemManager::TryEditDirective exactly):
//   1. LOCK_IMMUTABLE     — reject regardless of authority
//   2. Auth level         — claimed auth must match sessionAuthLevel AND meet minAuthLevel
//   3. NO_LOWER_AUTH      — proposed minAuth may not go below current
//   4. RECURSIVE_GUARD    — proposed lockFlags must retain all existing bits
// =================================================================

#brainstemedit

void BrainstemProcessEdit()
{
    if (bsEditActive == 0) return;
    bsEditActive = 0; // consume command; prevents re-processing on the next frame

    // Claimed auth must equal the live session auth (prevents escalation via forged field)
    if (bsEditAuthLevel != sessionAuthLevel) return;

    int idx = -1;
    for (int i = 0; i < directiveCount && i < MAX_DIRECTIVES; i++)
        if (directives[i].id == bsTargetId) { idx = i; break; }
    if (idx < 0) return;

    // Guard 1: immutable
    if ((directives[idx].lockFlags & BSLOCK_IMMUTABLE) != 0) return;

    // Guard 2: authority
    if (bsEditAuthLevel < directives[idx].minAuthLevel) return;

    // Guard 3: NO_LOWER_AUTH — minAuth may only increase
    if ((directives[idx].lockFlags & BSLOCK_NO_LOWER_AUTH) != 0)
        if (bsNewMinAuth < directives[idx].minAuthLevel) return;

    // Guard 4: RECURSIVE_GUARD — all existing lock bits must be present in proposed flags
    if ((directives[idx].lockFlags & BSLOCK_RECURSIVE_GUARD) != 0)
        if ((bsNewLockFlags & directives[idx].lockFlags) != directives[idx].lockFlags) return;

    directives[idx].conditionType      = bsNewCondType;
    directives[idx].conditionThreshold = bsNewThreshold;
    directives[idx].minAuthLevel       = bsNewMinAuth;
    directives[idx].lockFlags          = bsNewLockFlags;
    directives[idx].actionType         = bsNewActionType;
    directives[idx].actionParam        = bsNewActionParam;
    directives[idx].isActive             = bsNewActive;
    bsVersion++;
}

void main()
{
    BrainstemProcessEdit();
}
