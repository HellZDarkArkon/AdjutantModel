#include "LanguageInputCortex.h"

#include <cmath>
#include <iostream>

void LanguageInputCortex::Init(AuditoryCortex& auditory, LanguageCortex& language)
{
	mAuditory = &auditory;
	mLanguage = &language;
}

// ---------------------------------------------------------------------------
// Update — VAD state machine, called every frame
// ---------------------------------------------------------------------------
void LanguageInputCortex::Update(double dt)
{
	if (!mAuditory || !mLanguage) return;
	if (!mAuditory->IsCapturing()) return;

	const float rms = mAuditory->GetRMSLevel();

	switch (mVadState)
	{
	case VadState::SILENT:
		if (rms >= vadThreshold)
		{
			mVadState     = VadState::SPEAKING;
			mSilenceTimer = 0.0;
			mUtteranceSec = 0.0;
		}
		break;

	case VadState::SPEAKING:
		mUtteranceSec += dt;

		if (rms < vadThreshold)
			mSilenceTimer += dt;
		else
			mSilenceTimer = 0.0;

		if (mSilenceTimer >= silenceTimeout)
			mVadState = VadState::PROCESSING;
		break;

	case VadState::PROCESSING:
	{
		const int sr = mAuditory->GetSampleRate();
		if (mUtteranceSec >= minUtteranceSec && sr > 0)
		{
			// Pull the utterance PCM from the ring buffer tail.
			// PeekPCM always returns the most-recent N samples, which is
			// exactly the utterance that just completed.
			const int nSamples = static_cast<int>(mUtteranceSec * sr);
			mUtteranceBuf.resize(nSamples);
			const size_t got = mAuditory->PeekPCM(mUtteranceBuf.data(), nSamples);
			mUtteranceBuf.resize(got);
			AnalyseAndCommit();
		}

		mVadState     = VadState::SILENT;
		mSilenceTimer = 0.0;
		mUtteranceSec = 0.0;
		mUtteranceBuf.clear();
		break;
	}
	}
}

// ---------------------------------------------------------------------------
// AnalyseAndCommit — extract acoustic features and push to LanguageCortex
// ---------------------------------------------------------------------------
void LanguageInputCortex::AnalyseAndCommit()
{
	if (mUtteranceBuf.empty()) return;

	const int      sr  = mAuditory->GetSampleRate();
	const int16_t* pcm = mUtteranceBuf.data();
	const int      n   = static_cast<int>(mUtteranceBuf.size());

	const float      f0Hz     = EstimateF0(pcm, n, sr);
	const int        sylCount = CountSyllables(pcm, n, sr);
	const float      stressEnt = ComputeStressEntropy(pcm, n, sr);
	const PhraseType pType    = InferPhraseType(pcm, n, sr);

	// Derive proxy word/phoneme counts from syllable count (no ASR available).
	// English averages: ~1.5 syllables/word, ~3 phonemes/word.
	int wordCount = static_cast<int>(sylCount / 1.5f + 0.5f);
	if (wordCount < 1) wordCount = 1;
	const int phonemeCount = wordCount * 3;

	// Average syllable duration in ms
	const float syllDurMs = (sylCount > 0)
		? static_cast<float>(mUtteranceSec * 1000.0) / static_cast<float>(sylCount)
		: 250.0f;

	// No word strings are available without ASR — noveltyScore will remain 0.
	mLanguage->RecordWords({});
	mLanguage->Analyze(wordCount, phonemeCount, sylCount, 0,
	                   f0Hz, syllDurMs, stressEnt, pType);

	std::cout << "[LIC] utterance " << mUtteranceSec << "s | syl=" << sylCount
	          << " f0=" << f0Hz << "Hz stressH=" << stressEnt << std::endl;
}

// ---------------------------------------------------------------------------
// EstimateF0
// Naïve autocorrelation pitch estimator on the last kWinSize samples.
// Searches lags corresponding to 80–400 Hz; returns 0 if the signal is
// too quiet or no clear periodicity is found.
// ---------------------------------------------------------------------------
float LanguageInputCortex::EstimateF0(const int16_t* samples, int count, int sampleRate)
{
	if (count < 512 || sampleRate <= 0) return 0.0f;

	constexpr int kWinSize = 1024;
	const int start = (count > kWinSize) ? (count - kWinSize) : 0;
	const int n     = count - start;

	// Convert window to float
	float f[kWinSize] = {};
	float power = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		f[i]  = samples[start + i] / 32768.0f;
		power += f[i] * f[i];
	}
	power /= static_cast<float>(n);
	if (power < 1e-6f) return 0.0f; // silence — skip

	const int minLag = sampleRate / 400; // 400 Hz ceiling
	const int maxLag = (sampleRate / 80 < n - 1) ? sampleRate / 80 : n - 1; // 80 Hz floor

	float bestCorr = -1.0f;
	int   bestLag  = minLag;

	for (int lag = minLag; lag <= maxLag; ++lag)
	{
		float corr = 0.0f;
		for (int i = 0; i < n - lag; ++i)
			corr += f[i] * f[i + lag];
		if (corr > bestCorr)
		{
			bestCorr = corr;
			bestLag  = lag;
		}
	}

	return (bestCorr > 0.0f) ? static_cast<float>(sampleRate) / static_cast<float>(bestLag) : 0.0f;
}

