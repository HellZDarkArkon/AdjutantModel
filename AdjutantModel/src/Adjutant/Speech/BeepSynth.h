#ifndef BEEP_SYNTH_H
#define BEEP_SYNTH_H

#include <vector>
#include <cmath>
#include <cstdint>
#include <initializer_list>

// ---------------------------------------------------------------------------
// BeepType
// Named presets for synthetic UI tones played at points in a message.
//
//   MSG_START  — rising two-tone cue before a message begins
//   MSG_END    — falling sweep after a message closes
//   ATTENTION  — single sharp tone for emphasis
//   SEPARATOR  — short click-tone between message segments
//   CONFIRM    — rising double-tone for positive acknowledgement
//   ALERT      — pulsed urgent tone for high-priority warnings
// ---------------------------------------------------------------------------
enum class BeepType
{
	MSG_START,
	MSG_END,
	ATTENTION,
	SEPARATOR,
	CONFIRM,
	ALERT,
};

// ---------------------------------------------------------------------------
// BeepSynth
// Header-only PCM tone generator.  All methods are static; no instance state.
// Output is 16-bit signed mono PCM at the supplied sample rate (default 44100).
// ---------------------------------------------------------------------------
class BeepSynth
{
public:
	static std::vector<short> Generate(BeepType type, int sampleRate = 44100)
	{
		switch (type)
		{
		case BeepType::MSG_START:
			return Concat({
				Tone(880.0,  100, 0.45, 8,  12, sampleRate),
				Silence(18, sampleRate),
				Tone(1100.0, 80,  0.55, 6,  16, sampleRate)
			});
		case BeepType::MSG_END:
			return Sweep(1100.0, 880.0, 130, 0.45, 8, 20, sampleRate);
		case BeepType::ATTENTION:
			return Tone(1200.0, 65,  0.60, 5, 10, sampleRate);
		case BeepType::SEPARATOR:
			return Tone(900.0,  30,  0.35, 3,  8, sampleRate);
		case BeepType::CONFIRM:
			return Concat({
				Tone(1000.0, 55, 0.45, 5, 10, sampleRate),
				Silence(15, sampleRate),
				Tone(1200.0, 55, 0.55, 5, 14, sampleRate)
			});
		case BeepType::ALERT:
			return PulsedTone(1400.0, 220, 8.0, 0.65, sampleRate);
		default:
			return {};
		}
	}

	// Zero-filled silence — also useful for inter-word gaps in synthesis.
	static std::vector<short> Silence(int durMs, int sr)
	{
		size_t N = static_cast<size_t>(sr * durMs / 1000.0);
		return std::vector<short>(N, 0);
	}

private:
	static constexpr double kPi  = 3.14159265358979323846;
	static constexpr double kTau = 2.0 * kPi;

	// Sine tone with linear attack/release amplitude envelope.
	static std::vector<short> Tone(double freqHz, int durMs,
	                               double amplitude,
	                               int attackMs, int releaseMs,
	                               int sr)
	{
		size_t N        = static_cast<size_t>(sr * durMs   / 1000.0);
		size_t attackN  = static_cast<size_t>(sr * attackMs  / 1000.0);
		size_t releaseN = static_cast<size_t>(sr * releaseMs / 1000.0);
		if (attackN  > N) attackN  = N;
		if (releaseN > N) releaseN = N;

		std::vector<short> out(N);
		double phase    = 0.0;
		double phaseInc = kTau * freqHz / sr;

		for (size_t i = 0; i < N; ++i)
		{
			double env = 1.0;
			if (i < attackN)
				env = static_cast<double>(i) / static_cast<double>(attackN);
			else if (releaseN > 0 && i >= N - releaseN)
				env = static_cast<double>(N - i) / static_cast<double>(releaseN);

			out[i] = static_cast<short>(amplitude * std::sin(phase) * env * 32767.0);
			phase += phaseInc;
		}
		return out;
	}

	// Linear frequency sweep (glide) from freqStart to freqEnd.
	static std::vector<short> Sweep(double freqStart, double freqEnd, int durMs,
	                                double amplitude,
	                                int attackMs, int releaseMs,
	                                int sr)
	{
		size_t N        = static_cast<size_t>(sr * durMs   / 1000.0);
		size_t attackN  = static_cast<size_t>(sr * attackMs  / 1000.0);
		size_t releaseN = static_cast<size_t>(sr * releaseMs / 1000.0);
		if (attackN  > N) attackN  = N;
		if (releaseN > N) releaseN = N;

		std::vector<short> out(N);
		double phase = 0.0;

		for (size_t i = 0; i < N; ++i)
		{
			double t    = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 0.0;
			double freq = freqStart + (freqEnd - freqStart) * t;

			double env = 1.0;
			if (i < attackN)
				env = static_cast<double>(i) / static_cast<double>(attackN);
			else if (releaseN > 0 && i >= N - releaseN)
				env = static_cast<double>(N - i) / static_cast<double>(releaseN);

			out[i] = static_cast<short>(amplitude * std::sin(phase) * env * 32767.0);
			phase += kTau * freq / sr;
		}
		return out;
	}

	// AM-modulated sine: carrier at freqHz pulsed at pulseRateHz.
	static std::vector<short> PulsedTone(double freqHz, int durMs,
	                                     double pulseRateHz,
	                                     double amplitude, int sr)
	{
		size_t N = static_cast<size_t>(sr * durMs / 1000.0);
		std::vector<short> out(N);
		double phase    = 0.0;
		double phaseInc = kTau * freqHz / sr;

		for (size_t i = 0; i < N; ++i)
		{
			double t  = static_cast<double>(i) / sr;
			double am = 0.5 + 0.5 * std::sin(kTau * pulseRateHz * t);
			out[i] = static_cast<short>(amplitude * std::sin(phase) * am * 32767.0);
			phase += phaseInc;
		}
		return out;
	}

	// Concatenate multiple PCM segments into one buffer.
	static std::vector<short> Concat(std::initializer_list<std::vector<short>> segs)
	{
		std::vector<short> out;
		size_t total = 0;
		for (const auto& s : segs) total += s.size();
		out.reserve(total);
		for (const auto& s : segs)
			out.insert(out.end(), s.begin(), s.end());
		return out;
	}
};

#endif // BEEP_SYNTH_H
