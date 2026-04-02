#ifndef PHONEME_SYNTH_H
#define PHONEME_SYNTH_H

#include "Phoneme.h"
#include "IntonationModel.h"
#include <vector>
#include <cmath>
#include <algorithm>

class PhonemeSynth
{
public:
	explicit PhonemeSynth(int sampleRate = 44100) : mSampleRate(sampleRate)
	{

	}

	double LipRadiation(double sample) const
	{
		double out = sample - mLastRadiation; // Calculate the difference between the current sample and the last radiation value to simulate lip radiation effect
		mLastRadiation = sample; // Update the last radiation value for the next iteration
		return out * 0.5; // Scale the output to prevent excessive amplitude (adjustable based on desired effect)
	}

	std::vector<short> Generate(const Phoneme& phoneme, double durationSeconds)
	{
		if (phoneme.IsConsonant())
			return GenerateConsonant(phoneme, durationSeconds);

		mTiltState = 0.0;
		mGlottalPhase = 0.0; // Reset the glottal phase for each new phoneme generation to ensure consistent sound characteristics
		size_t totalSamples = static_cast<size_t>(mSampleRate * durationSeconds); // Calculate total number of samples for the given duration
		std::vector<short> buffer(totalSamples); // Buffer to hold the generated audio samples

		double pitchHz = 210.0; // Default pitch frequency in Hz (can be adjusted based on the phoneme)
		SetupFormants(phoneme); // Setup formant filters based on the phoneme characteristics

		std::vector<double> raw(totalSamples); // Buffer to hold the raw audio samples before converting to 16-bit PCM format
		std::vector<double> filtered(totalSamples); // Buffer to hold the filtered audio samples after applying formant filters
		for (size_t i = 0; i < totalSamples; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;
			double vibratoDepth = (t > 0.050) ? 0.012 * min(1.0, (t - 0.050) / 0.080) : 0.0;
			double vibrato = 1.0 + vibratoDepth * sin(TAU * 6.2 * t);
			double s = GlottalSource(t, pitchHz*vibrato);
			driftFormants[0] = 1.0 + 0.003 * sin(TAU * 3.1 * t); // Add a small amount of drift to the first formant frequency to create a more natural sound by simulating slight variations in the vocal tract shape over time (adjustable based on desired effect)
			driftFormants[1] = 1.0 + 0.004 * sin(TAU * 2.7 * t); // Add a small amount of drift to the second formant frequency to create a more natural sound by simulating slight variations in the vocal tract shape over time (adjustable based on desired effect)
			driftFormants[2] = 1.0 + 0.002 * sin(TAU * 4.3 * t); // Add a small amount of drift to the third formant frequency to create a more natural sound by simulating slight variations in the vocal tract shape over time (adjustable based on desired effect)
			driftFormants[3] = 1.0 + 0.003 * sin(TAU * 3.5 * t); // Add a small amount of drift to the fourth formant frequency to create a more natural sound by simulating slight variations in the vocal tract shape over time (adjustable based on desired effect)
			driftFormants[4] = 1.0 + 0.004 * sin(TAU * 2.9 * t); // Add a small amount of drift to the fifth formant frequency to create a more natural sound by simulating slight variations in the vocal tract shape over time (adjustable based on desired effect)

			if (mGlottalPhase < mLastPhase)
			{
				mShimmer      = mShimmer      * 0.5  + (1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.04)  * 0.5;
				mMicroShimmer = mMicroShimmer * 0.20 + (1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.016) * 0.80;
			}
			mLastPhase = mGlottalPhase; // Update the last phase for the next iteration
			s *= mShimmer * mMicroShimmer; // Apply shimmer to the glottal source signal to create a more natural sound (adjustable based on desired effect)
			mTiltCoeff = 0.88 + 0.02 * sin(TAU * 0.7 * t);
			s = SpectralTilt(s);
			s += WhiteNoise() * 0.048;
			ApplyFormants(s, phoneme);
			filtered[i] = s; // Store the filtered sample in the buffer for potential further processing or analysis
			s *= Envelope(t, durationSeconds); // Apply an amplitude envelope to shape the sound over time
			raw[i] = s; // Store the raw sample in the buffer for potential further processing or analysis
		}

		double SumSquares = 0.0; // Variable to accumulate the sum of squares of the raw samples for RMS calculation
		for (double s : filtered) SumSquares += s * s;
		double rms = std::sqrt(SumSquares / totalSamples); // Calculate the root mean square (RMS) value of the raw audio samples for normalization
		double targetRms = 0.2; // Target RMS level for normalization (adjustable based on desired loudness)
		double gain = (rms > 1e-9) ? (targetRms / rms) : 1.0; // Calculate the gain factor for normalization, avoiding division by zero

		for (size_t i = 0; i < totalSamples; i++)
		{
			double sample = max(-1.0, min(1.0, raw[i] * gain)); // Apply gain to the raw sample and clamp it to the range [-1.0, 1.0] to prevent clipping
			buffer[i] = static_cast<short>(sample * 32767.0); // Convert the sample to 16-bit PCM format
		}

		return buffer;
	}