// ---------------------------------------------------------------------------
// CountSyllables
// Divides the utterance into 10 ms energy frames and counts local peaks in
// the RMS envelope that exceed 30 % of the maximum frame energy. Each peak
// is treated as one syllable nucleus.
// ---------------------------------------------------------------------------
int LanguageInputCortex::CountSyllables(const int16_t* samples, int count, int sampleRate)
{
	if (count <= 0 || sampleRate <= 0) return 1;

	const int frameSize = sampleRate / 100; // 10 ms
	if (frameSize < 1) return 1;
	const int numFrames = count / frameSize;
	if (numFrames < 3) return 1;

	// Build RMS envelope
	float env[4096] = {};
	const int safeFrames = numFrames < 4096 ? numFrames : 4096;
	float envMax = 0.0f;

	for (int fi = 0; fi < safeFrames; ++fi)
	{
		float sum = 0.0f;
		for (int i = 0; i < frameSize; ++i)
		{
			const float v = samples[fi * frameSize + i] / 32768.0f;
			sum += v * v;
		}
		env[fi] = std::sqrt(sum / static_cast<float>(frameSize));
		if (env[fi] > envMax) envMax = env[fi];
	}

	if (envMax < 1e-5f) return 1;
	const float thresh = envMax * 0.30f;

	// Count local maxima above threshold
	int peaks = 0;
	for (int fi = 1; fi + 1 < safeFrames; ++fi)
	{
		if (env[fi] > thresh && env[fi] >= env[fi - 1] && env[fi] >= env[fi + 1])
			++peaks;
	}

	return peaks > 0 ? peaks : 1;
}

// ---------------------------------------------------------------------------
// ComputeStressEntropy
// Bins each 10 ms RMS frame into low / mid / high (thirds of max energy)
// and returns the normalised Shannon entropy of that distribution [0,1].
// High entropy (≈1) means evenly varied stress; low (≈0) means monotone.
// ---------------------------------------------------------------------------
float LanguageInputCortex::ComputeStressEntropy(const int16_t* samples, int count, int sampleRate)
{
	if (count <= 0 || sampleRate <= 0) return 0.0f;

	const int frameSize = sampleRate / 100;
	if (frameSize < 1) return 0.0f;
	const int numFrames = count / frameSize;
	if (numFrames < 3) return 0.0f;

	const int safeFrames = numFrames < 4096 ? numFrames : 4096;

	float envMax = 0.0f;
	float env[4096] = {};
	for (int fi = 0; fi < safeFrames; ++fi)
	{
		float sum = 0.0f;
		for (int i = 0; i < frameSize; ++i)
		{
			const float v = samples[fi * frameSize + i] / 32768.0f;
			sum += v * v;
		}
		env[fi] = std::sqrt(sum / static_cast<float>(frameSize));
		if (env[fi] > envMax) envMax = env[fi];
	}

	if (envMax < 1e-5f) return 0.0f;

	int bins[3] = { 0, 0, 0 };
	for (int fi = 0; fi < safeFrames; ++fi)
	{
		const float norm = env[fi] / envMax;
		if      (norm < 0.333f) bins[0]++;
		else if (norm < 0.667f) bins[1]++;
		else                    bins[2]++;
	}

	// Shannon entropy, normalised by log2(3)
	float H = 0.0f;
	for (int b = 0; b < 3; ++b)
	{
		if (bins[b] > 0)
		{
			const float p = static_cast<float>(bins[b]) / static_cast<float>(safeFrames);
			H -= p * std::log2(p);
		}
	}

	constexpr float kMaxH = 1.58496f; // log2(3)
	return H / kMaxH;
}

// ---------------------------------------------------------------------------
// InferPhraseType
// Compares the mean F0 of the first third of the utterance to the last third.
// Rising terminal F0 (× 1.15) → INTERROGATIVE.
// Falling terminal F0 (× 0.85) → DECLARATIVE.
// Otherwise → NEUTRAL.
// ---------------------------------------------------------------------------
PhraseType LanguageInputCortex::InferPhraseType(const int16_t* samples, int count, int sampleRate)
{
	if (count < 3 * 512 || sampleRate <= 0) return PhraseType::NEUTRAL;

	const int third = count / 3;
	const float f0Early = EstimateF0(samples,             third, sampleRate);
	const float f0Late  = EstimateF0(samples + 2 * third, third, sampleRate);

	if (f0Early < 10.0f || f0Late < 10.0f) return PhraseType::NEUTRAL;
	if (f0Late > f0Early * 1.15f)           return PhraseType::INTERROGATIVE;
	if (f0Late < f0Early * 0.85f)           return PhraseType::DECLARATIVE;
	return PhraseType::NEUTRAL;
}
