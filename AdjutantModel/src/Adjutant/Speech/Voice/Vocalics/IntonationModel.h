#ifndef INTONATION_MODEL_H
#define INTONATION_MODEL_H

#include "ProsodicWord.h"
#include "MoraicGrid.h"
#include "SonorityContour.h"
#include <vector>
#include <string>
#include <fstream>
#include <cmath>

// ---------------------------------------------------------------------------
// PhonemeRenderParams
// Per-phoneme synthesis parameters produced by IntonationModel::Compute().
// Consumed by PhonemeSynth::Generate(Phoneme, PhonemeRenderParams).
//
//   durationSeconds — total time window for this phoneme (from MoraicGrid,
//                     divided proportionally among phonemes in the syllable).
//   f0Start / f0End — fundamental frequency at phoneme onset and offset (Hz).
//                     PhonemeSynth linearly glides between them per sample.
//   amplitudeScale  — multiplier applied to the synthesis gain baseline.
// ---------------------------------------------------------------------------
struct PhonemeRenderParams
{
	double durationSeconds = 0.10;
	double f0Start         = 210.0;
	double f0End           = 210.0;
	double amplitudeScale  = 1.0;
	double f1Carry         = 0.0;  // carrier vowel F1 for diphthong formant glide (0.0 = normal)
	double f2Carry         = 0.0;  // carrier vowel F2
	double f3Carry         = 0.0;  // carrier vowel F3
};