	// Prosodic overload: duration, F0 contour, and amplitude are driven by
	// IntonationModel output rather than fixed defaults.
	// Consonant synthesis is delegated unchanged; only vowel synthesis uses
	// the pitch glide and amplitude scale from `params`.
	std::vector<short> Generate(const Phoneme& phoneme, const PhonemeRenderParams& params)
	{
		if (phoneme.IsConsonant())
		{
			mInWord = true;
			mCurrentF0 = (params.f0Start + params.f0End) * 0.5;
			mCurrentAmpScale = params.amplitudeScale;
			return GenerateConsonant(phoneme, params.durationSeconds);
		}

		bool isFirst = !mInWord;
		mInWord = true;
		if (isFirst) { mTiltState = 0.0; }
		size_t totalSamples = static_cast<size_t>(mSampleRate * params.durationSeconds);
		std::vector<short> buffer(totalSamples);

		// Diphthong formant carry: if f1Carry > 0, this is a glide nucleus.
		// Seed filters at the carrier's formant frequencies and per-sample
		// cosine-ease to this vowel's own targets — same pattern as SynthApproximant.
		bool hasCarry = (params.f1Carry > 0.0);
		double f1t = phoneme.GetFormant1()   > 0.0 ? phoneme.GetFormant1()   : 800.0;
		double bw1 = phoneme.GetBandwidth1() > 0.0 ? phoneme.GetBandwidth1() : 80.0;
		double f2t = phoneme.GetFormant2()   > 0.0 ? phoneme.GetFormant2()   : 1200.0;
		double bw2 = phoneme.GetBandwidth2() > 0.0 ? phoneme.GetBandwidth2() : 90.0;
		double f3t = phoneme.GetFormant3()   > 0.0 ? phoneme.GetFormant3()   : 2500.0;
		double bw3 = phoneme.GetBandwidth3() > 0.0 ? phoneme.GetBandwidth3() : 120.0;
		double f4t = phoneme.GetFormant4()   > 0.0 ? phoneme.GetFormant4()   : 3200.0;
		double bw4 = phoneme.GetBandwidth4() > 0.0 ? phoneme.GetBandwidth4() : 200.0;
		double f5t = phoneme.GetFormant5()   > 0.0 ? phoneme.GetFormant5()   : 3500.0;
		double bw5 = phoneme.GetBandwidth5() > 0.0 ? phoneme.GetBandwidth5() : 250.0;
		bw2 *= 1.20;
		bw3 *= 1.30;
		bw4 *= 1.50;
		bw5 *= 2.00;
		double f1o = hasCarry ? params.f1Carry : f1t;
		double f2o = hasCarry ? params.f2Carry : f2t;
		double f3o = hasCarry ? params.f3Carry : f3t;
		if      (hasCarry && isFirst)
			SetupFormantValues(f1o, bw1, f2o, bw2, f3o, bw3, f4t, bw4, f5t, bw5);
		else if (!hasCarry && isFirst)
			SetupFormantValues(f1t, bw1, f2t, bw2, f3t, bw3, f4t, bw4, f5t, bw5);
		else if (!hasCarry)
		{
			// Continuation: update coefficients only — IIR state preserved for click-free transition
			mF1.UpdateFreq(f1t, bw1, mSampleRate);
			mF2.UpdateFreq(f2t, bw2, mSampleRate);
			mF3.UpdateFreq(f3t, bw3, mSampleRate);
			mF4.UpdateFreq(f4t, bw4, mSampleRate);
			mF5.UpdateFreq(f5t, bw5, mSampleRate);
		}

		std::vector<double> raw(totalSamples);
		std::vector<double> filtered(totalSamples);
		for (size_t i = 0; i < totalSamples; i++)
		{
			double t      = static_cast<double>(i) / mSampleRate;
			double tNorm  = (totalSamples > 1) ? ((double)i / (double)(totalSamples - 1)) : 0.0;

			// F0 glides linearly from f0Start to f0End over the phoneme window
			double pitchHz = params.f0Start + (params.f0End - params.f0Start) * tNorm;
			double vibratoDepth = (t > 0.050) ? 0.012 * min(1.0, (t - 0.050) / 0.080) : 0.0;
			double vibrato = 1.0 + vibratoDepth * sin(TAU * 6.2 * t);

			double s = GlottalSource(t, pitchHz * vibrato);
			driftFormants[0] = 1.0 + 0.003 * sin(TAU * 3.1 * t);
			driftFormants[1] = 1.0 + 0.004 * sin(TAU * 2.7 * t);
			driftFormants[2] = 1.0 + 0.002 * sin(TAU * 4.3 * t);
			driftFormants[3] = 1.0 + 0.003 * sin(TAU * 3.5 * t);
			driftFormants[4] = 1.0 + 0.004 * sin(TAU * 2.9 * t);

			if (mGlottalPhase < mLastPhase)
			{
				mShimmer      = mShimmer      * 0.5  + (1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.04)  * 0.5;
				mMicroShimmer = mMicroShimmer * 0.20 + (1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.016) * 0.80;
			}
			mLastPhase = mGlottalPhase;
			s *= mShimmer * mMicroShimmer;
			mTiltCoeff = 0.88 + 0.020 * sin(TAU * 0.7 * t);
			s  = SpectralTilt(s);
			if (hasCarry)
			{
				// Cosine-eased per-sample formant glide: carrier → this vowel's targets.
				// UpdateFreq preserves filter state so the transition is click-free.
				double ease = 0.5 - 0.5 * cos(PI * tNorm);
				mF1.UpdateFreq(f1o + (f1t - f1o) * ease, bw1, mSampleRate);
				mF2.UpdateFreq(f2o + (f2t - f2o) * ease, bw2, mSampleRate);
				mF3.UpdateFreq(f3o + (f3t - f3o) * ease, bw3, mSampleRate);
				}
				s += WhiteNoise() * 0.048;
				s = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(s)))));
				filtered[i] = s;
			// Attack only for the first vowel of a word; subsequent vowels continue
			// from the preceding phoneme (release only avoids a trailing click).
			if (isFirst && !hasCarry && !mSuppressAttack)
			{
				double atk = min(0.020, params.durationSeconds * 0.25);
				double rel = min(0.030, params.durationSeconds * 0.20);
				if (t < atk)
					s *= t / atk;
				else if (!mSuppressRelease && t > params.durationSeconds - rel)
					s *= max(0.0, (params.durationSeconds - t) / rel);
			}
			else if (!mSuppressRelease)
			{
				double rel = min(0.030, params.durationSeconds * 0.20);
				s *= (t > params.durationSeconds - rel)
					? max(0.0, (params.durationSeconds - t) / rel) : 1.0;
			}
			raw[i] = s;
		}

		double sumSq = 0.0;
		for (double s : filtered) sumSq += s * s;
		double rms       = std::sqrt(sumSq / totalSamples);
		double targetRms = 0.2 * params.amplitudeScale;
		double gain      = (rms > 1e-9) ? (targetRms / rms) : 1.0;

		for (size_t i = 0; i < totalSamples; i++)
		{
			double sample = max(-1.0, min(1.0, raw[i] * gain));
			buffer[i] = static_cast<short>(sample * 32767.0);
		}

		return buffer;
	}

	int GetSampleRate() const { return mSampleRate; }
	void BeginWord() { mInWord = false; }
	void SetSuppressEnvelopes(bool suppressAttack, bool suppressRelease)
	{
		mSuppressAttack  = suppressAttack;
		mSuppressRelease = suppressRelease;
	}

