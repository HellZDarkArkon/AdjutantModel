#ifndef FOOT_PARSER_H
#define FOOT_PARSER_H

#include "ProsodicWord.h"
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// FootParser
// Groups a flat syllable sequence into a ProsodicWord using weight-sensitive
// binary foot parsing.
//
// Defaults — trochaic, L→R, weight-sensitive, rightmost primary:
//   Models English-like systems out of the box.
//   Override via LoadStressRules() or set public members directly.
//
// Algorithm (L→R weight-sensitive trochee):
//   1. If current syllable is HEAVY/SUPERHEAVY:
//        - Pair with following LIGHT  → moraic trochee (H,L), head=0.
//        - Else (H,H or lone H)       → degenerate foot, advance by 1.
//   2. Else (current is LIGHT):
//        - Pair with next syllable    → binary foot; head from defaultFootType.
//        - In weight-sensitive mode, promote a following HEAVY to head=1.
//        - If no following syllable   → degenerate foot.
//   R→L: reverse syllables, apply L→R, then reverse feet and syllable-order
//        within each foot, and mirror head indices.
//
// Usage:
//   FootParser parser;
//   parser.LoadStressRules("path/to/Syllables.dat");
//   ProsodicWord word = parser.Parse(syllables);
// ---------------------------------------------------------------------------
class FootParser
{
public:
	// Directionality of the foot-building scan.
	enum class Direction { LEFT_TO_RIGHT, RIGHT_TO_LEFT };

	// Which foot's head receives PRIMARY stress.
	enum class PrimaryPlacement { RIGHTMOST, LEFTMOST, HEAVIEST };

	// -----------------------------------------------------------------------
	// Configuration — set directly or populated by LoadStressRules()
	// -----------------------------------------------------------------------
	Direction        direction        = Direction::LEFT_TO_RIGHT;
	FootType         defaultFootType  = FootType::TROCHEE;
	PrimaryPlacement primaryPlacement = PrimaryPlacement::RIGHTMOST;
	bool             weightSensitive  = true;

	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Read the #stress_rules section of Syllables.dat.
	// Missing file or section is silently ignored; defaults remain in effect.
	bool LoadStressRules(const std::string& path)
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

			if (section != "stress_rules") continue;