// ---------------------------------------------------------------------------
// IntonationModel
// Combines ProsodicWord + MoraicGrid + per-syllable SonorityProfiles to
// produce a PhonemeRenderParams for every phoneme in the utterance.
//
// F0 shaping:
//   - Base F0 × stress scale gives the syllable-level pitch target.
//   - The sonority contour shapes the within-syllable F0 arc: onset and
//     coda consonants are pitched slightly lower than the nucleus peak.
//   - A linear declination is applied across the entire word: F0 falls
//     gradually from first syllable to last.
//
// Amplitude shaping:
//   - Base amplitude × stress scale; nucleus phoneme gets a small boost
//     (~10%) relative to onset/coda phonemes within the same syllable.
//
// Duration:
//   - Each syllable's total duration (from MoraicGrid) is divided among
//     its phonemes using fixed proportional weights:
//       onset consonant  — onsetConsonantShare  (default 0.12 per consonant)
//       nucleus vowel    — nucleus gets the remainder
//       coda consonant   — codaConsonantShare   (default 0.10 per consonant)
//
// Usage:
//   IntonationModel im;
//   im.LoadParams("path/to/Syllables.dat");
//   auto params = im.Compute(word, grid, contours);
//   // params[syllableIdx][phonemeIdx] → PhonemeRenderParams
// ---------------------------------------------------------------------------
class IntonationModel
{
public:
	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Read the #intonation_params section of Syllables.dat.
	// Silently falls back to built-in defaults if section or file is absent.
	bool LoadParams(const std::string& path)
	{
		std::ifstream f(path);
		if (!f.is_open()) return false;

		std::string line, section;
		while (std::getline(f, line))
		{
			line.erase(0, line.find_first_not_of(" \t\r\n"));

			if (!line.empty() && line[0] == '#')
			{
				section = line.substr(1);
				section.erase(0, section.find_first_not_of(" \t\r\n"));
				section.erase(section.find_last_not_of(" \t\r\n") + 1);
				continue;
			}

			if (section != "intonation_params") continue;

			auto pos = line.find('#');
			if (pos != std::string::npos) line = line.substr(0, pos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tok = Tokenise(line);
			if (tok.size() < 2) continue;

			if      (tok[0] == "base_f0")                mParams.baseF0              = std::stod(tok[1]);
			else if (tok[0] == "primary_f0_scale")       mParams.primaryF0Scale      = std::stod(tok[1]);
			else if (tok[0] == "secondary_f0_scale")     mParams.secondaryF0Scale    = std::stod(tok[1]);
			else if (tok[0] == "unstressed_f0_scale")    mParams.unstressedF0Scale   = std::stod(tok[1]);
			else if (tok[0] == "primary_amp_scale")      mParams.primaryAmpScale     = std::stod(tok[1]);
			else if (tok[0] == "secondary_amp_scale")    mParams.secondaryAmpScale   = std::stod(tok[1]);
			else if (tok[0] == "unstressed_amp_scale")   mParams.unstressedAmpScale  = std::stod(tok[1]);
			else if (tok[0] == "declination_rate")       mParams.declinationRate     = std::stod(tok[1]);
			else if (tok[0] == "onset_consonant_share")  mParams.onsetConsonantShare = std::stod(tok[1]);
			else if (tok[0] == "coda_consonant_share")   mParams.codaConsonantShare  = std::stod(tok[1]);
			else if (tok[0] == "glide_limit_ms")         mParams.glideLimitMs        = std::stod(tok[1]);
			else if (tok[0] == "glide_max_coda_sec")     mParams.glideMaxCodaSec     = std::stod(tok[1]);
			else if (tok[0] == "glide_vowel_fraction")   mParams.glideVowelFraction  = std::stod(tok[1]);
			else if (tok[0] == "glide_amp_blend")        mParams.glideAmpBlend       = std::stod(tok[1]);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Computation
	// -----------------------------------------------------------------------

	// Returns a 2-D array indexed as [syllableIdx][phonemeIdx].
	// syllableIdx matches the ordering of MoraicGrid::Syllables() and
	// ProsodicWord::FlatSyllables().
	// phonemeIdx iterates: onset[0..k], nucleus, coda[0..m] in linear order.
	std::vector<std::vector<PhonemeRenderParams>> Compute(
		const ProsodicWord&              word,
		const MoraicGrid&                grid,
		const std::vector<SonorityProfile>& contours,
		const std::vector<double>&       phraseF0Bases = {}) const
	{
		auto flat = word.FlatSyllables();
		const auto& timings = grid.Syllables();

		int nSyl = (int)flat.size();
		double totalDur = grid.TotalDuration();

		std::vector<std::vector<PhonemeRenderParams>> result;
		result.reserve(nSyl);

		for (int si = 0; si < nSyl; ++si)
		{
			const Syllable&     syl     = flat[si].first;
			StressLevel         stress  = flat[si].second;
			const SyllableTiming& timing = timings[si];

			// Syllable-level F0 target: phrase contour base when provided,
			// otherwise the model's baseF0.  Word-level declination is suppressed
			// when phrase bases are active (the contour already encodes it).
			double f0Base   = (!phraseF0Bases.empty() && si < (int)phraseF0Bases.size())
							   ? phraseF0Bases[si] : mParams.baseF0;
			double f0Target = f0Base * F0Scale(stress);
			double ampTarget = AmpScale(stress);

			if (phraseF0Bases.empty())
			{
				// Sentence-level declination: F0 drifts down linearly over the word
				double tMid = (timing.startSeconds + timing.durationSeconds * 0.5) / (totalDur > 1e-6 ? totalDur : 1e-6);
				double declination = 1.0 - mParams.declinationRate * tMid;
				f0Target *= (declination > 0.5 ? declination : 0.5);
			}

			// Duration allocation across phonemes in this syllable.
			// A diphthong glide is an onset-less, unstressed, non-first syllable
			// whose immediately preceding syllable is NOT also a glide.  The
			// prevIsGlide guard breaks the chain for triphthong sequences
			// (e.g. /aɪ.ər/ in "fire": IH is compressed, ER is not).
			// Bare-vowel glides (no coda) use a proportional allocator;
			// consonant-terminal glides use the fixed-cap + coda-budget allocator.
			bool prevIsGlide = (si > 0
				&& flat[si - 1].first.onset.empty()
				&& flat[si - 1].second == StressLevel::UNSTRESSED);
			bool isGlide = (si > 0 && syl.onset.empty()
				&& stress == StressLevel::UNSTRESSED && !prevIsGlide);
			std::vector<double> durations;
			if (isGlide)
				durations = syl.coda.empty()
					? AllocateVowelGlideDurations(timing.durationSeconds)
					: AllocateGlideDurations(syl, timing.durationSeconds);
			else
				durations = AllocateDurations(syl, timing.durationSeconds);

			// Diphthong bleed: capture the carrier's terminal F0, amplitude, and
			// nucleus formants while result.back() is still syllable si-1, before
			// the new slot is pushed.
			double f0Carry  = 0.0;
			double ampCarry = 0.0;
			double f1Carry  = 0.0, f2Carry = 0.0, f3Carry = 0.0;
			if (isGlide && !result.empty() && !result.back().empty())
			{
				f0Carry  = result.back().back().f0End;
				ampCarry = result.back().back().amplitudeScale;
				const Phoneme& cn = flat[si - 1].first.nucleus;
				f1Carry  = cn.GetFormant1();
				f2Carry  = cn.GetFormant2();
				f3Carry  = cn.GetFormant3();
			}

			// Build a PhonemeRenderParams per phoneme using the sonority profile
			const SonorityProfile& profile = (si < (int)contours.size())
			                               ? contours[si]
			                               : SonorityProfile{};

			int phonemeCount = syl.Size();
			result.push_back(std::vector<PhonemeRenderParams>());
			result.back().reserve(phonemeCount);

			int phonemeIdx = 0;
			for (int pi = 0; pi < phonemeCount; ++pi)
			{
				PhonemeRenderParams p;
				p.durationSeconds = durations[pi];

				// Normalized time position of this phoneme's midpoint within syllable
				double tStart = 0.0;
				for (int k = 0; k < pi; ++k) tStart += durations[k] / timing.durationSeconds;
				double tEnd = tStart + durations[pi] / timing.durationSeconds;
				double tMidP = (tStart + tEnd) * 0.5;

				// Sonority at start and end of phoneme shapes F0 within the syllable
				double sonStart = profile.IsEmpty() ? 1.0 : profile.SampleAt(tStart);
				double sonEnd   = profile.IsEmpty() ? 1.0 : profile.SampleAt(tEnd);

				// F0 follows the sonority arc: peaks at nucleus (sonority=1.0)
				// and dips at onset/coda consonants proportionally
				double f0RangeHz = f0Target * 0.15; // ±15% variation within syllable
				p.f0Start = f0Target + f0RangeHz * (sonStart - 0.5) * 2.0;
				p.f0End   = f0Target + f0RangeHz * (sonEnd   - 0.5) * 2.0;

				// Amplitude: nucleus phoneme boosted slightly relative to consonants
				double sonMid = profile.IsEmpty() ? 1.0 : profile.SampleAt(tMidP);
				p.amplitudeScale = ampTarget * (0.85 + 0.30 * sonMid);

				// Glide nucleus: inherit carrier terminal F0 (continuous pitch glide),
				// blend amplitude, and carry the carrier's formant frequencies as the
				// synthesis start point so formants sweep carrier → glide target across
				// the phoneme window rather than jumping at the syllable boundary.
				if (isGlide && pi == 0 && f0Carry > 0.0)
				{
					p.f0Start        = f0Carry;
					p.amplitudeScale = ampCarry * mParams.glideAmpBlend
									 + p.amplitudeScale * (1.0 - mParams.glideAmpBlend);
					p.f1Carry        = f1Carry;
					p.f2Carry        = f2Carry;
					p.f3Carry        = f3Carry;
				}

						result.back().push_back(p);
							++phonemeIdx;
						}
					}

					// Stitch F0 at syllable boundaries: average each adjacent pair so
					// GlottalSource sees a continuous pitch trajectory rather than a jump
					// produced by independent per-syllable F0 targets.
					// Diphthong glides are skipped — their f0Start is already set to
					// f0Carry (the carrier's terminal pitch).
					for (int si = 0; si < nSyl - 1; ++si)
					{
						if (result[si].empty() || result[si + 1].empty()) continue;
						if (result[si + 1].front().f1Carry > 0.0) continue;
						double mid = (result[si].back().f0End + result[si + 1].front().f0Start) * 0.5;
						result[si].back().f0End        = mid;
						result[si + 1].front().f0Start = mid;
					}

					return result;
				}

	// -----------------------------------------------------------------------
	// Parameters (set directly, or populated by LoadParams() / ApplyParams())
	// -----------------------------------------------------------------------
	struct Params
	{
		double baseF0              = 210.0; // Hz — neutral pitch
		double primaryF0Scale      = 1.25;
		double secondaryF0Scale    = 1.10;
		double unstressedF0Scale   = 0.90;
		double primaryAmpScale     = 1.30;
		double secondaryAmpScale   = 1.10;
		double unstressedAmpScale  = 0.80;
		double declinationRate     = 0.03;  // fractional F0 drop per unit normalised time
		double onsetConsonantShare = 0.25;  // proportion of syllable duration per onset C
		double codaConsonantShare  = 0.20;  // proportion of syllable duration per coda C
		double glideLimitMs        = 25.0;  // nucleus duration cap for diphthong-glide syllables
		double glideMaxCodaSec     = 0.075; // per-coda-consonant cap inside a glide syllable
		double glideVowelFraction  = 0.20;  // fraction of MoraicGrid window for a bare-vowel glide (no coda)
		double glideAmpBlend       = 0.65;  // carrier-amplitude carry fraction into the glide nucleus (diphthong bleed)
	};

	void ApplyParams(const Params& p) { mParams = p; }
	double GetBaseF0() const { return mParams.baseF0; }

private:
	Params mParams;

	// -----------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------

	double F0Scale(StressLevel s) const
	{
		switch (s)
		{
		case StressLevel::PRIMARY:   return mParams.primaryF0Scale;
		case StressLevel::SECONDARY: return mParams.secondaryF0Scale;
		default:                     return mParams.unstressedF0Scale;
		}
	}

	double AmpScale(StressLevel s) const
	{
		switch (s)
		{
		case StressLevel::PRIMARY:   return mParams.primaryAmpScale;
		case StressLevel::SECONDARY: return mParams.secondaryAmpScale;
		default:                     return mParams.unstressedAmpScale;
		}
	}

	// Vowel-internal diphthong-glide allocator (bare-vowel case, no coda).
	// Used for pure V.V transitions such as /ʊ/ in "now" or /ɪ/ in "sky".
	// Returns a proportional fraction of the MoraicGrid window rather than
	// a hard cap so the glide scales with syllable weight and the entire
	// window is consumed — no unused budget, no F0-declination drift.
	std::vector<double> AllocateVowelGlideDurations(double totalSec) const
	{
		return { totalSec * mParams.glideVowelFraction };
	}

	// Diphthong-glide syllable allocator (consonant-terminal case, has coda).
	// Gives the nucleus a fixed short cap (glideLimitMs) and distributes the
	// remaining budget across coda consonants, each further capped at
	// glideMaxCodaSec, so a glide like /aɪ/ stays brief regardless of the
	// full MoraicGrid window assigned to the syllable.
	std::vector<double> AllocateGlideDurations(const Syllable& syl, double totalSec) const
	{
		int nCoda = (int)syl.coda.size();
		double nucSec = mParams.glideLimitMs / 1000.0;
		double codaBudget = totalSec - nucSec;
		if (codaBudget < 0.0) codaBudget = 0.0;
		double perCodaSec = 0.0;
		if (nCoda > 0)
		{
			perCodaSec = codaBudget / nCoda;
			if (perCodaSec > mParams.glideMaxCodaSec) perCodaSec = mParams.glideMaxCodaSec;
		}
		std::vector<double> d;
		d.reserve(1 + nCoda);
		d.push_back(nucSec);
		for (int i = 0; i < nCoda; ++i) d.push_back(perCodaSec);
		return d;
	}

	// Divide the syllable's total duration among its phonemes.
	// Onset and coda consonants each receive a fixed share; the remainder
	// goes to the nucleus.
	std::vector<double> AllocateDurations(const Syllable& syl, double totalSec) const
	{
		int nOnset = (int)syl.onset.size();
		int nCoda  = (int)syl.coda.size();

		double onsetShare = mParams.onsetConsonantShare * nOnset;
		double codaShare  = mParams.codaConsonantShare  * nCoda;
		double remaining  = 1.0 - onsetShare - codaShare;
		double nucShare   = (remaining > 0.10 ? remaining : 0.10);

		// Renormalize in case shares exceed 1.0
		double total = onsetShare + nucShare + codaShare;
		onsetShare /= total;
		nucShare   /= total;
		codaShare  /= total;

		std::vector<double> d;
		d.reserve(nOnset + 1 + nCoda);

		double perOnset = (nOnset > 0) ? (onsetShare / nOnset) : 0.0;
		double perCoda  = (nCoda  > 0) ? (codaShare  / nCoda)  : 0.0;

		for (int i = 0; i < nOnset; ++i)  d.push_back(totalSec * perOnset);
		d.push_back(totalSec * nucShare);
		for (int i = 0; i < nCoda; ++i)   d.push_back(totalSec * perCoda);

		return d;
	}

	// -----------------------------------------------------------------------
	// .dat parsing helper
	// -----------------------------------------------------------------------
	static std::vector<std::string> Tokenise(const std::string& line)
	{
		std::vector<std::string> tokens;
		std::string remaining = line;
		const std::string delim = " - ";
		size_t pos = 0;
		while ((pos = remaining.find(delim)) != std::string::npos)
		{
			std::string tok = remaining.substr(0, pos);
			tok.erase(0, tok.find_first_not_of(" \t"));
			tok.erase(tok.find_last_not_of(" \t") + 1);
			if (!tok.empty()) tokens.push_back(tok);
			remaining = remaining.substr(pos + delim.size());
		}
		remaining.erase(0, remaining.find_first_not_of(" \t"));
		remaining.erase(remaining.find_last_not_of(" \t") + 1);
		if (!remaining.empty()) tokens.push_back(remaining);
		return tokens;
	}
};

#endif // INTONATION_MODEL_H