private:
	// -----------------------------------------------------------------
	// CONSONANT SYNTHESIS
	// Dispatches by ConsonantManner and uses the 5 consonant params from
	// PhonemeData (noiseLevel, noiseFreq, noiseBw, burstDuration, voicingRatio).
	// -----------------------------------------------------------------

	// Band-pass biquad filter — used to shape frication noise in-place
	struct BandPass
	{
		double b0 = 0.0, b2 = 0.0; // symmetric: b1=0
		double a1 = 0.0, a2 = 0.0;
		double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

		void Setup(double freq, double bw, int sr)
		{
			double w0 = 2.0 * 3.14159265358979 * freq / sr;
			double alpha = sin(w0) * bw / (2.0 * freq); // bw in Hz: alpha = sin(w0)/(2Q), Q = freq/bw
			double a0inv = 1.0 / (1.0 + alpha);
			b0 =  alpha * a0inv;
			b2 = -alpha * a0inv;
			a1 = -2.0 * cos(w0) * a0inv;
			a2 = (1.0 - alpha) * a0inv;
			x1 = x2 = y1 = y2 = 0.0;
		}

		double Process(double x)
		{
			double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1; x1 = x;
			y2 = y1; y1 = y;
			return y;
		}
	};

	// High-pass biquad filter (Audio EQ Cookbook, Butterworth Q = 0.707).
	// Used by SynthSibilant to remove sub-glottal energy below the sibilant spectral peak,
	// creating the characteristic spectral trough that separates the sibilant noise band
	// from low-frequency voiced energy.
	struct HighPass
	{
		double b0 = 0.0, b1 = 0.0, b2 = 0.0;
		double a1 = 0.0, a2 = 0.0;
		double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

		void Setup(double fc, int sr)
		{
			const double PI_ = 3.14159265358979323846;
			double w0    = 2.0 * PI_ * fc / sr;
			double alpha = sin(w0) / (2.0 * 0.707); // Butterworth Q
			double a0inv = 1.0 / (1.0 + alpha);
			b0 =  (1.0 + cos(w0)) * 0.5 * a0inv;
			b1 = -(1.0 + cos(w0))       * a0inv;
			b2 =  (1.0 + cos(w0)) * 0.5 * a0inv;
			a1 = -2.0 * cos(w0)         * a0inv;
			a2 = (1.0 - alpha)           * a0inv;
			x1 = x2 = y1 = y2 = 0.0;
		}

		double Process(double x)
		{
			double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
			x2 = x1; x1 = x;
			y2 = y1; y1 = y;
			return y;
		}
	};

	// Second-order all-zero FIR notch — implements a spectral anti-formant (nasal zero).
	// Transfer function: H(z) = 1 + c1*z^-1 + c2*z^-2  (poles at origin, zeros near unit circle).
	// Wide bandwidth (~600 Hz) gives a broad, natural nasal notch.
	struct AntiFormant
	{
		double c1 = 0.0, c2 = 0.0; // FIR coefficients for the spectral zero
		double x1 = 0.0, x2 = 0.0; // Delay-line state

		void Setup(double freq, double bw, int sr)
		{
			const double PI_  = 3.14159265358979323846;
			const double TAU_ = 2.0 * PI_;
			double r  = exp(-PI_ * bw / sr);
			double w0 = TAU_ * freq / sr;
			c1 = -2.0 * r * cos(w0);
			c2 = r * r;
			x1 = x2 = 0.0;
		}

		double Process(double x)
		{
			double y = x + c1 * x1 + c2 * x2;
			x2 = x1; x1 = x;
			return y;
		}
	};

	// Klatt (1980) Burst Model
	// A pressure-release impulse (narrow exponential spike at env=0, approximating
	// the Dirac δ at the release instant) is combined with spectrally pre-emphasised
	// noise on a fast exponential decay, both shaped through the place-dependent
	// front-cavity band-pass filter (Klatt 1980, §III).
	// env:  normalised position [0,1] within the burst window (0 = release instant)
	// bp:   front-cavity band-pass (stateful — advance one sample per call)
	// nl:   noise level scalar from PhonemeData
	struct BurstNoise
	{
		double tiltPrev = 0.0; // first-order pre-emphasis delay

		void Reset() { tiltPrev = 0.0; }

		double Sample(double env, BandPass& bp, double nl)
		{
			// Release impulse: narrow spike approximating the Dirac δ at the
			// moment of intraoral pressure release
			double impulse = exp(-300.0 * env);

			// Flat-spectrum noise source (pseudo-random uniform noise per Klatt)
			double n = ((rand() / (double)RAND_MAX) - 0.5) * 2.0;

			// First-order spectral pre-emphasis (α = 0.97, +6 dB/oct)
			// Models the high-frequency dominance of the burst through the short
			// front cavity; mirrors the tilt applied in Klatt's cascade path
			const double kAlpha = 0.97;
			double tilted = n - kAlpha * tiltPrev;
			tiltPrev = n;

			// Klatt burst amplitude: fast exponential decay from the release instant
			// ε = 60 → ~20 dB drop by env ≈ 0.15; burst energy gone by env ≈ 0.25
			const double kEpsilon = 60.0;
			double burstEnv = exp(-kEpsilon * env);

			// Front-cavity resonance shapes the burst spectrum (place-dependent)
			return bp.Process(impulse + tilted * burstEnv) * nl;
		}
	};

	// Rosenberg-Klatt Turbulence Model (Rosenberg 1971; Klatt 1980, §IV)
	// Turbulence noise amplitude is proportional to √Ug(t), where Ug is the
	// glottal volume velocity. An LF-consistent sinusoidal open-phase model
	// provides Ug(t): sin(π·phase/te) during the open phase [0, te=0.6T], zero
	// during the closed phase [te, T]. Voiceless fricatives treat the glottis as
	// permanently open (Ug constant → gating=1). A first-order pre-emphasis on the
	// noise source models the spectral rise of turbulent jet noise.
	struct TurbulenceNoise
	{
		double glottalPhase = 0.0; // position within pitch period [0, T)
		double tiltPrev     = 0.0; // source pre-emphasis delay

		void Setup() { glottalPhase = 0.0; tiltPrev = 0.0; }

		// t:   absolute time (s) — kept for API compatibility; internal phase is used
		// hp:  sub-constriction high-pass (stateful — advance one sample per call)
		// fp:  front-cavity band-pass (stateful — advance one sample per call)
		// nl:  noise level scalar from PhonemeData
		// vr:  voicing ratio (0 = voiceless, 1 = fully voiced)
		// f0:  fundamental frequency (Hz) — drives the Rosenberg-Klatt Ug model
		// sr:  sample rate
		double Sample(double t, BandPass& hp, BandPass& fp,
					  double nl, double vr, double f0, int sr)
		{
			(void)t;
			const double PI_ = 3.14159265358979323846;

			// Rosenberg-Klatt glottal area function:
			// Open phase  [0, te = 0.6T]: Ug = sin(π·phase/te)  (0 → peak → 0)
			// Closed phase [te, T]:       Ug = 0  (glottal closure)
			double T = 1.0 / f0;
			glottalPhase += 1.0 / sr;
			if (glottalPhase >= T) glottalPhase -= T;
			const double te = 0.6 * T;
			double Ug = (glottalPhase < te) ? sin(PI_ * glottalPhase / te) : 0.0;

			// Rosenberg-Klatt amplitude gating: noise ∝ √Ug
			// Voiceless: glottis held open (Ug constant); gating = 1
			// Voiced:    periodic modulation tracks the glottal pulsing cycle
			double gating = (vr < 0.01) ? 1.0 : sqrt(max(0.0, Ug));

			// White noise with first-order pre-emphasis (α = 0.95, +6 dB/oct)
			// models the spectral rise of turbulent jet noise at the constriction
			double n = ((rand() / (double)RAND_MAX) - 0.5) * 2.0;
			const double kAlpha = 0.95;
			double src = n - kAlpha * tiltPrev;
			tiltPrev = n;

			// Sub-constriction HP → front-cavity BP cascade
			double shaped = fp.Process(hp.Process(src));

			return shaped * nl * gating;
		}
	};

	double WhiteNoise() const
	{
		return ((rand() / (double)RAND_MAX) - 0.5) * 2.0;
	}

	// Shared per-consonant normalise + convert to PCM
	std::vector<short> NormaliseAndConvert(std::vector<double>& raw, double targetRms = 0.2) const
	{
		double ss = 0.0;
		for (double s : raw) ss += s * s;
		double rms = std::sqrt(ss / raw.size());
		double gain = (rms > 1e-9) ? (targetRms * (std::min)(1.4, mCurrentAmpScale)) / rms : 1.0;
		std::vector<short> out(raw.size());
		for (size_t i = 0; i < raw.size(); i++)
		{
			double s = max(-1.0, min(1.0, raw[i] * gain));
			out[i] = static_cast<short>(s * 32767.0);
		}
		return out;
	}

	// Nasal: Coupled Resonator + Anti-Formant model
	// Signal chain: LF source → spectral tilt → F1 murmur pole (~250 Hz)
	//               → anti-formant zero (nasal notch at ~0.75*F2)
	//               → F2 nasal formant pole (place-dependent: bilabial ~900 Hz,
	//                 alveolar ~1700 Hz, velar ~2300 Hz)
	//               → F3 upper pole → amplitude envelope
	// The anti-formant zero (FIR notch) models the spectral null produced by
	// oral-cavity coupling when the velum is lowered. It is placed just below
	// the main nasal formant pole and given a broad bandwidth (~600 Hz) to
	// match the diffuse character of measured nasal zeros.
	std::vector<short> SynthNasal(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N);

		double f1  = ph.GetFormant1()   > 0.0 ? ph.GetFormant1()   : 250.0;  // murmur pole
		double bw1 = ph.GetBandwidth1() > 0.0 ? ph.GetBandwidth1() : 50.0;
		double f2  = ph.GetFormant2()   > 0.0 ? ph.GetFormant2()   : 1000.0; // nasal formant
		double bw2 = ph.GetBandwidth2() > 0.0 ? ph.GetBandwidth2() : 100.0;
		double f3  = ph.GetFormant3()   > 0.0 ? ph.GetFormant3()   : 2200.0; // upper pole
		double bw3 = ph.GetBandwidth3() > 0.0 ? ph.GetBandwidth3() : 200.0;
		bw2 *= 1.35;
		bw3 *= 1.45;
		double nzFreq = f2 * 0.80;
		AntiFormant nasalZero;
		nasalZero.Setup(nzFreq, 600.0, mSampleRate);

		// F4/F5 are unused in nasal data (0.0); seed them with extrapolated values
		// so the filter chain is numerically well-behaved.
		SetupFormantValues(f1, bw1, f2, bw2, f3, bw3, f3 * 1.5, 300.0, f3 * 2.0, 400.0, !mInWord);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		double pitchHz = mCurrentF0;
		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;
			double s = GlottalSource(t, pitchHz) * 0.6; // nasals are low-energy murmurs
			s = SpectralTilt(s);
			s = mF1.Process(s);       // murmur pole — couples oral + nasal cavity resonance
			s = nasalZero.Process(s); // nasal zero: spectral anti-formant at ~0.80 × F2
			s = mF2.Process(s);       // nasal formant pole (place-dependent)
			s = mF3.Process(s);       // upper pole
			s *= ConsonantEnvelope(t, dur);
			raw[i] = s;
		}
		return NormaliseAndConvert(raw, 0.18);
	}

	// Plosive: Burst + Aspiration + Formant Transition model
	// Phase 1 — Closure (~45% of duration): silence; voiced plosives add a quiet pre-voicing bar.
	// Phase 2 — Burst: exponential-decay noise burst shaped by a place-dependent band-pass.
	// Phase 3 — Aspiration (VOT window): turbulent aspiration noise; voiceless = 80 ms total
	//           (burst+aspiration), voiced = short burst then voiced ramp.
	// Phase 4 — Formant transition: cosine-eased sweep from F2 locus to target over ~60 ms
	//           using UpdateFreq() per-sample so filter state is preserved across the glide.
	std::vector<short> SynthPlosive(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N, 0.0);

		double vr       = ph.GetVoicingRatio();
		double nl       = ph.GetNoiseLevel();
		double nf       = ph.GetNoiseFreq()     > 0.0 ? ph.GetNoiseFreq()  : 2000.0;
		double nbw      = ph.GetNoiseBw()       > 0.0 ? ph.GetNoiseBw()    : 2000.0;
		double burstMs  = ph.GetBurstDuration() > 0.0 ? ph.GetBurstDuration() : 10.0;
		double f0       = mCurrentF0;

		// VOT timing — all durations proportional to the phoneme window so the
		// formant-transition phase always has room even on short (80–120 ms) consonants.
		double burstSec   = min(burstMs * 0.001, dur * 0.10);
		double votSec     = (vr < 0.5) ? min(dur * 0.28, 0.055)
									   : min(dur * 0.08, 0.015);
		double closureSec = dur * 0.35;

		size_t closureSamp = static_cast<size_t>(mSampleRate * closureSec);
		size_t burstSamp   = static_cast<size_t>(mSampleRate * burstSec);
		size_t votSamp     = static_cast<size_t>(mSampleRate * votSec);
		size_t transStart  = closureSamp + burstSamp + votSamp;
		size_t transSamp   = (transStart < N)
			? min(static_cast<size_t>(mSampleRate * 0.060), N - transStart)
			: 0;

		// Burst band-pass (place-dependent)
		BandPass burstBP;
		burstBP.Setup(nf, nbw, mSampleRate);

		// Aspiration band-pass (broader, centred around 1800 Hz)
		BandPass aspirBP;
		aspirBP.Setup(1800.0, 3200.0, mSampleRate);

		// Derive F2 locus from place (encoded in NF):
		//   bilabial  NF < 2000 Hz  → locus  ~800 Hz
		//   velar     NF 2000-2999  → locus ~3000 Hz  (velar pinch: F2/F3 converge)
		//   alveolar+ NF ≥ 3000 Hz  → locus ~1800 Hz
		double f2Target = ph.GetFormant2() > 0.0 ? ph.GetFormant2() : 1500.0;
		double f2Locus  = (nf < 2000.0) ? 800.0 : (nf < 3000.0) ? 3000.0 : 1800.0;
		double f1Target = ph.GetFormant1() > 0.0 ? ph.GetFormant1() : 700.0;
		double f1Locus  = 200.0; // constricted during closure

		// Init formants at locus values (state reset before transition loop)
		double bw1 = ph.GetBandwidth1() > 0.0 ? ph.GetBandwidth1() : 80.0;
		double bw2 = ph.GetBandwidth2() > 0.0 ? ph.GetBandwidth2() : 120.0;
		double bw3 = ph.GetBandwidth3() > 0.0 ? ph.GetBandwidth3() : 200.0;
		double bw4 = ph.GetBandwidth4() > 0.0 ? ph.GetBandwidth4() : 280.0;
		double bw5 = ph.GetBandwidth5() > 0.0 ? ph.GetBandwidth5() : 350.0;
		double f3  = ph.GetFormant3()   > 0.0 ? ph.GetFormant3()   : 2500.0;
		double f4  = ph.GetFormant4()   > 0.0 ? ph.GetFormant4()   : 3300.0;
		double f5  = ph.GetFormant5()   > 0.0 ? ph.GetFormant5()   : 4000.0;

		SetupFormantValues(f1Locus, bw1, f2Locus, bw2, f3, bw3, f4, bw4, f5, bw5, !mInWord);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		BurstNoise burstNoise;
		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;
			double s = 0.0;

			if (i < closureSamp)
			{
				// Phase 1 — Closure: voiced plosives run the formant chain at sub-perceptual
				// amplitude to warm up filter state without producing an audible vowel murmur.
				// Voiceless plosives advance the glottal clock + add sub-threshold tracheal noise.
				if (vr > 0.0)
				{
					s = GlottalSource(t, f0);
					s = SpectralTilt(s);
					s = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(s)))));
					s *= vr * 0.12;
				}
				else
				{
					double g = GlottalSource(t, f0);
					(void)SpectralTilt(g);
					(void)mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(g)))));
					s = WhiteNoise() * nl * 0.015;
				}
			}
			else if (i < closureSamp + burstSamp)
			{
				// Phase 2 — Burst: Klatt (1980) burst model through place-dependent front-cavity BP
				double tBurst = static_cast<double>(i - closureSamp) / max(1.0, (double)burstSamp);
				s = burstNoise.Sample(tBurst, burstBP, nl);
				if (vr > 0.0)
				{
					double sv = GlottalSource(t, f0);
					sv = SpectralTilt(sv);
					sv = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(sv)))));
					s += sv * vr * 0.5;
				}
				else
				{
					double g = GlottalSource(t, f0);
					(void)SpectralTilt(g);
				}
			}
			else if (i < transStart)
			{
				// Phase 3 — Aspiration (VOT window)
				double tAspr = static_cast<double>(i - closureSamp - burstSamp) / max(1.0, (double)votSamp);
				if (vr < 0.5)
				{
					// Voiceless: decaying aspiration noise + glottal clock advance
					double aspEnv = exp(-2.0 * tAspr) * (1.0 - vr);
					s = aspirBP.Process(WhiteNoise()) * nl * 0.5 * aspEnv;
					double g = GlottalSource(t, f0);
					(void)SpectralTilt(g);
				}
				else
				{
					// Voiced: continue voiced formant synthesis through the VOT window
					double sv = GlottalSource(t, f0);
					sv = SpectralTilt(sv);
					sv = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(sv)))));
					s = sv * vr;
				}
			}
			else
			{
				// Phase 4 — Formant transition: cosine-eased F1/F2 locus → target
				size_t tIdx   = i - transStart;
				double tTrans = min(1.0, static_cast<double>(tIdx) / max(1.0, (double)transSamp));
				double ease   = 0.5 - 0.5 * cos(PI * tTrans);
				double f1Now  = f1Locus + (f1Target - f1Locus) * ease;
				double f2Now  = f2Locus + (f2Target - f2Locus) * ease;
				mF1.UpdateFreq(f1Now, bw1, mSampleRate);
				mF2.UpdateFreq(f2Now, bw2, mSampleRate);

				double voiced = GlottalSource(t, f0) * vr;
				double aspr   = (vr < 0.5) ? aspirBP.Process(WhiteNoise()) * nl * 0.15 * (1.0 - tTrans) : 0.0;
				s = voiced + aspr;
				s = SpectralTilt(s);
				s = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(s)))));
				if (vr < 0.5)
					s *= min(1.0, tTrans * 4.0); // fade aspiration in from silence (voiceless only)
			}

			raw[i] = s * ConsonantEnvelope(t, dur);
		}
		return NormaliseAndConvert(raw, 0.35);
	}

	// Fricative
	// Two-stage shaping: sub-constriction HP removes low-frequency energy;
	// front-cavity BP (NF/NBW from phoneme data) sculpts the frication spectrum.
	// Turbulence follows the Rosenberg-Klatt model: noise amplitude ∝ √Ug(t),
	// where Ug is the glottal volume velocity (constant for voiceless; sinusoidal
	// open-phase for voiced). Voiced fricatives additionally inject a Klatt burst
	// at each glottal closure (te = 0.6/f0), modelling the periodic aspiration
	// spike that gives voiced fricatives their characteristic rough quality.
	std::vector<short> SynthFricative(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N);

		double nf  = ph.GetNoiseFreq()  > 0.0 ? ph.GetNoiseFreq()  : 2000.0;
		double nbw = ph.GetNoiseBw()    > 0.0 ? ph.GetNoiseBw()    : 2000.0;
		double nl  = ph.GetNoiseLevel();
		double vr  = ph.GetVoicingRatio();
		double f0  = mCurrentF0; // fundamental for voiced coupling

		// Stage 1: Butterworth sub-constriction high-pass (proper spectral trough below frication band)
		double hpFc = nf * 0.25;
		HighPass hpFilt;
		hpFilt.Setup(hpFc, mSampleRate);

		// Stage 2: front-cavity band-pass (place-dependent spectral peak)
		BandPass fpFilt;
		fpFilt.Setup(nf, nbw, mSampleRate);

		// Low-pass for voiced glottal leakage (cuts above ~700 Hz)
		BandPass lpGlottal;
		lpGlottal.Setup(700.0, 1400.0, mSampleRate);

		SetupFormants(ph, !mInWord);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		// Inline Rosenberg-Klatt turbulence state (phase-synced with glottal burst detection)
		double turbGlotPhase = 0.0;
		double turbTiltPrev  = 0.0;
		double turbT         = 1.0 / f0;
		double turbTe        = 0.6 * turbT;

		BurstNoise glotBurst;
		BandPass glotBurstBP;
		glotBurstBP.Setup(600.0, 1200.0, mSampleRate);

		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;

			// Rosenberg-Klatt glottal area gating (inline, phase-synced with burst detection)
			turbGlotPhase += 1.0 / mSampleRate;
			if (turbGlotPhase >= turbT) turbGlotPhase -= turbT;
			double Ug     = (turbGlotPhase < turbTe) ? sin(PI * turbGlotPhase / turbTe) : 0.0;
			double gating = (vr < 0.01) ? 1.0 : sqrt(max(0.0, Ug));

			// Pre-emphasized white noise → HP → front-cavity BP with turbulence flutter
			double n       = WhiteNoise();
			double src     = n - 0.95 * turbTiltPrev;
			turbTiltPrev   = n;
			double flutter = 1.0 + 0.08 * sin(TAU * 6.7 * t) * sin(TAU * 2.9 * t);
			double noise   = fpFilt.Process(hpFilt.Process(src)) * nl * gating * flutter;

			// Voiced fricatives: Klatt glottal burst at closure instant (phase-synced)
			if (vr > 0.0)
			{
				double burstWin = 0.1 * turbT;
				if (turbGlotPhase >= turbTe && turbGlotPhase < turbTe + burstWin)
				{
					double burstEnv = (turbGlotPhase - turbTe) / burstWin;
					noise += glotBurst.Sample(burstEnv, glotBurstBP, nl * vr * 0.25);
				}
			}

			// Voiced component: LP-filtered glottal source
			double g = GlottalSource(t, f0);
			g = SpectralTilt(g);
			double voiced = (vr > 0.0) ? lpGlottal.Process(g) * vr * 0.35 : 0.0;
			double carry  = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(g))))) * 0.06;

			raw[i] = (noise + voiced + carry) * ConsonantEnvelope(t, dur);
		}
		return NormaliseAndConvert(raw, 0.28);
	}

	// Sibilant Fricative
	// Sibilants (/s z ʃ ʒ/) have a narrow, high-energy turbulence peak produced by a
	// turbulent jet striking the teeth ridge. The two-stage signal chain models this:
	//   1. White noise with strong pre-emphasis (+6 dB/oct) → models turbulent jet noise
	//   2. HighPass (cutoff ≈ NF×0.3) → removes sub-glottal rumble and creates the
	//      spectral trough that defines the lower edge of the sibilant band
	//   3. BandPass (NF / NBW) → sculpts the main spectral peak (front-cavity resonance)
	// Voiced sibilants add LP-filtered glottal leakage through the same BPF at 700 Hz.
	std::vector<short> SynthSibilant(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N);

		double nf  = ph.GetNoiseFreq()  > 0.0 ? ph.GetNoiseFreq()  : 5000.0;
		double nbw = ph.GetNoiseBw()    > 0.0 ? ph.GetNoiseBw()    : 3000.0;
		double nl  = ph.GetNoiseLevel();
		double vr  = ph.GetVoicingRatio();
		double f0  = mCurrentF0;

		// High-pass: cutoff at NF * 0.3 creates the spectral trough below the sibilant peak
		HighPass hpFilt;
		hpFilt.Setup(nf * 0.3, mSampleRate);

		// Front-cavity BPF: sculpts the sibilant spectral peak (place-dependent NF/NBW)
		BandPass fpFilt;
		fpFilt.Setup(nf, nbw, mSampleRate);
		// Second-stage BPF: narrows the peak for crisper sibilant character
		BandPass fpFilt2;
		fpFilt2.Setup(nf, nbw * 0.55, mSampleRate);

		// LP-filtered glottal leakage for voiced sibilants (/z/, /ʒ/)
		BandPass lpGlottal;
		lpGlottal.Setup(700.0, 1400.0, mSampleRate);

		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		double tiltPrev = 0.0;
		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;

			// White noise with strong pre-emphasis (α = 0.97) — models turbulent jet noise
			double n   = WhiteNoise();
			double src = n - 0.97 * tiltPrev;
			tiltPrev   = n;

			// HP → BP1 → BP2: spectral trough + coarse peak + narrowed sibilant peak
			double flutter = 1.0 + 0.06 * sin(TAU * 8.1 * t) * sin(TAU * 3.7 * t);
			double noise   = fpFilt2.Process(fpFilt.Process(hpFilt.Process(src))) * nl * flutter;

			// Voiced component: LP-filtered glottal source (unconditional for phase continuity)
			double g = GlottalSource(t, f0);
			g = SpectralTilt(g);
			double voiced = (vr > 0.0) ? lpGlottal.Process(g) * vr * 0.4 : 0.0;
			double carry  = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(g))))) * 0.05;

			raw[i] = (noise + voiced + carry) * ConsonantEnvelope(t, dur);
		}
		return NormaliseAndConvert(raw, 0.35);
	}

	// Affricate
	std::vector<short> SynthAffricate(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N, 0.0);

		double burstSec  = ph.GetBurstDuration() * 0.001;
		double burstEnd  = dur * 0.3 + burstSec;
		size_t burstSamples = static_cast<size_t>(mSampleRate * burstEnd);

		double nf  = ph.GetNoiseFreq()  > 0.0 ? ph.GetNoiseFreq()  : 3000.0;
		double nbw = ph.GetNoiseBw()    > 0.0 ? ph.GetNoiseBw()    : 2000.0;
		double nl  = ph.GetNoiseLevel();
		double vr  = ph.GetVoicingRatio();
		double f0  = mCurrentF0;

		// Burst band-pass (place-dependent, same as plosive burst)
		BandPass burstBP;
		burstBP.Setup(nf, nbw, mSampleRate);

		// Turbulent friction tail: sub-constriction HP → front-cavity BP
		double hpFc = nf * 0.25;
		BandPass hpFilt;
		hpFilt.Setup(hpFc, hpFc * 2.0, mSampleRate);
		BandPass fpFilt;
		fpFilt.Setup(nf, nbw, mSampleRate);

		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		BurstNoise burstNoise;
		TurbulenceNoise turbulence;
		for (size_t i = 0; i < N; i++)
		{
			double t      = static_cast<double>(i) / mSampleRate;
			double tNorm  = static_cast<double>(i) / N;
			double fricWeight  = (i > burstSamples) ? min(1.0, (tNorm - 0.3) / 0.4) : 0.0;
			double burstWeight = (i < burstSamples) ? max(0.0, 1.0 - tNorm / 0.3) : 0.0;

			double s = 0.0;

			// Burst segment: Klatt (1980) burst model through place-dependent front-cavity BP
			if (burstWeight > 0.0)
			{
				double tBurst = static_cast<double>(i) / max(1.0, (double)burstSamples);
				s += burstNoise.Sample(tBurst, burstBP, nl) * burstWeight;
			}

			// Friction tail: TurbulenceNoise generator
			if (fricWeight > 0.0)
				s += turbulence.Sample(t, hpFilt, fpFilt, nl, vr, f0, mSampleRate) * fricWeight;

			// Voiced component throughout (unconditional for phase continuity)
			double g = GlottalSource(t, f0);
			g = SpectralTilt(g);
			double voiced = g * vr * 0.3;
			double carry  = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(g))))) * 0.04;
			s = (s + voiced + carry) * ConsonantEnvelope(t, dur);
			raw[i] = s;
		}
		return NormaliseAndConvert(raw, 0.28);
	}

	// Approximant
	// Approximants (/r/, /w/, /j/, /ʋ/) are perceptually "weak vowels" at onset —
	// the vocal tract begins in a neutral (schwa-like) configuration and sweeps
	// rapidly to the approximant's own formant targets over a ~50 ms cosine-eased
	// glide using per-sample UpdateFreq() so filter state is preserved across the
	// transition. Amplitude ramps from 0.3 at onset to full over the same window,
	// capturing the characteristic low-energy onset of semivowel glides.
	std::vector<short> SynthApproximant(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N);

		// Target formants from phoneme data
		double f1t  = ph.GetFormant1()   > 0.0 ? ph.GetFormant1()   : 500.0;
		double bw1  = ph.GetBandwidth1() > 0.0 ? ph.GetBandwidth1() : 70.0;
		double f2t  = ph.GetFormant2()   > 0.0 ? ph.GetFormant2()   : 1400.0;
		double bw2  = ph.GetBandwidth2() > 0.0 ? ph.GetBandwidth2() : 100.0;
		double f3t  = ph.GetFormant3()   > 0.0 ? ph.GetFormant3()   : 2500.0;
		double bw3  = ph.GetBandwidth3() > 0.0 ? ph.GetBandwidth3() : 160.0;
		double f4t  = ph.GetFormant4()   > 0.0 ? ph.GetFormant4()   : 3200.0;
		double bw4  = ph.GetBandwidth4() > 0.0 ? ph.GetBandwidth4() : 240.0;
		double f5t  = ph.GetFormant5()   > 0.0 ? ph.GetFormant5()   : 4200.0;
		double bw5  = ph.GetBandwidth5() > 0.0 ? ph.GetBandwidth5() : 350.0;
		bw2 *= 1.15;
		bw3 *= 1.25;
		// F4/F5 are held at their target throughout — only F1/F2/F3 glide.
		const double f1o = 500.0;   // neutral F1 onset
		const double f2o = 1500.0;  // neutral F2 onset (schwa)
		const double f3o = 2500.0;  // neutral F3 onset

		// 50 ms cosine-eased transition from onset to target
		size_t transSamp = static_cast<size_t>(mSampleRate * 0.050);

		// Seed filters at onset values (state reset)
		if (!mInWord) SetupFormantValues(f1o, bw1, f2o, bw2, f3o, bw3, f4t, bw4, f5t, bw5);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }

		double pitchHz = mCurrentF0;
		for (size_t i = 0; i < N; i++)
		{
			double t     = static_cast<double>(i) / mSampleRate;
			double tNorm = min(1.0, static_cast<double>(i) / max(1.0, (double)transSamp));
			double ease  = 0.5 - 0.5 * cos(PI * tNorm);

			// Per-sample glide: onset → approximant target
			mF1.UpdateFreq(f1o + (f1t - f1o) * ease, bw1, mSampleRate);
			mF2.UpdateFreq(f2o + (f2t - f2o) * ease, bw2, mSampleRate);
			mF3.UpdateFreq(f3o + (f3t - f3o) * ease, bw3, mSampleRate);

			// Onset amplitude: ramps from 0.3 to 1.0 over transition window
			double ampScale = 0.3 + 0.7 * ease;

			double s = GlottalSource(t, pitchHz);
			s = SpectralTilt(s);
			s = mF1.Process(mF2.Process(mF3.Process(mF4.Process(mF5.Process(s)))));
			s *= ampScale * ConsonantEnvelope(t, dur);
			raw[i] = s;
		}
		return NormaliseAndConvert(raw, 0.18);
	}

	// Flap
	std::vector<short> SynthFlap(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N, 0.0);

		double closureSec = 0.020;
		size_t closureSamp = static_cast<size_t>(mSampleRate * closureSec);

		BandPass bp;
		double nf  = ph.GetNoiseFreq() > 0.0 ? ph.GetNoiseFreq() : 3000.0;
		double nbw = ph.GetNoiseBw()   > 0.0 ? ph.GetNoiseBw()   : 2000.0;
		bp.Setup(nf, nbw, mSampleRate);

		SetupFormants(ph, !mInWord);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }
		double pitchHz = mCurrentF0;

		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;
			double s;
			if (i < closureSamp)
			{
				double env = sin(3.14159265358979 * (double)i / closureSamp);
				s = bp.Process(WhiteNoise()) * ph.GetNoiseLevel() * env
				  + GlottalSource(t, pitchHz) * ph.GetVoicingRatio() * env;
			}
			else
			{
				s = GlottalSource(t, pitchHz) * ph.GetVoicingRatio();
				ApplyFormants(s, ph);
				s *= ConsonantEnvelope(t - closureSec, dur - closureSec);
			}
			raw[i] = s;
		}
		return NormaliseAndConvert(raw, 0.15);
	}

	// Trill
	std::vector<short> SynthTrill(const Phoneme& ph, double dur)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N, 0.0);

		double cycleSec = ph.GetBurstDuration() > 0.0 ? ph.GetBurstDuration() * 0.001 : 0.040;
		size_t cycleSamp = static_cast<size_t>(mSampleRate * cycleSec);

		BandPass bp;
		double nf  = ph.GetNoiseFreq() > 0.0 ? ph.GetNoiseFreq() : 3000.0;
		double nbw = ph.GetNoiseBw()   > 0.0 ? ph.GetNoiseBw()   : 2000.0;
		bp.Setup(nf, nbw, mSampleRate);

		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }
		double pitchHz = mCurrentF0;

		for (size_t i = 0; i < N; i++)
		{
			size_t phase = i % cycleSamp;
			double cycleEnv = sin(3.14159265358979 * (double)phase / cycleSamp);
			double t = static_cast<double>(i) / mSampleRate;
			double noise  = bp.Process(WhiteNoise()) * ph.GetNoiseLevel() * cycleEnv;
			double voiced = GlottalSource(t, pitchHz) * ph.GetVoicingRatio();
			raw[i] = (noise + voiced) * cycleEnv * ConsonantEnvelope(t, dur);
		}
		return NormaliseAndConvert(raw, 0.15);
	}

	// Lateral
	std::vector<short> SynthLateral(const Phoneme& ph, double dur, bool hasFrication = false)
	{
		size_t N = static_cast<size_t>(mSampleRate * dur);
		std::vector<double> raw(N);

		BandPass bp;
		if (hasFrication)
		{
			double nf  = ph.GetNoiseFreq() > 0.0 ? ph.GetNoiseFreq() : 4000.0;
			double nbw = ph.GetNoiseBw()   > 0.0 ? ph.GetNoiseBw()   : 2500.0;
			bp.Setup(nf, nbw, mSampleRate);
		}

		SetupFormants(ph, !mInWord);
		if (!mInWord) { mGlottalPhase = 0.0; mTiltState = 0.0; mShimmer = 1.0; mLastPhase = 0.0; }
		double pitchHz = mCurrentF0;
		AntiFormant latZero;
		latZero.Setup(1800.0, 900.0, mSampleRate);

		for (size_t i = 0; i < N; i++)
		{
			double t = static_cast<double>(i) / mSampleRate;
			double s = GlottalSource(t, pitchHz) * ph.GetVoicingRatio();
			s = SpectralTilt(s);
			ApplyFormants(s, ph);
			s = latZero.Process(s);
			if (hasFrication)
				s += bp.Process(WhiteNoise()) * ph.GetNoiseLevel();
			s *= ConsonantEnvelope(t, dur);
			raw[i] = s;
		}
		return NormaliseAndConvert(raw, 0.18);
	}

	std::vector<short> GenerateConsonant(const Phoneme& ph, double dur)
	{
		switch (ph.GetManner())
		{
		case ConsonantManner::NASAL:       return SynthNasal(ph, dur);
		case ConsonantManner::PLOSIVE:     return SynthPlosive(ph, dur);
		case ConsonantManner::SIBAFF:      return SynthAffricate(ph, dur);
		case ConsonantManner::AFFRICATE:   return SynthAffricate(ph, dur);
		case ConsonantManner::SIBFRIC:     return SynthSibilant(ph, dur);
		case ConsonantManner::FRICATIVE:   return SynthFricative(ph, dur);
		case ConsonantManner::APPROXIMANT: return SynthApproximant(ph, dur);
		case ConsonantManner::FLAP:        return SynthFlap(ph, dur);
		case ConsonantManner::TRILL:       return SynthTrill(ph, dur);
		case ConsonantManner::LATAFF:      return SynthLateral(ph, dur, true);
		case ConsonantManner::LATFRIC:     return SynthLateral(ph, dur, true);
		case ConsonantManner::LATAPP:      return SynthLateral(ph, dur, false);
		case ConsonantManner::LATFLAP:     return SynthFlap(ph, dur);
		default:                           return std::vector<short>(static_cast<size_t>(mSampleRate * dur), 0);
		}
	}

