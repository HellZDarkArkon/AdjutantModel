#ifndef LANGUAGE_INPUT_CORTEX_H
#define LANGUAGE_INPUT_CORTEX_H

#include <cstdint>
#include <vector>

#include "AuditoryCortex.h"
#include "Voice/Language/LanguageCortex.h"

// ---------------------------------------------------------------------------
// LanguageInputCortex
// Bridges the microphone stream (AuditoryCortex) to the linguistic feature
// extractor (LanguageCortex) without requiring a full ASR pass.
//
// Pipeline per utterance:
//   1. VAD       — RMS threshold gating detects speech onset and offset.
//   2. Buffer    — tracks utterance duration while speech is active.
//   3. Snapshot  — on offset, pulls the utterance PCM from AuditoryCortex.
//   4. Analyse   — extracts acoustic proxies for the LanguageCortex vector:
//                    • syllableCount  — energy-envelope peak count (10 ms frames)
//                    • avgF0Hz        — autocorrelation pitch estimate (80–400 Hz)
//                    • stressEntropy  — 3-bin Shannon entropy of the RMS envelope
//                    • phraseType     — rising/falling terminal F0 heuristic
//   5. Commit    — calls LanguageCortex::Analyze() to mark the feature vector
//                  dirty so AdjutantEngine::UpdateMindGPU() uploads it next tick.
//
// Call Init() after both AuditoryCortex and LanguageCortex are ready,
// then call Update(dt) every frame from AdjutantEngine::Update().
// ---------------------------------------------------------------------------
class LanguageInputCortex
{
public:
	// Tunable VAD parameters — adjust at runtime if needed
	float vadThreshold    = 0.02f; // RMS onset/offset level [0,1]
	float silenceTimeout  = 0.40f; // seconds of sub-threshold RMS that closes an utterance
	float minUtteranceSec = 0.15f; // shorter bursts are discarded as noise

	// Bind to the owning AuditoryCortex and LanguageCortex instances.
	// Both must outlive this object.
	void Init(AuditoryCortex& auditory, LanguageCortex& language);

	// Advance the VAD state machine by one frame. Call every frame.
	void Update(double dt);

	// ---------------------------------------------------------------------------
	// VAD state — exposed for diagnostics / UI
	// ---------------------------------------------------------------------------
	enum class VadState { SILENT, SPEAKING, PROCESSING };
	VadState GetVadState()     const { return mVadState;     }
	double   GetUtteranceSec() const { return mUtteranceSec; }

private:
	// Commit the current utterance buffer to LanguageCortex
	void AnalyseAndCommit();

	// ---------------------------------------------------------------------------
	// Acoustic feature helpers — operate on mono int16 PCM at the device rate
	// ---------------------------------------------------------------------------
	float      EstimateF0(const int16_t* samples, int count, int sampleRate);
	int        CountSyllables(const int16_t* samples, int count, int sampleRate);
	float      ComputeStressEntropy(const int16_t* samples, int count, int sampleRate);
	PhraseType InferPhraseType(const int16_t* samples, int count, int sampleRate);

	AuditoryCortex* mAuditory = nullptr;
	LanguageCortex* mLanguage = nullptr;

	VadState             mVadState     = VadState::SILENT;
	double               mSilenceTimer = 0.0; // seconds of continuous sub-threshold RMS
	double               mUtteranceSec = 0.0; // total seconds of active speech so far
	std::vector<int16_t> mUtteranceBuf;        // PCM snapshot filled on PROCESSING entry
};

#endif // LANGUAGE_INPUT_CORTEX_H
