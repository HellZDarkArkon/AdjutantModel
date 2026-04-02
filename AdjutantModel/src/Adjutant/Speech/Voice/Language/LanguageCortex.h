#ifndef LANGUAGE_CORTEX_H
#define LANGUAGE_CORTEX_H

#include "../Vocalics/PhraseContour.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// LinguisticFeatures
// CPU mirror of the LinguisticState SSBO (binding 14): 16 floats = 64 bytes.
// Field order must match the std430 layout declared in language.shader exactly.
// ---------------------------------------------------------------------------
struct LinguisticFeatures
{
	float wordCount       = 0.0f; // words in the utterance
	float phonemeCount    = 0.0f; // total phonemes
	float syllableCount   = 0.0f; // total syllables
	float oovCount        = 0.0f; // out-of-vocabulary words
	float oovRate         = 0.0f; // oovCount / wordCount
	float avgF0Norm       = 0.0f; // mean realized F0, normalized [0,1] (÷500 Hz)
	float avgSyllDurNorm  = 0.0f; // mean syllable duration, normalized [0,1] (÷500 ms)
	float stressEntropy   = 0.0f; // distribution entropy over PRIMARY/SECONDARY/UNSTRESSED [0,1]
	float phraseTypeFlag  = 0.0f; // 0=DECL 1=INTER 2=EXCL 3=CONT 4=NEUTRAL
	float noveltyScore    = 0.0f; // fraction of content not seen in recent window [0,1]
	float complexityScore = 0.0f; // composite: syl/word ratio + stressEntropy [0,1]
	float utteranceReady  = 0.0f; // handshake: 1.0 = GPU should integrate; GPU clears to 0.0
	float lsPad0          = 0.0f;
	float lsPad1          = 0.0f;
	float lsPad2          = 0.0f;
	float lsPad3          = 0.0f;
};

static_assert(sizeof(LinguisticFeatures) == 16 * sizeof(float),
              "LinguisticFeatures must be exactly 64 bytes");

// ---------------------------------------------------------------------------
// LanguageCortex
// Extracts linguistic features from a spoken utterance after the phonological
// analysis pass and prepares them for GPU upload via AdjutantEngine.
//
// Owned by Language (for dat-loaded pipeline use) or directly by
// AdjutantEngine (for live SpeechEngine synthesis path).
//
// Usage:
//   // After SpeakLine() Pass 1 analysis:
//   cortex.RecordWords(wordTexts);       // update novelty window first
//   cortex.Analyze(wordCount, phonemeCount, syllableCount, oovCount,
//                  avgF0Hz, avgSyllDurMs, stressEntropy, dominantPhrase);
//
//   // In AdjutantEngine::UpdateMindGPU, before langUpdateProgram dispatch:
//   if (cortex.IsDirty()) {
//       glBindBuffer(GL_SHADER_STORAGE_BUFFER, linguisticStateBuffer);
//       glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(LinguisticFeatures),
//                       &cortex.GetFeatures());
//       cortex.ClearDirty();
//   }
// ---------------------------------------------------------------------------
class LanguageCortex
{
public:
	// -----------------------------------------------------------------------
	// Feature extraction
	// Call after prosodic analysis (phrase contour + pitch accent) is complete
	// so that phraseF0Bases and stress distributions are finalized.
	// Always call RecordWords() first so that noveltyScore is included.
	// -----------------------------------------------------------------------
	void Analyze(int wordCount, int phonemeCount, int syllableCount, int oovCount,
	             float avgF0Hz, float avgSyllDurMs, float stressEntropy,
	             PhraseType dominantPhrase)
	{
		float wc = (float)wordCount;

		mFeatures.wordCount      = wc;
		mFeatures.phonemeCount   = (float)phonemeCount;
		mFeatures.syllableCount  = (float)syllableCount;
		mFeatures.oovCount       = (float)oovCount;
		mFeatures.oovRate        = (wc > 0.5f) ? (float)oovCount / wc : 0.0f;
		mFeatures.avgF0Norm      = Clamp01(avgF0Hz     / 500.0f);  // 500 Hz ceiling
		mFeatures.avgSyllDurNorm = Clamp01(avgSyllDurMs / 500.0f); // 500 ms ceiling
		mFeatures.stressEntropy  = Clamp01(stressEntropy);
		mFeatures.phraseTypeFlag = PhraseTypeToFlag(dominantPhrase);

		// Complexity: syllable-per-word ratio (1=min, 5+=max) blended with stress entropy
		float sylPerWord = (wc > 0.5f)
			? Clamp01(((float)syllableCount / wc - 1.0f) / 4.0f)
			: 0.0f;
		mFeatures.complexityScore = Clamp01(sylPerWord * 0.5f + stressEntropy * 0.5f);

		mFeatures.utteranceReady  = 1.0f;
		mDirty = true;
	}

	// -----------------------------------------------------------------------
	// Vocabulary novelty tracking
	// Call with the list of recognised word strings BEFORE Analyze() so that
	// the computed noveltyScore is included in the analysis.
	// -----------------------------------------------------------------------
	void RecordWords(const std::vector<std::string>& words)
	{
		if (words.empty()) { mFeatures.noveltyScore = 0.0f; return; }

		int novelCount = 0;
		for (const auto& w : words)
		{
			if (std::find(mRecentWords.begin(), mRecentWords.end(), w)
				== mRecentWords.end())
				novelCount++;
		}
		mFeatures.noveltyScore = Clamp01((float)novelCount / (float)words.size());

		for (const auto& w : words)
		{
			if ((int)mRecentWords.size() >= mNoveltyWindowSize)
				mRecentWords.erase(mRecentWords.begin());
			mRecentWords.push_back(w);
		}
	}

	// -----------------------------------------------------------------------
	// State accessors
	// -----------------------------------------------------------------------
	bool                      IsDirty()     const { return mDirty; }
	const LinguisticFeatures& GetFeatures() const { return mFeatures; }
	void                      ClearDirty()        { mDirty = false; }

	void SetNoveltyWindowSize(int n) { mNoveltyWindowSize = (n > 1) ? n : 1; }
	int  GetNoveltyWindowSize() const { return mNoveltyWindowSize; }

private:
	LinguisticFeatures     mFeatures{};
	bool                   mDirty            = false;
	std::vector<std::string> mRecentWords;
	int                    mNoveltyWindowSize = 200;

	static float Clamp01(float v)
	{
		return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
	}

	static float PhraseTypeToFlag(PhraseType t)
	{
		switch (t)
		{
			case PhraseType::DECLARATIVE:   return 0.0f;
			case PhraseType::INTERROGATIVE: return 1.0f;
			case PhraseType::EXCLAMATORY:   return 2.0f;
			case PhraseType::CONTINUATION:  return 3.0f;
			default:                        return 4.0f;
		}
	}
};

#endif // LANGUAGE_CORTEX_H