private:
	int mSampleRate; // Sample rate for audio generation (e.g., 44100 Hz)
	double PI = 3.14159265358979323846; // Constant for pi
	double TAU = 2.0 * PI; // Constant for tau (2 * pi)
	double mCurrentF0 = 220.0; // F0 carried from PhonemeRenderParams into consonant synthesis
	mutable double mCurrentAmpScale = 1.0; // amplitude scale carried from PhonemeRenderParams into consonant normalisation
	mutable double mLastRadiation = 0.0;
	mutable double mJitter = 1.0; // State variable for the jitter applied to the pitch to create a more natural sound (adjustable based on desired effect)
	mutable double mTiltState = 0.0; // State variable for the tilt of the glottal source, which can be used to shape the sound (adjustable based on desired voice characteristics)
	mutable double mGlottalPhase = 0.0; // State variable for the phase of the glottal source, which can be used to create more complex waveforms (adjustable based on desired voice characteristics)
	mutable double mShimmer      = 1.0;
	mutable double mMicroShimmer = 1.0;
	mutable double mLastPhase    = 0.0;
	mutable double mJitter2      = 1.0;
	mutable double mOQJitter     = 0.60;
	mutable double mTiltCoeff    = 0.88;
	mutable bool   mInWord       = false;
	bool mSuppressAttack  = false;
	bool mSuppressRelease = false;
	double driftFormants[5] = { 1.0, 1.0, 1.0, 1.0, 1.0 };

	struct FormantFilter
	{
		double a0 = 1.0, a1 = 0.0, a2 = 0.0; // Filter coefficients for the formant filter
		double b1 = 0.0, b2 = 0.0; // Feedback coefficients for the formant filter
		double x1 = 0.0, x2 = 0.0; // State variables for the input samples of the formant filter
		double y1 = 0.0, y2 = 0.0; // State variables for the output samples of the formant filter

		double Process(double x) {
			double y = a0 * x + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2; // Calculate the output sample using the filter coefficients and state variables
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;

			return y;
		}

		// Update coefficients per-sample during formant glide — state is intentionally preserved
		void UpdateFreq(double freq, double bw, int sr)
		{
			if (freq <= 0.0) return;
			const double PI_  = 3.14159265358979323846;
			const double TAU_ = 2.0 * PI_;
			double r  = exp(-PI_ * bw / sr);
			double w0 = TAU_ * freq / sr;
			a0 = 1.0 - 2.0 * r * cos(w0) + r * r;
			b1 = -2.0 * r * cos(w0);
			b2 = r * r;
		}
	};

	mutable FormantFilter mF1, mF2, mF3, mF4, mF5;

	void SetupFormantValues(double f1, double bw1, double f2, double bw2,
		double f3, double bw3, double f4, double bw4, double f5, double bw5, bool resetState = true)
	{
		auto setup = [this, resetState](FormantFilter& f, double freq, double bw)
		{
			double r  = exp(-PI * bw / mSampleRate);
			double w0 = TAU * freq / mSampleRate;
			f.a0 = 1.0 - 2.0 * r * cos(w0) + r * r;
			f.a1 = 0.0; f.a2 = 0.0;
			f.b1 = -2.0 * r * cos(w0);
			f.b2 = r * r;
			if (resetState) f.x1 = f.x2 = f.y1 = f.y2 = 0.0;
		};
		if (f1 > 0) setup(mF1, f1, bw1);
		if (f2 > 0) setup(mF2, f2, bw2);
		if (f3 > 0) setup(mF3, f3, bw3);
		if (f4 > 0) setup(mF4, f4, bw4);
		if (f5 > 0) setup(mF5, f5, bw5);
	}

	void SetupFormants(const Phoneme& phoneme, bool resetState = true)
	{
		// bw is in Hz; convert to Q = freq / bw for the filter design and use alpha = sin(w0) * sinh(log(2.0) / 2.0 * bw * w0 / sin(w0)) for the biquad filter coefficients
		auto setup = [this, resetState](FormantFilter& f, double freq, double bw)
			{
				double r = exp(-PI * bw / mSampleRate); // Calculate the pole radius for the biquad filter based on the bandwidth and sample rate
				double w0 = TAU * freq / mSampleRate; // Calculate the normalized angular frequency for the formant frequency
				double a = (1.0 - 2.0 * r * cos(w0) + r * r); // Calculate the normalization factor for the filter coefficients to ensure a gain of 1.0 at the formant frequency

				f.a0 = a; // Set the feedforward coefficient a0 based on the normalization factor
				f.a1 = 0.0; // Set the feedforward coefficient a1 to 0 for a bandpass filter
				f.a2 = 0.0; // Set the feedforward coefficient a2 to 0 for a bandpass filter
				f.b1 = -2.0 * r * cos(w0); // Set the feedback coefficient b1 based on the pole radius and angular frequency
				f.b2 = r * r; // Set the feedback coefficient b2 based on the pole radius
				if (resetState) f.x1 = f.x2 = f.y1 = f.y2 = 0.0;
					
			};
		double f1 = phoneme.GetFormant1(), bw1 = phoneme.GetBandwidth1();
		double f2 = phoneme.GetFormant2(), bw2 = phoneme.GetBandwidth2();
		double f3 = phoneme.GetFormant3(), bw3 = phoneme.GetBandwidth3();
		double f4 = phoneme.GetFormant4(), bw4 = phoneme.GetBandwidth4();
		double f5 = phoneme.GetFormant5(), bw5 = phoneme.GetBandwidth5();

		if (f1 <= 0) { f1 = 800.0; bw1 = 80.0; }
		if (f2 <= 0) { f2 = 1200.0; bw2 = 90.0; }
		if (f3 <= 0) { f3 = 2500.0; bw3 = 120.0; }
		if (f4 <= 0) { f4 = 3200.0; bw4 = 200.0; }
		if (f5 <= 0) { f5 = 3500.0; bw5 = 250.0; }

		setup(mF1, f1 * driftFormants[0], bw1);
		setup(mF2, f2 * driftFormants[1], bw2);
		setup(mF3, f3 * driftFormants[2], bw3);
		setup(mF4, f4 * driftFormants[3], bw4);
		setup(mF5, f5 * driftFormants[4], bw5);
	}

	double GlottalSource(double t, double pitchHz) const // Generate a glottal source signal based on the Liljencrants-Fant (LF) model for a given time and pitch frequency
	{
		double T = 1.0 / pitchHz; // Calculate the period of the glottal source signal based on the pitch frequency
		mGlottalPhase += 1.0 / mSampleRate;
		if (mGlottalPhase >= T * mJitter * mJitter2)
		{
			mGlottalPhase -= T * mJitter * mJitter2;
			double target1 = 1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.012;
			mJitter  = mJitter  * 0.75 + target1 * 0.25;
			double target2 = 1.0 + ((rand() / (double)RAND_MAX) - 0.5) * 0.006;
			mJitter2 = mJitter2 * 0.40 + target2 * 0.60;
			double oqTarget = 0.60 + ((rand() / (double)RAND_MAX) - 0.5) * 0.040;
			mOQJitter = mOQJitter * 0.55 + oqTarget * 0.45;
		}
		double tL = mGlottalPhase;

		const double tp = (2.0 / 3.0) * mOQJitter * T; // Time of maximum glottal flow (adjustable based on desired voice characteristics)
		const double te = mOQJitter * T;                 // Time of glottal closure (adjustable based on desired voice characteristics)
		const double ta = 0.05 * T; // Time constant for the exponential decay of the glottal flow (adjustable based on desired voice characteristics)

		double wg = PI / tp; // Angular frequency for the glottal flow during the open phase, calculated based on the time of glottal closure to ensure the flow reaches its maximum at the correct time

		double alpha = 12.0 / te; // Amplitude scaling factor for the glottal flow (adjustable based on desired voice characteristics)
		double tPeak = (PI - atan(wg / alpha)) / wg; // Calculate the time of the peak glottal flow based on the angular frequency and amplitude scaling factor to ensure the flow reaches its maximum at the correct time
		double e0 = 1.0 / (exp(alpha * tPeak) * sin(wg * tPeak));	// Calculate the normalization factor for the glottal flow to ensure it has a maximum value of 1.0

		double g = 0.0; // Variable to hold the glottal flow value

		if (tL < te)
		{
			g = e0 * exp(alpha * tL) * sin(wg * tL); // Calculate the glottal flow value based on the LF model for the current time within the period
		}
		else
		{
			double epsilon = 1.0 / ta;
			double Ee = -e0 * exp(alpha * te) * sin(wg * te); // Calculate the glottal flow value at the time of closure to use as the starting point for the exponential decay
			g = -Ee * (exp(-epsilon * (tL - te)) - exp(-epsilon * (T - te))) / (1.0 - exp(-epsilon * (T - te))); // Calculate the glottal flow value for the time after closure using an exponential decay
		}
		double noiseAmt = (tL < te) ? 0.008 * (1.0 - tL / te) : 0.0;
		double noise = ((rand() / (double)RAND_MAX) - 0.5) * noiseAmt;
		return g + noise;
	}

	void ApplyFormants(double& sample, const Phoneme& phoneme) const
	{
		sample = mF1.Process(sample); // Apply the first formant filter to the sample
		sample = mF2.Process(sample); // Apply the second formant filter to the sample
		sample = mF3.Process(sample); // Apply the third formant filter to the sample
		sample = mF4.Process(sample); // Apply the fourth formant filter to the sample
		sample = mF5.Process(sample); // Apply the fifth formant filter to the sample
	}

	double SpectralTilt(double sample) const
	{
		mTiltState = mTiltCoeff * mTiltState + (1.0 - mTiltCoeff) * sample;
		return mTiltState;
	}

	double Envelope(double t, double durationSeconds) const
	{
		double attack  = min(0.020, durationSeconds * 0.25); // attack scaled to phoneme length
		double release = min(0.030, durationSeconds * 0.20); // release scaled to phoneme length

		if (t < attack)
			return t / attack;
		if (t > durationSeconds - release)
			return max(0.0, (durationSeconds - t) / release);
		return 1.0;
	}

	// Short-attack envelope for consonants: 10 ms onset, 20 ms release, both
	// clamped to 15%/20% of duration so very short consonants never overdamp.
	// Vowels continue to use Envelope() with its 80 ms attack.
	double ConsonantEnvelope(double t, double dur) const
	{
		double attack  = min(0.010, dur * 0.15);
		double release = min(0.020, dur * 0.20);
		if (!mSuppressAttack  && t < attack)        return t / attack;
		if (!mSuppressRelease && t > dur - release) return max(0.0, (dur - t) / release);
		return 1.0;
	}
};

#endif // PHONEME_SYNTH_H