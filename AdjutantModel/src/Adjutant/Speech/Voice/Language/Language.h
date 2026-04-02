#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "PhonologyRules.h"
#include "ProsodyRules.h"
#include "OrthographyRules.h"
#include "MorphologyRules.h"
#include "SyntaxRules.h"
#include "GrammarRules.h"
#include "LanguageDictionary.h"

#include "../Vocalics/SyllableBuilder.h"
#include "../Vocalics/FootParser.h"
#include "../Vocalics/MoraicGrid.h"
#include "../Vocalics/IntonationModel.h"
#include "LanguageCortex.h"

#include <string>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// LanguageRules
// Aggregates all rule domains for a Language into one inspectable record.
// Language::Load() populates this from a language dat file; the contained
// structs are then used to configure the owned pipeline instances.
// ---------------------------------------------------------------------------
struct LanguageRules
{
	PhonologyRules  phono;
	ProsodyRules    prosody;
	OrthographyRules ortho;
	MorphologyRules  morph;
	SyntaxRules      syntax;
	GrammarRules     grammar;
};

// ---------------------------------------------------------------------------
// Language
// Single configuration point for the phonological pipeline.
//
// Owns:
//   syllableBuilder — segments phoneme sequences into syllables
//   footParser      — groups syllables into prosodic feet
//   moraicGrid      — maps feet onto a timing grid
//   intonationModel — generates per-phoneme F0/amplitude/duration params
//   dict            — word → phoneme sequence lookup
//
// Dat file format (language-specific, superset of Syllables.dat):
//   #language          — name, id
//   #phonology         — dat paths, max onset/coda lengths, stress flag
//   #prosody           — foot type, mora weights, F0/amp params
//                        (same key names as #stress_rules / #mora_weights /
//                         #intonation_params in Syllables.dat)
//   #orthography       — script, case-sensitivity, grapheme entries
//   #morphology        — type, prefix/suffix/infix entries
//   #syntax            — word order, head directionality flags
//   #grammar           — case count, gender, tonality flags
//   #dictionary_src    — path to the word→phoneme lookup dat
//
// Usage:
//   Language lang;
//   if (lang.Load("English.dat")) {
//       auto syllables = lang.GetSyllableBuilder().Build(phonemes);
//       auto word      = lang.GetFootParser().Parse(syllables);
//       lang.GetMoraicGrid().Compute(word);
//       auto params    = lang.GetIntonationModel().Compute(word, ...);
//   }
// ---------------------------------------------------------------------------
class Language
{
public:
	// -----------------------------------------------------------------------
	// Language identifier — determines default pipeline parameters and the
	// dictionary file to load when none is specified explicitly.
	// -----------------------------------------------------------------------
	enum class LanguageID
	{
		UNKNOWN,
		ENGLISH,
		ESPANOL,
		FRANCAIS,
		RUSKIY,
		NIHONGO,
		POLSKI
	};

