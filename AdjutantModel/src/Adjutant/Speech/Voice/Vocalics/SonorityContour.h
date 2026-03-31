#ifndef SONORITY_CONTOUR_H
#define SONORITY_CONTOUR_H

#include "Syllable.h"
#include "SyllableBuilder.h"
#include <vector>
#include <cassert>

// ---------------------------------------------------------------------------
// SonorityProfile
// Normalized sonority curve over the time span of one syllable.
//
// `values` contains N samples in [0.0, 1.0] where:
//   0.0 = minimum sonority (obstruent onset / coda)
//   1.0 = nucleus peak
//
// `nucleusSample` is the index of the nucleus peak within values[].
// Adjacent onset/coda samples are linearly interpolated between the
// sonority ranks of their phonemes and the nucleus peak.
// ---------------------------------------------------------------------------
struct SonorityProfile
{
	std::vector<double> values;   // normalized sonority at each time sample
	int                 nucleusSample = 0; // index of nucleus peak in values[]

	bool IsEmpty()   const { return values.empty(); }
	int  NumSamples() const { return (int)values.size(); }

	// Interpolate the profile at normalized time t in [0, 1].
	double SampleAt(double t) const
	{
		assert(!values.empty());
		double idx = t * (double)(values.size() - 1);
		int    lo  = (int)idx;
		int    hi  = lo + 1;
		if (hi >= (int)values.size()) return values.back();
		double frac = idx - (double)lo;
		return values[lo] * (1.0 - frac) + values[hi] * frac;
	}
};

// ---------------------------------------------------------------------------
// SonorityContour
// Computes a SonorityProfile for a single Syllable using the same sonority
// ranks as SyllableBuilder::SonorityRank().
//
// The profile is built as a piecewise linear curve:
//   - One sample per phoneme in [onset | nucleus | coda] order.
//   - Sonority rank is normalized to [0, 1] using the known range [0, 6]:
//       rank 0  (plosive/sibaff) → 0.00
//       rank 6  (vowel nucleus)  → 1.00
//   - An onset with zero phonemes still produces a ramp from 0 → 1.
//   - A coda with zero phonemes produces a flat tail at 1.
//   - `resolution` controls how many output samples are generated
//     (default 64 — cheap and sufficient for smooth contour interpolation).
//
// Usage:
//   SonorityProfile p = SonorityContour::Compute(syllable);
//   double sonority   = p.SampleAt(0.5); // midpoint
// ---------------------------------------------------------------------------
class SonorityContour
{
public:
	// Number of samples in the output profile.  Must be >= 3.
	static constexpr int kResolution = 64;

	// -----------------------------------------------------------------------
	// Main entry point
	// -----------------------------------------------------------------------

	static SonorityProfile Compute(const Syllable& syl)
	{
		// Build the raw rank sequence: onset phonemes, nucleus, coda phonemes
		std::vector<double> ranks;
		ranks.reserve(syl.onset.size() + 1 + syl.coda.size());

		for (const Phoneme& p : syl.onset)
			ranks.push_back(NormalizedRank(p));

		int nucleusIdx = (int)ranks.size();
		ranks.push_back(1.0); // nucleus always at peak

		for (const Phoneme& p : syl.coda)
			ranks.push_back(NormalizedRank(p));

		// Edge case: single-phoneme syllable (bare nucleus)
		if (ranks.size() == 1)
		{
			SonorityProfile out;
			out.values.assign(kResolution, 1.0);
			out.nucleusSample = kResolution / 2;
			return out;
		}

		// Resample the piecewise linear rank curve onto kResolution samples.
		// nucleusIdx maps to the output sample at the proportional position.
		SonorityProfile out;
		out.values.resize(kResolution);

		double srcLen    = (double)(ranks.size() - 1);
		double nucSrcPos = (double)nucleusIdx / srcLen;
		out.nucleusSample = (int)(nucSrcPos * (kResolution - 1) + 0.5);

		for (int i = 0; i < kResolution; ++i)
		{
			double t   = (double)i / (double)(kResolution - 1);
			double src = t * srcLen;
			int    lo  = (int)src;
			int    hi  = lo + 1;
			if (hi >= (int)ranks.size())
			{
				out.values[i] = ranks.back();
			}
			else
			{
				double frac   = src - (double)lo;
				out.values[i] = ranks[lo] * (1.0 - frac) + ranks[hi] * frac;
			}
		}

		return out;
	}

private:
	// Map a phoneme's sonority rank to a normalized [0, 1] value.
	// Vowels (rank 6) → 1.0; maximum-effort obstruents (rank 0) → 0.0.
	static double NormalizedRank(const Phoneme& p)
	{
		int r = SyllableBuilder::SonorityRank(p); // returns 0–6
		if (r < 0) r = 0;
		return (double)r / 6.0;
	}
};

#endif // SONORITY_CONTOUR_H