			auto pos = line.find('#');
			if (pos != std::string::npos) line = line.substr(0, pos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tok = Tokenise(line);
			if (tok.size() < 2) continue;

			if      (tok[0] == "foot_direction")
				direction = (tok[1] == "right_to_left") ? Direction::RIGHT_TO_LEFT
				                                        : Direction::LEFT_TO_RIGHT;
			else if (tok[0] == "foot_type")
				defaultFootType = FootTypeFromString(tok[1]);
			else if (tok[0] == "weight_sensitive")
				weightSensitive = (tok[1] == "true" || tok[1] == "1");
			else if (tok[0] == "primary_placement")
				primaryPlacement = PlacementFromString(tok[1]);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Parsing
	// -----------------------------------------------------------------------

	// Groups `syllables` into feet and returns the assembled ProsodicWord
	// with PRIMARY / SECONDARY / UNSTRESSED stress fully resolved.
	ProsodicWord Parse(const std::vector<Syllable>& syllables) const
	{
		if (syllables.empty()) return {};

		std::vector<ProsodicFoot> feet;
		if (direction == Direction::LEFT_TO_RIGHT)
			BuildFeetLTR(syllables, feet);
		else
			BuildFeetRTL(syllables, feet);

		ProsodicWord word;
		word.feet             = std::move(feet);
		word.primaryFootIndex = ResolvePrimary(word.feet);
		return word;
	}

private:
	// -----------------------------------------------------------------------
	// Foot construction
	// -----------------------------------------------------------------------

	void BuildFeetLTR(const std::vector<Syllable>& syls,
	                  std::vector<ProsodicFoot>&    feet) const
	{
		int i = 0;
		const int n = (int)syls.size();

		while (i < n)
		{
			const Syllable& cur      = syls[i];
			bool            curHeavy = (cur.GetWeight() != SyllableWeight::LIGHT);

			// Weight-sensitive path for heavy/superheavy head syllables
			if (weightSensitive && curHeavy)
			{
				if (i + 1 < n && syls[i + 1].GetWeight() == SyllableWeight::LIGHT)
				{
					// (H, L) — quantity-sensitive trochee; heavy syllable heads
					feet.push_back(ProsodicFoot(cur, syls[i + 1], 0, FootType::TROCHEE));
					i += 2;
				}
				else
				{
					// (H, H) or lone heavy — degenerate foot, advance one at a time
					feet.push_back(ProsodicFoot(cur));
					++i;
				}
				continue;
			}

			// Standard binary pair
			if (i + 1 < n)
			{
				// Default head from foot type; IAMB/AMPHIBRACH put head at 1
				int head = (defaultFootType == FootType::IAMB ||
				            defaultFootType == FootType::AMPHIBRACH) ? 1 : 0;

				// Weight-sensitive override: if light precedes heavy, promote heavy
				if (weightSensitive && !curHeavy &&
				    syls[i + 1].GetWeight() != SyllableWeight::LIGHT)
					head = 1;

				feet.push_back(ProsodicFoot(cur, syls[i + 1], head, defaultFootType));
				i += 2;
			}
			else
			{
				// Lone remaining syllable
				feet.push_back(ProsodicFoot(cur));
				++i;
			}
		}
	}

	// R→L: reverse the input, build L→R, then restore linear order by
	// reversing the foot list, reversing syllables within each foot, and
	// mirroring each head index so that head positions are correct.
	void BuildFeetRTL(const std::vector<Syllable>& syls,
	                  std::vector<ProsodicFoot>&    feet) const
	{
		std::vector<Syllable> rev(syls.rbegin(), syls.rend());
		BuildFeetLTR(rev, feet);

		std::reverse(feet.begin(), feet.end());
		for (ProsodicFoot& f : feet)
		{
			std::reverse(f.syllables.begin(), f.syllables.end());
			f.headIndex = (f.Size() - 1) - f.headIndex;
		}
	}

	// -----------------------------------------------------------------------
	// Primary stress placement
	// -----------------------------------------------------------------------

	int ResolvePrimary(const std::vector<ProsodicFoot>& feet) const
	{
		if (feet.empty()) return 0;

		if (primaryPlacement == PrimaryPlacement::LEFTMOST)  return 0;
		if (primaryPlacement == PrimaryPlacement::RIGHTMOST) return (int)feet.size() - 1;

		// HEAVIEST: foot whose head has the maximum SyllableWeight;
		// ties are broken rightmost (later index wins).
		int best      = 0;
		int bestWeight = -1;
		for (int i = 0; i < (int)feet.size(); ++i)
		{
			int w = static_cast<int>(feet[i].Head().GetWeight());
			if (w >= bestWeight)
			{
				bestWeight = w;
				best       = i;
			}
		}
		return best;
	}

	// -----------------------------------------------------------------------
	// .dat parsing helpers
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

	static FootType FootTypeFromString(const std::string& s)
	{
		if (s == "iamb")       return FootType::IAMB;
		if (s == "dactyl")     return FootType::DACTYL;
		if (s == "amphibrach") return FootType::AMPHIBRACH;
		if (s == "spondee")    return FootType::SPONDEE;
		return FootType::TROCHEE;
	}

	static PrimaryPlacement PlacementFromString(const std::string& s)
	{
		if (s == "leftmost") return PrimaryPlacement::LEFTMOST;
		if (s == "heaviest") return PrimaryPlacement::HEAVIEST;
		return PrimaryPlacement::RIGHTMOST;
	}
};

#endif // FOOT_PARSER_H