	Language() = default;

	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Parse `path` and configure all owned pipeline components.
	// Returns true if the file was opened successfully.
	// Missing sections fall back to per-component defaults.
	bool Load(const std::string& path)
	{
		mDatPath = path;

		std::ifstream f(path);
		if (!f.is_open()) return false;

		std::string line, section;
		while (std::getline(f, line))
		{
			// Trim leading whitespace before section-header check
			line.erase(0, line.find_first_not_of(" \t\r\n"));

			if (!line.empty() && line[0] == '#')
			{
				section = line.substr(1);
				section.erase(0, section.find_first_not_of(" \t\r\n"));
				section.erase(section.find_last_not_of(" \t\r\n") + 1);
				continue;
			}

			// Strip inline comments after section-header check
			auto cpos = line.find('#');
			if (cpos != std::string::npos) line = line.substr(0, cpos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tok = Tokenise(line);
			if (tok.size() < 2) continue;

			// --- #language --------------------------------------------------
			if (section == "language")
			{
				if      (tok[0] == "id")   mID = IDFromString(tok[1]);
				else if (tok[0] == "name") mName = tok[1];
			}

			// --- #phonology -------------------------------------------------
			else if (section == "phonology")
			{
				if      (tok[0] == "phonemes_dat")     mRules.phono.phonemesDatPath  = tok[1];
				else if (tok[0] == "syllables_dat")    mRules.phono.syllablesDatPath = tok[1];
				else if (tok[0] == "max_onset_length") mRules.phono.maxOnsetLength   = std::stoi(tok[1]);
				else if (tok[0] == "max_coda_length")  mRules.phono.maxCodaLength    = std::stoi(tok[1]);
				else if (tok[0] == "stress_phonemic")  mRules.phono.stressIsPhonemic = (tok[1] == "true" || tok[1] == "1");
			}

			// --- #prosody (mirrors #stress_rules + #mora_weights + #intonation_params) ---
			else if (section == "prosody")
			{
				ProsodyRules& pr = mRules.prosody;
				if      (tok[0] == "foot_direction")
					pr.footDirection = (tok[1] == "right_to_left")
									 ? FootParser::Direction::RIGHT_TO_LEFT
									 : FootParser::Direction::LEFT_TO_RIGHT;
				else if (tok[0] == "foot_type")         pr.defaultFootType   = FootTypeFromString(tok[1]);
				else if (tok[0] == "weight_sensitive")  pr.weightSensitive   = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "primary_placement")
				{
					if      (tok[1] == "leftmost")  pr.primaryPlacement = FootParser::PrimaryPlacement::LEFTMOST;
					else if (tok[1] == "heaviest")  pr.primaryPlacement = FootParser::PrimaryPlacement::HEAVIEST;
					else                            pr.primaryPlacement = FootParser::PrimaryPlacement::RIGHTMOST;
				}
				else if (tok[0] == "base_mora_ms")           pr.baseMoraMs           = std::stod(tok[1]);
				else if (tok[0] == "onset_pad_ms")           pr.onsetPadMs           = std::stod(tok[1]);
				else if (tok[0] == "primary_scale")          pr.primaryScale         = std::stod(tok[1]);
				else if (tok[0] == "secondary_scale")        pr.secondaryScale       = std::stod(tok[1]);
				else if (tok[0] == "unstressed_scale")       pr.unstressedScale      = std::stod(tok[1]);
				else if (tok[0] == "base_f0")                pr.baseF0               = std::stod(tok[1]);
				else if (tok[0] == "primary_f0_scale")       pr.primaryF0Scale       = std::stod(tok[1]);
				else if (tok[0] == "secondary_f0_scale")     pr.secondaryF0Scale     = std::stod(tok[1]);
				else if (tok[0] == "unstressed_f0_scale")    pr.unstressedF0Scale    = std::stod(tok[1]);
				else if (tok[0] == "primary_amp_scale")      pr.primaryAmpScale      = std::stod(tok[1]);
				else if (tok[0] == "secondary_amp_scale")    pr.secondaryAmpScale    = std::stod(tok[1]);
				else if (tok[0] == "unstressed_amp_scale")   pr.unstressedAmpScale   = std::stod(tok[1]);
				else if (tok[0] == "declination_rate")       pr.declinationRate      = std::stod(tok[1]);
				else if (tok[0] == "onset_consonant_share")  pr.onsetConsonantShare  = std::stod(tok[1]);
				else if (tok[0] == "coda_consonant_share")   pr.codaConsonantShare   = std::stod(tok[1]);
			}

			// --- #orthography -----------------------------------------------
			else if (section == "orthography")
			{
				if      (tok[0] == "script")         mRules.ortho.script        = tok[1];
				else if (tok[0] == "case_sensitive") mRules.ortho.caseSensitive = (tok[1] == "true" || tok[1] == "1");
				// grapheme entries: grapheme - PHONEME_A - PHONEME_B ...
				else
				{
					std::vector<PhonemeType> phonemes;
					for (size_t i = 1; i < tok.size(); ++i)
					{
						PhonemeType pt;
						if (PhonemeTypeFromString(tok[i], pt))
							phonemes.push_back(pt);
					}
					if (!phonemes.empty())
						mRules.ortho.graphemeMap[tok[0]] = std::move(phonemes);
				}
			}

			// --- #morphology ------------------------------------------------
			else if (section == "morphology")
			{
				if      (tok[0] == "type")        mRules.morph.type = MorphTypeFromString(tok[1]);
				else if (tok[0] == "prefix")      mRules.morph.prefixes.push_back(tok[1]);
				else if (tok[0] == "suffix")      mRules.morph.suffixes.push_back(tok[1]);
				else if (tok[0] == "infix")       mRules.morph.infixes.push_back(tok[1]);
				else if (tok[0] == "circumfix")   mRules.morph.circumfixes.push_back(tok[1]);
			}

			// --- #syntax ----------------------------------------------------
			else if (section == "syntax")
			{
				if      (tok[0] == "word_order")  mRules.syntax.wordOrder      = WordOrderFromString(tok[1]);
				else if (tok[0] == "head_initial") mRules.syntax.headInitial    = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "pro_drop")     mRules.syntax.proDropAllowed = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "verb_second")  mRules.syntax.verbSecond     = (tok[1] == "true" || tok[1] == "1");
			}

