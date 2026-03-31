#ifndef MORAIC_GRID_H
#define MORAIC_GRID_H

#include "ProsodicWord.h"
#include <vector>
#include <string>
#include <fstream>

// ---------------------------------------------------------------------------
// SyllableTiming
// Per-syllable output of MoraicGrid::Compute().
// Consumed by IntonationModel to produce per-phoneme render parameters.
// ---------------------------------------------------------------------------
struct SyllableTiming
{
	double     startSeconds;    // absolute onset from word start
	double     durationSeconds; // total syllable window (onset pad + mora body)
	int        moraCount;       // 1 (LIGHT), 2 (HEAVY), 3 (SUPERHEAVY)
	StressLevel stress;         // stress level resolved by ProsodicWord
};

// ---------------------------------------------------------------------------
// MoraicGrid
// Maps a ProsodicWord onto a timeline using moraic weight theory:
//
//   LIGHT     = 1 mora  (V, CV)
//   HEAVY     = 2 morae (VC, CVC, CVV)
//   SUPERHEAVY = 3 morae (CVCC, CVVV, etc.)
//
// Base mora duration and per-consonant onset padding are loaded from the
// #mora_weights section of Syllables.dat (falls back to 80 ms / 15 ms).
//
// Stress multipliers scale the mora body only; onset padding is constant:
//   PRIMARY    × 1.40
//   SECONDARY  × 1.20
//   UNSTRESSED × 0.85
//
// Usage:
//   MoraicGrid grid;
//   grid.LoadParams("path/to/Syllables.dat");
//   grid.Compute(word);
//   const auto& timings = grid.Syllables();
// ---------------------------------------------------------------------------
class MoraicGrid
{
public:
	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Read the #mora_weights section of Syllables.dat.
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

			if (section != "mora_weights") continue;

			auto pos = line.find('#');
			if (pos != std::string::npos) line = line.substr(0, pos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tok = Tokenise(line);
			if (tok.size() < 2) continue;

			if      (tok[0] == "base_mora_ms")        mParams.baseMoraMs        = std::stod(tok[1]);
			else if (tok[0] == "onset_pad_ms")        mParams.onsetPadMs        = std::stod(tok[1]);
			else if (tok[0] == "primary_scale")       mParams.primaryScale      = std::stod(tok[1]);
			else if (tok[0] == "secondary_scale")     mParams.secondaryScale    = std::stod(tok[1]);
			else if (tok[0] == "unstressed_scale")    mParams.unstressedScale   = std::stod(tok[1]);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Computation
	// -----------------------------------------------------------------------

	// Populate the internal timing table from the resolved ProsodicWord.
	// Call once per word; results are then read via Syllables() / TotalDuration().
	void Compute(const ProsodicWord& word)
	{
		mSyllables.clear();

		auto flat = word.FlatSyllables();
		double cursor = 0.0;

		for (const auto& [syl, stress] : flat)
		{
			SyllableTiming t;
			t.stress = stress;

			// Mora count from syllable weight
			switch (syl.GetWeight())
			{
			case SyllableWeight::SUPERHEAVY: t.moraCount = 3; break;
			case SyllableWeight::HEAVY:      t.moraCount = 2; break;
			default:                         t.moraCount = 1; break;
			}

			// Stress multiplier scales the mora body
			double scale = StressScale(stress);
			double moraBodySec = (t.moraCount * mParams.baseMoraMs * scale) / 1000.0;

			// Onset consonant padding (extrametrical; stress-independent)
			double onsetPadSec = ((int)syl.onset.size() * mParams.onsetPadMs) / 1000.0;

			t.startSeconds    = cursor;
			t.durationSeconds = onsetPadSec + moraBodySec;

			mSyllables.push_back(t);
			cursor += t.durationSeconds;
		}
	}

	// -----------------------------------------------------------------------
	// Accessors
	// -----------------------------------------------------------------------

	const std::vector<SyllableTiming>& Syllables() const { return mSyllables; }

	double TotalDuration() const
	{
		if (mSyllables.empty()) return 0.0;
		const SyllableTiming& last = mSyllables.back();
		return last.startSeconds + last.durationSeconds;
	}

	// -----------------------------------------------------------------------
	// Parameters (set directly, or populated by LoadParams() / ApplyParams())
	// -----------------------------------------------------------------------
	struct Params
	{
		double baseMoraMs      = 160.0; // duration of one mora at neutral stress
		double onsetPadMs      = 15.0;  // added per onset consonant (extrametrical)
		double primaryScale    = 1.40;
		double secondaryScale  = 1.20;
		double unstressedScale = 0.85;
	};

	void ApplyParams(const Params& p) { mParams = p; }

private:
	Params mParams;

	std::vector<SyllableTiming> mSyllables;

	double StressScale(StressLevel s) const
	{
		switch (s)
		{
		case StressLevel::PRIMARY:   return mParams.primaryScale;
		case StressLevel::SECONDARY: return mParams.secondaryScale;
		default:                     return mParams.unstressedScale;
		}
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

#endif // MORAIC_GRID_H
