#ifndef PROSODY_DICTIONARY_H
#define PROSODY_DICTIONARY_H

#include "../Vocalics/PhraseContour.h"    // PhraseType
#include "../Vocalics/PitchAccentModel.h" // PitchAccentType (includes StressLevel transitively)
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// ProsodyEntry
// Parsed result of a single en_US.prosody word_prosody line.
//
//   stress       — one StressLevel per syllable, left-to-right (P/S/U).
//   accent       — pitch accent for the primary syllable; AUTO when
//                  accentIsAuto is true (delegates to PitchAccentModel).
//   accentIsAuto — true when the ACCENT token was "AUTO".
// ---------------------------------------------------------------------------
struct ProsodyEntry
{
	std::vector<StressLevel> stress;
	PitchAccentType          accent      = PitchAccentType::AUTO;
	bool                     accentIsAuto = true;
};

// ---------------------------------------------------------------------------
// ProsodyDictionary
// Parses an en_US.prosody file and provides per-word stress pattern and pitch
// accent overrides keyed by word (case-insensitive) with optional PhraseType
// context variants.
//
// Format (word_prosody section):
//   word              - S1 - S2 - ... - ACCENT
//   word[CONTEXT]     - S1 - S2 - ... - ACCENT
//
//   Token delimiter : ' - '  (space-hyphen-space)
//   Si tokens       : P | S | U
//   ACCENT token    : H_STAR | L_STAR | L_PLUS_H_STAR | H_PLUS_L_STAR |
//                     DOWNSTEP_H | NONE | AUTO
//   CONTEXT         : DECLARATIVE | INTERROGATIVE | CONTINUATION | EXCLAMATORY
//
// Lookup order: context-specific entry first, plain entry as fallback.
// Unknown CONTEXT labels are silently ignored (entry skipped).
// Duplicate keys: later entries in the file overwrite earlier ones.
// ---------------------------------------------------------------------------
class ProsodyDictionary
{
public:
	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Parse a .prosody file.  Only lines inside the #word_prosody section are
	// processed; everything outside is treated as a comment / file header.
	// Returns false only if the file cannot be opened.
	bool Load(const std::string& path)
	{
		std::ifstream f(path);
		if (!f.is_open()) return false;

		bool        inSection = false;
		std::string line;
		while (std::getline(f, line))
		{
			line.erase(0, line.find_first_not_of(" \t\r\n"));
			if (line.empty()) continue;

			// Section header / comment — only #word_prosody flips the gate on;
			// all other #-lines are treated as comments (don't turn the gate off).
			if (line[0] == '#')
			{
				std::string header = line.substr(1);
				header.erase(0, header.find_first_not_of(" \t\r\n"));
				header.erase(header.find_last_not_of(" \t\r\n") + 1);
				if (header == "word_prosody")
					inSection = true;
				continue;
			}

			if (!inSection) continue;

			// Strip inline comment
			auto cpos = line.find('#');
			if (cpos != std::string::npos) line = line.substr(0, cpos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tokens = Tokenise(line);
			if (tokens.size() < 3) continue; // need word + ≥1 stress + accent

			// Parse word token and optional [CONTEXT] tag.
			// Entries such as "kernel [DECLARATIVE]" (space before '[') are handled
			// by trimming the word part after stripping the tag.
			std::string wordTok = tokens[0];
			wordTok.erase(0, wordTok.find_first_not_of(" \t"));
			wordTok.erase(wordTok.find_last_not_of(" \t") + 1);

			PhraseType context;
			bool       hasContext = false;
			auto       tagOpen   = wordTok.find('[');
			if (tagOpen != std::string::npos)
			{
				auto tagClose = wordTok.find(']', tagOpen);
				if (tagClose != std::string::npos)
				{
					std::string tag = wordTok.substr(tagOpen + 1, tagClose - tagOpen - 1);
					wordTok = wordTok.substr(0, tagOpen);
					wordTok.erase(wordTok.find_last_not_of(" \t") + 1);
					if (PhraseTypeFromString(tag, context))
						hasContext = true;
					else
						continue; // unknown context label — skip
				}
			}

			std::transform(wordTok.begin(), wordTok.end(), wordTok.begin(),
			               [](unsigned char c) { return (char)std::tolower(c); });
			if (wordTok.empty()) continue;

			// tokens[1 .. size-2] = stress levels; tokens[size-1] = ACCENT
			ProsodyEntry entry;
			for (size_t i = 1; i + 1 < tokens.size(); ++i)
			{
				std::string s = tokens[i];
				s.erase(0, s.find_first_not_of(" \t"));
				s.erase(s.find_last_not_of(" \t") + 1);
				StressLevel sl;
				if (!StressLevelFromString(s, sl)) { entry.stress.clear(); break; }
				entry.stress.push_back(sl);
			}
			if (entry.stress.empty()) continue;

			std::string accentTok = tokens.back();
			accentTok.erase(0, accentTok.find_first_not_of(" \t"));
			accentTok.erase(accentTok.find_last_not_of(" \t\r\n") + 1);

			if (accentTok == "AUTO")
			{
				entry.accent      = PitchAccentType::AUTO;
				entry.accentIsAuto = true;
			}
			else
			{
				PitchAccentType at;
				if (!PitchAccentTypeFromString(accentTok, at)) continue;
				entry.accent      = at;
				entry.accentIsAuto = false;
			}

			if (hasContext)
				mContextEntries[wordTok][context] = std::move(entry);
			else
				mEntries[wordTok] = std::move(entry);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Lookup
	// -----------------------------------------------------------------------

	// Returns a pointer to the best-matching entry for (word, ctx):
	//   context-specific entry first, plain entry as fallback.
	// Returns nullptr if the word has no entry at all.
	const ProsodyEntry* Lookup(const std::string& word, PhraseType ctx) const
	{
		std::string key = Normalise(word);

		auto cit = mContextEntries.find(key);
		if (cit != mContextEntries.end())
		{
			auto pit = cit->second.find(ctx);
			if (pit != cit->second.end())
				return &pit->second;
		}

		auto it = mEntries.find(key);
		if (it != mEntries.end()) return &it->second;

		return nullptr;
	}

	// -----------------------------------------------------------------------
	// Utility
	// -----------------------------------------------------------------------

	bool   Empty() const { return mEntries.empty() && mContextEntries.empty(); }
	size_t Size()  const { return mEntries.size(); }
	void   Clear()       { mEntries.clear(); mContextEntries.clear(); }

private:
	std::map<std::string, ProsodyEntry>                           mEntries;
	std::map<std::string, std::map<PhraseType, ProsodyEntry>>     mContextEntries;

	// -----------------------------------------------------------------------
	// Parsing helpers
	// -----------------------------------------------------------------------

	static std::string Normalise(const std::string& s)
	{
		std::string out = s;
		std::transform(out.begin(), out.end(), out.begin(),
		               [](unsigned char c) { return (char)std::tolower(c); });
		return out;
	}

	static std::vector<std::string> Tokenise(const std::string& line)
	{
		std::vector<std::string> tokens;
		std::string remaining = line;
		const std::string delim = " - ";
		size_t pos = 0;
		while ((pos = remaining.find(delim)) != std::string::npos)
		{
			tokens.push_back(remaining.substr(0, pos));
			remaining = remaining.substr(pos + delim.size());
		}
		if (!remaining.empty()) tokens.push_back(remaining);
		return tokens;
	}

	static bool StressLevelFromString(const std::string& s, StressLevel& out)
	{
		if (s == "P") { out = StressLevel::PRIMARY;    return true; }
		if (s == "S") { out = StressLevel::SECONDARY;  return true; }
		if (s == "U") { out = StressLevel::UNSTRESSED; return true; }
		return false;
	}

	static bool PitchAccentTypeFromString(const std::string& s, PitchAccentType& out)
	{
		if (s == "H_STAR")        { out = PitchAccentType::H_STAR;        return true; }
		if (s == "L_STAR")        { out = PitchAccentType::L_STAR;        return true; }
		if (s == "L_PLUS_H_STAR") { out = PitchAccentType::L_PLUS_H_STAR; return true; }
		if (s == "H_PLUS_L_STAR") { out = PitchAccentType::H_PLUS_L_STAR; return true; }
		if (s == "DOWNSTEP_H")    { out = PitchAccentType::DOWNSTEP_H;    return true; }
		if (s == "NONE")          { out = PitchAccentType::NONE;          return true; }
		return false;
	}

	static bool PhraseTypeFromString(const std::string& s, PhraseType& out)
	{
		if (s == "DECLARATIVE")   { out = PhraseType::DECLARATIVE;   return true; }
		if (s == "INTERROGATIVE") { out = PhraseType::INTERROGATIVE; return true; }
		if (s == "CONTINUATION")  { out = PhraseType::CONTINUATION;  return true; }
		if (s == "EXCLAMATORY")   { out = PhraseType::EXCLAMATORY;   return true; }
		return false;
	}
};

#endif // PROSODY_DICTIONARY_H
