#ifndef SYLLABLE_BUILDER_H
#define SYLLABLE_BUILDER_H

#include "Syllable.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

// SyllableBuilder: segments a flat phoneme sequence into Syllable objects.
//
// Algorithm:
//   1. Identify all nucleus positions (non-consonant phonemes = vowels).
//   2. Apply the Maximal Onset Principle (MOP): inter-vocalic consonant
//      clusters are assigned as far left as possible to the FOLLOWING onset,
//      subject to the Sonority Sequencing Principle (SSP).
//   3. SSP requires onset sonority to strictly RISE toward the nucleus and
//      coda sonority to strictly FALL away from the nucleus.
//   4. When MOP+SSP would reject a cluster, the exception table loaded from
//      Syllables.dat is consulted (covers /s/+obstruent and other attested
//      phonotactic violations).
//   5. Any remaining unassignable consonants are placed in the coda of the
//      preceding syllable.
//
// Usage:
//   SyllableBuilder builder;
//   builder.LoadFromDat("path/to/Syllables.dat");
//   auto syllables = builder.Build(phonemes);
//
class SyllableBuilder
{
public:
	// ---------------------------------------------------------------------
	// Loading
	// ---------------------------------------------------------------------

	// Parse Syllables.dat and populate the internal exception cluster tables
	// and attested shape list. Call once at startup; safe to skip (falls back
	// to pure SSP+MOP which handles the majority of languages).
	bool LoadFromDat(const std::string& path)
	{
		std::ifstream f(path);
		if (!f.is_open()) return false;

		std::string line, section;
		while (std::getline(f, line))
		{
			// Trim leading whitespace before section-header check
			line.erase(0, line.find_first_not_of(" \t\r\n"));

			// Section header: line starts with '#' before any comment stripping
			if (!line.empty() && line[0] == '#')
			{
				section = line.substr(1);
				section.erase(0, section.find_first_not_of(" \t\r\n"));
				section.erase(section.find_last_not_of(" \t\r\n") + 1);
				continue;
			}

			// Strip inline comments and trailing whitespace
			auto pos = line.find('#');
			if (pos != std::string::npos) line = line.substr(0, pos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			// Tokenise on ' - '
			std::vector<std::string> tok = Tokenise(line);
			if (tok.empty()) continue;

			if (section == "shapes" && tok.size() >= 4)
			{
				SyllableShapeRecord r;
				r.shape    = tok[0];
				r.maxOnset = std::stoi(tok[1]);
				r.maxCoda  = std::stoi(tok[2]);
				r.weight   = tok[3];
				mShapes.push_back(r);
			}
			else if (section == "onset_clusters" && tok.size() >= 4)
			{
				ClusterPattern p = ParseCluster(tok, 1);
				if (!p.manners.empty()) mOnsetExceptions.push_back(p);
			}
			else if (section == "coda_clusters" && tok.size() >= 5)
			{
				ClusterPattern p = ParseCluster(tok, 1);
				if (!p.manners.empty()) mCodaExceptions.push_back(p);
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------
	// Segmentation
	// ---------------------------------------------------------------------

	// Main entry point: returns a vector of Syllable objects assembled from
	// the input phoneme sequence using MOP + SSP + exception clusters.
	std::vector<Syllable> Build(const std::vector<Phoneme>& phonemes) const
	{
		if (phonemes.empty()) return {};

		// Step 1: locate nucleus positions; an IH or UH vowel immediately following
		// another vowel is a diphthong off-glide — it shares the preceding nucleus.
		std::vector<int> nuclei;
		std::vector<bool> isOffGlide(phonemes.size(), false);
		for (int i = 0; i < (int)phonemes.size(); ++i)
		{
			if (!phonemes[i].IsConsonant())
			{
				if (!nuclei.empty() && i == nuclei.back() + 1 && IsDiphthongOffGlide(phonemes[i]))
					isOffGlide[i] = true;
				else
					nuclei.push_back(i);
			}
		}

		// No vowels at all — treat entire sequence as a single degenerate syllable
		if (nuclei.empty())
		{
			Syllable s(phonemes[0]);
			for (int i = 1; i < (int)phonemes.size(); ++i)
				s.coda.push_back(phonemes[i]);
			return { s };
		}

		std::vector<Syllable> result;

		// Step 2: for each nucleus, determine onset and collect any preceding
		// consonant cluster that hasn't been claimed by the previous coda.
		// `cursor` tracks how far we've consumed the phoneme stream.
		int cursor = 0;

		for (int ni = 0; ni < (int)nuclei.size(); ++ni)
		{
			int nucPos = nuclei[ni];

			// Range available for this nucleus's onset: [cursor, nucPos), excluding
			// diphthong off-glide vowels (they will be pushed to the preceding coda).
			std::vector<int> available;
			for (int k = cursor; k < nucPos; ++k)
				if (!isOffGlide[k])
					available.push_back(k);

			// MOP: try to maximise the onset, falling back if SSP (or exceptions)
			// require a shorter onset.  When available is empty (adjacent vowels /
			// diphthong) nucPos == cursor, so onsetStart must equal nucPos; calling
			// FindOnsetStart in that case returns phonemes.size() and causes every
			// subsequent phoneme to be duplicated into the previous coda.
			int onsetStart = available.empty() ? nucPos : FindOnsetStart(phonemes, available);

			// Everything before onsetStart goes to the CODA of the previous syllable.
			// For the first syllable there is no previous coda, so force all
			// leading consonants into the onset regardless of SSP.
			if (!result.empty())
			{
				for (int k = cursor; k < onsetStart; ++k)
					result.back().coda.push_back(phonemes[k]);
			}
			else
			{
				onsetStart = cursor;
			}

			// Build this syllable
			Syllable syl(phonemes[nucPos]);
			for (int k = onsetStart; k < nucPos; ++k)
				syl.onset.push_back(phonemes[k]);

			result.push_back(syl);
			cursor = nucPos + 1; // advance past nucleus
		}

		// Step 3: any trailing consonants go to the final syllable's coda
		for (int k = cursor; k < (int)phonemes.size(); ++k)
			result.back().coda.push_back(phonemes[k]);

		return result;
	}

	// Returns true if the given Syllable conforms to an attested shape in the
	// loaded .dat, or to the universal minimal shapes if no .dat was loaded.
	bool IsAttested(const Syllable& s) const
	{
		std::string shape = s.GetShape();
		if (mShapes.empty())
			return shape == "V"   || shape == "CV"  || shape == "VC"  ||
			       shape == "CVC" || shape == "CCV" || shape == "CVCC";
		for (const SyllableShapeRecord& r : mShapes)
			if (r.shape == shape) return true;
		return false;
	}

	// Sonority rank used by the SSP check.
	// Higher = more sonorous. Vowels are implicitly 6 (not returned here).
	static int SonorityRank(ConsonantManner m)
	{
		switch (m)
		{
		case ConsonantManner::PLOSIVE:     return 0;
		case ConsonantManner::SIBAFF:      return 0;
		case ConsonantManner::LATAFF:      return 1;
		case ConsonantManner::AFFRICATE:   return 1;
		case ConsonantManner::SIBFRIC:     return 2;
		case ConsonantManner::FRICATIVE:   return 2;
		case ConsonantManner::LATFRIC:     return 2;
		case ConsonantManner::NASAL:       return 3;
		case ConsonantManner::FLAP:        return 4;
		case ConsonantManner::TRILL:       return 4;
		case ConsonantManner::LATFLAP:     return 4;
		case ConsonantManner::LATAPP:      return 4;
		case ConsonantManner::APPROXIMANT: return 5;
		default:                           return -1;
		}
	}

	static int SonorityRank(const Phoneme& ph)
	{
		if (!ph.IsConsonant()) return 6; // vowel / nucleus
		return SonorityRank(ph.GetManner());
	}

	// Returns true for the two vowels that serve as English diphthong off-glides:
	// IH /ɪ/ (sky, day, boy) and UH /ʊ/ (go, now).  An IH or UH immediately
	// following another vowel nucleus is merged into that nucleus rather than
	// starting a new syllable.
	static bool IsDiphthongOffGlide(const Phoneme& ph)
	{
		return ph.GetType() == PhonemeType::NEAR_CLOSE_NEAR_FRONT_UNROUNDED  // IH /ɪ/
			|| ph.GetType() == PhonemeType::NEAR_CLOSE_NEAR_BACK_ROUNDED;    // UH /ʊ/
	}

private:
	// ---------------------------------------------------------------------
	// Internal types
	// ---------------------------------------------------------------------

	struct ClusterPattern
	{
		std::vector<ConsonantManner> manners; // manner sequence
		std::string context;                  // language/context tag
	};

	struct SyllableShapeRecord
	{
		std::string shape;
		int         maxOnset = 0;
		int         maxCoda  = 0;
		std::string weight;
	};

	std::vector<ClusterPattern>    mOnsetExceptions;
	std::vector<ClusterPattern>    mCodaExceptions;
	std::vector<SyllableShapeRecord> mShapes;

	// ---------------------------------------------------------------------
	// MOP + SSP logic
	// ---------------------------------------------------------------------

	// Returns the index into `phonemes` at which the onset of the current
	// syllable should begin, given the indices of available consonants.
	// Implements MOP: start with all available consonants as onset, shrink
	// from the left if SSP is violated, consulting exception table as needed.
	int FindOnsetStart(const std::vector<Phoneme>& phonemes,
	                   const std::vector<int>& available) const
	{
		if (available.empty()) return (int)phonemes.size();

		// Try maximal onset first (full available range), then reduce
		for (int take = (int)available.size(); take >= 1; --take)
		{
			// Onset = last `take` consonants in available[]
			int startIdx = available[(int)available.size() - take];
			std::vector<Phoneme> onset;
			for (int i = (int)available.size() - take; i < (int)available.size(); ++i)
				onset.push_back(phonemes[available[i]]);

			if (IsLegalOnset(onset)) return startIdx;
		}

		// Nothing legal — push everything to the preceding coda
		return available.back() + 1;
	}

	// Returns true if `onset` satisfies SSP (strictly rising toward nucleus)
	// or matches an attested exception cluster in mOnsetExceptions.
	bool IsLegalOnset(const std::vector<Phoneme>& onset) const
	{
		if (onset.empty()) return true;
		if (onset.size() == 1) return true; // any single consonant is a valid onset

		// Check SSP: each step must be strictly non-decreasing in sonority
		// (we allow equal adjacent ranks for cases like /sp/, /st/ after
		// exception matching below)
		bool sspOk = true;
		for (int i = 0; i + 1 < (int)onset.size(); ++i)
		{
			if (SonorityRank(onset[i]) >= SonorityRank(onset[i + 1]))
			{
				sspOk = false;
				break;
			}
		}
		if (sspOk) return true;

		// SSP fails — check exception table
		return MatchesCluster(onset, mOnsetExceptions);
	}

	// Returns true if `coda` satisfies SSP (strictly falling away from nucleus)
	// or matches an attested exception cluster in mCodaExceptions.
	bool IsLegalCoda(const std::vector<Phoneme>& coda) const
	{
		if (coda.empty()) return true;
		if (coda.size() == 1) return true;

		bool sspOk = true;
		for (int i = 0; i + 1 < (int)coda.size(); ++i)
		{
			if (SonorityRank(coda[i]) <= SonorityRank(coda[i + 1]))
			{
				sspOk = false;
				break;
			}
		}
		if (sspOk) return true;

		return MatchesCluster(coda, mCodaExceptions);
	}

	// Returns true if the manner sequence of `phonemes` matches any stored cluster pattern.
	bool MatchesCluster(const std::vector<Phoneme>& phonemes,
	                    const std::vector<ClusterPattern>& table) const
	{
		for (const ClusterPattern& pat : table)
		{
			if (pat.manners.size() != phonemes.size()) continue;
			bool match = true;
			for (int i = 0; i < (int)pat.manners.size(); ++i)
			{
				if (phonemes[i].GetManner() != pat.manners[i]) { match = false; break; }
			}
			if (match) return true;
		}
		return false;
	}

	// ---------------------------------------------------------------------
	// .dat parsing helpers
	// ---------------------------------------------------------------------

	// Split a line on " - " delimiters and strip whitespace from each token.
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

	// Parse a cluster entry: tokens[startCol..] are manner names, last token is context.
	// '_' marks an unused slot (2-consonant cluster in a 3-slot line).
	static ClusterPattern ParseCluster(const std::vector<std::string>& tok, int startCol)
	{
		ClusterPattern p;
		// Last token is always the context tag; everything before is a manner or '_'
		for (int i = startCol; i < (int)tok.size() - 1; ++i)
		{
			if (tok[i] == "_") continue;
			ConsonantManner m = MannerFromString(tok[i]);
			if (m != ConsonantManner::NOT_CONSONANT)
				p.manners.push_back(m);
		}
		if (!tok.empty()) p.context = tok.back();
		return p;
	}

	// Convert a manner name string (as written in Syllables.dat) to ConsonantManner.
	static ConsonantManner MannerFromString(const std::string& s)
	{
		if (s == "NASAL")       return ConsonantManner::NASAL;
		if (s == "PLOSIVE")     return ConsonantManner::PLOSIVE;
		if (s == "SIBAFF")      return ConsonantManner::SIBAFF;
		if (s == "AFFRICATE")   return ConsonantManner::AFFRICATE;
		if (s == "SIBFRIC")     return ConsonantManner::SIBFRIC;
		if (s == "FRICATIVE")   return ConsonantManner::FRICATIVE;
		if (s == "APPROXIMANT") return ConsonantManner::APPROXIMANT;
		if (s == "FLAP")        return ConsonantManner::FLAP;
		if (s == "TRILL")       return ConsonantManner::TRILL;
		if (s == "LATAFF")      return ConsonantManner::LATAFF;
		if (s == "LATFRIC")     return ConsonantManner::LATFRIC;
		if (s == "LATAPP")      return ConsonantManner::LATAPP;
		if (s == "LATFLAP")     return ConsonantManner::LATFLAP;
		return ConsonantManner::NOT_CONSONANT;
	}
};

#endif // SYLLABLE_BUILDER_H
