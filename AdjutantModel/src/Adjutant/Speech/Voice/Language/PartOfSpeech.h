#ifndef PART_OF_SPEECH_H
#define PART_OF_SPEECH_H

#include <string>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// PartOfSpeech
// Grammatical category of a word used to select the correct pronunciation
// for heteronyms — words that share the same spelling but differ in sound
// and meaning depending on their syntactic role.
//
// Pass PartOfSpeech::UNKNOWN to LanguageDictionary::LookupWithContext when
// no grammatical context is available; the lookup will fall back to the
// plain dictionary entry.
//
// Examples (en_US):
//   "read"   VERB         → R IY D  ("are we reading today?")
//   "read"   VERB_PAST    → R EH D  ("I read a book yesterday")
//   "lead"   VERB         → L IY D  ("she will lead the team")
//   "lead"   NOUN         → L EH D  ("a pipe made of lead")
//   "live"   VERB         → L IH V  ("we live here")
//   "live"   ADJECTIVE    → L AY IH V ("a live performance")
//   "wind"   NOUN         → W IH N D ("the wind is strong")
//   "wind"   VERB         → W AY IH N D ("wind the clock")
//   "close"  VERB         → K L OW UH Z ("close the door")
//   "close"  ADJECTIVE    → K L OW UH S ("a close friend")
// ---------------------------------------------------------------------------
enum class PartOfSpeech
{
	UNKNOWN,        // no context — fall back to plain dictionary entry
	NOUN,
	VERB,           // present tense / infinitive / base form
	VERB_PAST,      // simple past or past participle
	VERB_GERUND,    // -ing form
	ADJECTIVE,
	ADVERB,
	PRONOUN,
	PREPOSITION,
	CONJUNCTION,
	INTERJECTION,
	DETERMINER
};

// Resolve a string token (case-insensitive) to a PartOfSpeech.
// Used when parsing [TAG] annotations in .dict files.
// Returns PartOfSpeech::UNKNOWN for unrecognised tokens.
inline PartOfSpeech PartOfSpeechFromString(const std::string& s)
{
	std::string u = s;
	std::transform(u.begin(), u.end(), u.begin(),
	               [](unsigned char c) { return (char)std::toupper(c); });

	if (u == "NOUN")         return PartOfSpeech::NOUN;
	if (u == "VERB")         return PartOfSpeech::VERB;
	if (u == "VERB_PAST")    return PartOfSpeech::VERB_PAST;
	if (u == "VERB_GERUND")  return PartOfSpeech::VERB_GERUND;
	if (u == "ADJECTIVE")    return PartOfSpeech::ADJECTIVE;
	if (u == "ADVERB")       return PartOfSpeech::ADVERB;
	if (u == "PRONOUN")      return PartOfSpeech::PRONOUN;
	if (u == "PREPOSITION")  return PartOfSpeech::PREPOSITION;
	if (u == "CONJUNCTION")  return PartOfSpeech::CONJUNCTION;
	if (u == "INTERJECTION") return PartOfSpeech::INTERJECTION;
	if (u == "DETERMINER")   return PartOfSpeech::DETERMINER;
	return PartOfSpeech::UNKNOWN;
}

#endif // PART_OF_SPEECH_H
