#ifndef VOICE_PITCH_H
#define VOICE_PITCH_H

#include <cstddef>
#include <vector>

class VoicePitch
{
public:
	VoicePitch()
	{
		pitchRatio = 1.003; // Default pitch shift ratio (slightly higher than 1.0 for a subtle effect)

		mixLevel = 0.15; // Default mix level for the pitch-shifted signal (0.0 = only original, 1.0 = only pitch-shifted)
	}

	void Apply(short* samples, size_t count)
	{
		if (count == 0) return; // Avoid processing if there are no samples

		std::vector<short> shifted(count); // Buffer to hold the pitch-shifted samples

		for (size_t i = 0; i < count; i++) // Iterate through each sample and apply pitch shifting
		{
			double srcIndex = i / pitchRatio;
			size_t idx = static_cast<size_t>(srcIndex);

			if (idx < count)
				shifted[i] = static_cast<short>(samples[idx] * mixLevel + samples[i] * (1.0 - mixLevel)); // Mix original and pitch-shifted samples
			else shifted[i] = samples[i]; // If the source index is out of bounds, use the original sample
		}

		for (size_t i = 0; i < count; i++) // Mix the pitch-shifted samples back into the original buffer with clamping to prevent overflow
		{
			int mixed = static_cast<int>(samples[i] + shifted[i] * mixLevel);

			if (mixed > 32767) mixed = 32767;
			if (mixed < -31768) mixed = -32768; // Clamp the mixed sample to the valid range for 16-bit audio

			samples[i] = static_cast<short>(mixed); // Write the mixed sample back to the original buffer
		}
	}

private:
	double pitchRatio; // Ratio to shift the pitch by (e.g., 1.0 = no change, 2.0 = one octave up, 0.5 = one octave down)
	double mixLevel; // Level to mix the pitch-shifted signal with the original (0.0 = only original, 1.0 = only pitch-shifted)
};

#endif // VOICE_PITCH_H