#version 430
layout(local_size_x = 1) in;

// =================================================================
// GPU MIND STATE BUFFER  (binding 0)
// Read-only in language passes — the language cortex never writes
// to CoreMindState (fast path isolation contract).
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
// Language spikes novelty, reward, and social axes here in response
// to linguistic content (novel vocabulary, complex communication,
// interrogative register).
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
// langinfluence tints the slow contextual path here.
// Binding 0 is never written by language passes.
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
// LINGUISTIC STATE BUFFER  (binding 14)
// CPU-pushed per-utterance feature vector.
//
//   lsUtteranceReady = 1.0 when the CPU has uploaded new features.
//   #langupdate consumes the features and resets lsUtteranceReady
//   to 0.0, ensuring each utterance is integrated exactly once.
//
// Field order must match LinguisticFeatures in LanguageCortex.h.
// =================================================================

layout(std430, binding = 14) buffer LinguisticState
{
    float lsWordCount;        // words in the utterance
    float lsPhonemeCount;     // total phonemes
    float lsSyllableCount;    // total syllables
    float lsOovCount;         // out-of-vocabulary words
    float lsOovRate;          // oovCount / wordCount
    float lsAvgF0Norm;        // mean realized F0, normalized [0,1] (÷500 Hz)
    float lsAvgSyllDurNorm;   // mean syllable duration, normalized [0,1] (÷500 ms)
    float lsStressEntropy;    // distribution entropy over stress levels [0,1]
    float lsPhraseTypeFlag;   // 0=DECL 1=INTER 2=EXCL 3=CONT 4=NEUTRAL
    float lsNoveltyScore;     // fraction of content not seen in recent window [0,1]
    float lsComplexityScore;  // composite: syl/word ratio + stressEntropy [0,1]
    float lsUtteranceReady;   // handshake: 1.0 = integrate; GPU resets to 0.0
    float lsPad0;
    float lsPad1;
    float lsPad2;
    float lsPad3;
};

// =================================================================
// LINGUISTIC LEARNING BUFFER  (binding 15)
// GPU-maintained exponential moving averages of utterance statistics.
// Represents Adjutant's long-horizon model of communication style.
//
// llLearningRate is the EMA alpha applied per utterance (default 0.10).
// =================================================================

layout(std430, binding = 15) buffer LinguisticLearning
{
    float llAvgWordCount;       // EMA of words per utterance
    float llAvgPhonemePerWord;  // EMA of phonemes/word ratio
    float llAvgOovRate;         // EMA of OOV rate (vocabulary novelty trend)
    float llAvgComplexity;      // EMA of complexity score
    float llAvgF0Norm;          // EMA of realized F0 (speaker register)
    float llAvgSyllDurNorm;     // EMA of syllable duration (speaking rate)
    float llUtteranceCount;     // running count of utterances processed
    float llLearningRate;       // EMA alpha for integration (default 0.10)
    float llVocabularyNorm;     // vocabulary breadth estimate [0,1]
    float llConvergenceScore;   // stability: 1 - |delta(llAvgComplexity)|
    float llPad0;
    float llPad1;
    float llPad2;
    float llPad3;
    float llPad4;
    float llPad5;
};

uniform float deltaTime;

// =================================================================
// LANGUAGE UPDATE
// Arc 8 — CPU → GPU: integrate a new utterance's features into the
// running linguistic learning statistics.
//
// Triggered only when the CPU has pushed new features
// (lsUtteranceReady = 1.0).  Integrates via EMA into binding 15,
// then fires neurochemical spikes proportional to linguistic content:
//
//   novel vocabulary  → novelty spike
//   low OOV + complex → reward pulse (successful communication)
//   interrogative     → social engagement rise
//   exclamatory       → mild threat + focus
// =================================================================

#langupdate

