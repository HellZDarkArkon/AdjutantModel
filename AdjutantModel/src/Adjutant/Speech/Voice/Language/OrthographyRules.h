#ifndef ORTHOGRAPHY_RULES_H
#define ORTHOGRAPHY_RULES_H

#include "../Vocalics/Phoneme.h"
#include <string>
#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// OrthographyRules
// Maps written graphemes (single characters or digraphs) to phoneme sequences
// for a Language.  Used by the text-to-phoneme front-end before the
// phonological pipeline runs.
//
// script        — writing system identifier (e.g. "latin", "cyrillic",
//                 "hiragana", "devanagari").
// caseSensitive — whether grapheme lookup is case-sensitive.
// graphemeMap   — key: grapheme string (may be multi-char for digraphs/trigraphs)
//                 value: ordered phoneme sequence that grapheme represents in
//                 this language (context-free default; context-sensitive rules
//                 are an extension point).
// ---------------------------------------------------------------------------
struct OrthographyRules
{
	std::string script       = "latin";
	bool        caseSensitive = false;
	std::map<std::string, std::vector<PhonemeType>> graphemeMap;
};

#endif // ORTHOGRAPHY_RULES_H