			// --- #grammar ---------------------------------------------------
			else if (section == "grammar")
			{
				if      (tok[0] == "case_count")           mRules.grammar.caseCount            = std::stoi(tok[1]);
				else if (tok[0] == "noun_class_count")     mRules.grammar.nounClassCount       = std::stoi(tok[1]);
				else if (tok[0] == "grammatical_gender")   mRules.grammar.hasGrammaticalGender = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "tonality")             mRules.grammar.hasTonality          = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "aspect")               mRules.grammar.hasAspect            = (tok[1] == "true" || tok[1] == "1");
				else if (tok[0] == "mood")                 mRules.grammar.hasMood              = (tok[1] == "true" || tok[1] == "1");
			}

			// --- #dictionary_src --------------------------------------------
			// Accepts either:
			//   path - relative/or/absolute/path.dict   (key-value form)
			//   relative/or/absolute/path.dict          (bare value form)
			else if (section == "dictionary_src")
			{
				if      (tok[0] == "path") mDictionarySrc = tok[1];
				else if (mDictionarySrc.empty()) mDictionarySrc = tok[0];
			}
		}

		ApplyRulesToPipeline();

		// Load syllable-builder params (uses the same dat; reads its own sections)
		if (!mRules.phono.syllablesDatPath.empty())
			mSyllableBuilder.LoadFromDat(mRules.phono.syllablesDatPath);
		else
			mSyllableBuilder.LoadFromDat(path);

		// Load the dictionary if a source path was resolved
		if (!mDictionarySrc.empty())
		{
			mDict.SetCaseSensitive(mRules.ortho.caseSensitive);
			mDict.Load(mDictionarySrc);
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// -----------------------------------------------------------------------
	// Language Cortex accessor — feature extraction and GPU learning pipeline
	// -----------------------------------------------------------------------
	LanguageCortex&       GetCortex()       { return mCortex; }
	const LanguageCortex& GetCortex() const { return mCortex; }

	// Pipeline accessors — return configured instances ready for use
	// -----------------------------------------------------------------------
	SyllableBuilder&       GetSyllableBuilder()       { return mSyllableBuilder; }
	const SyllableBuilder& GetSyllableBuilder() const { return mSyllableBuilder; }

	FootParser&            GetFootParser()            { return mFootParser; }
	const FootParser&      GetFootParser()      const { return mFootParser; }

	MoraicGrid&            GetMoraicGrid()            { return mMoraicGrid; }
	const MoraicGrid&      GetMoraicGrid()      const { return mMoraicGrid; }

	IntonationModel&       GetIntonationModel()       { return mIntonationModel; }
	const IntonationModel& GetIntonationModel() const { return mIntonationModel; }

	// -----------------------------------------------------------------------
	// Rule and dictionary accessors
	// -----------------------------------------------------------------------
	const LanguageRules&     GetRules()      const { return mRules; }
	const LanguageDictionary& GetDictionary() const { return mDict; }
	LanguageDictionary&       GetDictionary()       { return mDict; }

	LanguageID               GetID()         const { return mID; }
	const std::string&       GetName()       const { return mName; }

private:
	LanguageID       mID      = LanguageID::UNKNOWN;
	std::string      mName;
	std::string      mDatPath;
	std::string      mDictionarySrc;

	LanguageRules    mRules;

	// Owned, fully configured pipeline instances
	SyllableBuilder    mSyllableBuilder;
	FootParser         mFootParser;
	MoraicGrid         mMoraicGrid;
	IntonationModel    mIntonationModel;
	LanguageDictionary mDict;
	LanguageCortex     mCortex;

	// -----------------------------------------------------------------------
	// Push rule structs into the owned pipeline instances
	// -----------------------------------------------------------------------
	void ApplyRulesToPipeline()
	{
		const ProsodyRules& pr = mRules.prosody;

		// FootParser — all config is exposed as public members
		mFootParser.direction        = pr.footDirection;
		mFootParser.defaultFootType  = pr.defaultFootType;
		mFootParser.primaryPlacement = pr.primaryPlacement;
		mFootParser.weightSensitive  = pr.weightSensitive;

		// MoraicGrid — push via public Params struct
		MoraicGrid::Params mp;
		mp.baseMoraMs      = pr.baseMoraMs;
		mp.onsetPadMs      = pr.onsetPadMs;
		mp.primaryScale    = pr.primaryScale;
		mp.secondaryScale  = pr.secondaryScale;
		mp.unstressedScale = pr.unstressedScale;
		mMoraicGrid.ApplyParams(mp);

		// IntonationModel — push via public Params struct
		IntonationModel::Params ip;
		ip.baseF0              = pr.baseF0;
		ip.primaryF0Scale      = pr.primaryF0Scale;
		ip.secondaryF0Scale    = pr.secondaryF0Scale;
		ip.unstressedF0Scale   = pr.unstressedF0Scale;
		ip.primaryAmpScale     = pr.primaryAmpScale;
		ip.secondaryAmpScale   = pr.secondaryAmpScale;
		ip.unstressedAmpScale  = pr.unstressedAmpScale;
		ip.declinationRate     = pr.declinationRate;
		ip.onsetConsonantShare = pr.onsetConsonantShare;
		ip.codaConsonantShare  = pr.codaConsonantShare;
		mIntonationModel.ApplyParams(ip);
	}

	// -----------------------------------------------------------------------
	// Dat parsing helpers
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

	static LanguageID IDFromString(const std::string& s)
	{
		if (s == "ENGLISH") return LanguageID::ENGLISH;
		if (s == "ESPANOL") return LanguageID::ESPANOL;
		if (s == "FRANCAIS") return LanguageID::FRANCAIS;
		if (s == "RUSKIY")  return LanguageID::RUSKIY;
		if (s == "NIHONGO") return LanguageID::NIHONGO;
		if (s == "POLSKI")  return LanguageID::POLSKI;
		return LanguageID::UNKNOWN;
	}

	static FootType FootTypeFromString(const std::string& s)
	{
		if (s == "iamb")        return FootType::IAMB;
		if (s == "dactyl")      return FootType::DACTYL;
		if (s == "amphibrach")  return FootType::AMPHIBRACH;
		if (s == "spondee")     return FootType::SPONDEE;
		if (s == "degenerate")  return FootType::DEGENERATE;
		return FootType::TROCHEE;
	}

	static MorphologyType MorphTypeFromString(const std::string& s)
	{
		if (s == "isolating")     return MorphologyType::ISOLATING;
		if (s == "agglutinative") return MorphologyType::AGGLUTINATIVE;
		if (s == "polysynthetic") return MorphologyType::POLYSYNTHETIC;
		return MorphologyType::FUSIONAL;
	}

	static WordOrder WordOrderFromString(const std::string& s)
	{
		if (s == "SOV") return WordOrder::SOV;
		if (s == "VSO") return WordOrder::VSO;
		if (s == "VOS") return WordOrder::VOS;
		if (s == "OVS") return WordOrder::OVS;
		if (s == "OSV") return WordOrder::OSV;
		if (s == "FREE") return WordOrder::FREE;
		return WordOrder::SVO;
	}

	// Delegates directly to LanguageDictionary's resolver so that both the
	// #orthography grapheme map and the dictionary share one canonical table.
	static bool PhonemeTypeFromString(const std::string& name, PhonemeType& out)
	{
		return LanguageDictionary::PhonemeTypeFromString(name, out);
	}
};

#endif // LANGUAGE_H