void LangUpdate()
{
    if (lsUtteranceReady < 0.5) return;

    float lr = clamp(llLearningRate, 0.001, 1.0);

    // EMA integration of per-utterance features into running statistics
    llAvgWordCount      = mix(llAvgWordCount,
                              lsWordCount,
                              lr);
    llAvgPhonemePerWord = mix(llAvgPhonemePerWord,
                              (lsWordCount > 0.5 ? lsPhonemeCount / lsWordCount : 0.0),
                              lr);
    llAvgOovRate        = mix(llAvgOovRate,      lsOovRate,        lr);

    float prevComplexity = llAvgComplexity;
    llAvgComplexity     = mix(llAvgComplexity,   lsComplexityScore, lr);
    llAvgF0Norm         = mix(llAvgF0Norm,        lsAvgF0Norm,       lr);
    llAvgSyllDurNorm    = mix(llAvgSyllDurNorm,   lsAvgSyllDurNorm,  lr);

    llUtteranceCount = llUtteranceCount + 1.0;

    // Convergence: how stable is the complexity EMA (1.0 = fully converged)
    float complexDelta = abs(llAvgComplexity - prevComplexity);
    llConvergenceScore = clamp(1.0 - complexDelta * 10.0, 0.0, 1.0);

    // Vocabulary breadth: saturates slowly as utterance count grows
    llVocabularyNorm = clamp(1.0 - exp(-llUtteranceCount * 0.005), 0.0, 1.0);

    // --- Neurochemical spikes from linguistic content ---

    // Novel vocabulary → curiosity / alertness
    if (lsNoveltyScore > 0.3)
        novelty = clamp(novelty + lsNoveltyScore * 0.5, 0.0, 1.0);

    // Successful complex communication → reward
    if (lsOovRate < 0.15 && lsComplexityScore > 0.3)
        reward = clamp(reward + lsComplexityScore * 0.25, 0.0, 1.0);

    // Interrogative register → social engagement rise
    if (lsPhraseTypeFlag >= 0.5 && lsPhraseTypeFlag <= 1.5)
        social = clamp(social + 0.08, 0.0, 1.0);

    // Exclamatory register → mild threat + elevated focus
    if (lsPhraseTypeFlag >= 1.5 && lsPhraseTypeFlag <= 2.5)
    {
        threat = clamp(threat + 0.05, 0.0, 1.0);
        focus  = clamp(focus  + 0.10, 0.0, 1.0);
    }

    // Consume the ready flag — prevents re-integration on the next frame
    lsUtteranceReady = 0.0;
}

void main()
{
    LangUpdate();
}

// =================================================================
// LANGUAGE INFLUENCE
// Arc 9 — Language → Slow Path: running linguistic statistics tint
// the contextual (slow) perception layer each frame.
//
// Richer / more complex language enriches contextual engagement on
// the slow path.  High sustained OOV rates lightly elevate emotional
// arousal.  A stable, converged language model nudges the slow
// decision baseline upward.
//
// CoreMindState (binding 0) is never written here.
// =================================================================

#langinfluence

void LangInfluenceState()
{
    float t = clamp(deltaTime * 0.25, 0.0, 0.25);

    // Linguistic complexity enriches the slow contextual baseline
    float langCtx = clamp(llAvgComplexity * 0.7 + (1.0 - llAvgOovRate) * 0.3, 0.0, 1.0);
    slowContext = clamp(mix(slowContext, max(slowContext, langCtx * 0.55), t), 0.0, 1.0);

    // Sustained unfamiliar vocabulary lightly elevates slow emotional arousal
    float langArousal = clamp(llAvgOovRate * 1.5, 0.0, 1.0);
    slowEmotional = clamp(mix(slowEmotional, max(slowEmotional, langArousal * 0.25), t), 0.0, 1.0);

    // Stable, converged language model softly lifts the slow decision baseline
    slowDecision = clamp(slowDecision + llConvergenceScore * 0.02 * t, 0.0, 1.0);
}

void main()
{
    LangInfluenceState();
}
