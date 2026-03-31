#ifndef MORPHOLOGY_RULES_H
#define MORPHOLOGY_RULES_H

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MorphologyRules
// Describes the morphological profile of a Language.
//
// MorphologyType — typological classification (from most-isolating to
//                  most-synthetic), used to tune morpheme-boundary detection.
// prefixes       — attested prefix morphemes (e.g. "un-", "re-", "pre-").
// suffixes       — attested suffix morphemes (e.g. "-ing", "-ed", "-ness").
// infixes        — attested infix morphemes (rare in English; present in
//                  Filipino, Arabic, etc.).
// circumfixes    — discontinuous affixes that wrap the stem (e.g. German
//                  ge-...-t for past participle).
// ---------------------------------------------------------------------------
enum class MorphologyType
{
	ISOLATING,      // little or no inflection (e.g. Mandarin, Vietnamese)
	AGGLUTINATIVE,  // transparent one-morpheme-one-meaning stacking (Turkish, Finnish)
	FUSIONAL,       // morphemes merge and fuse (English, Russian, Latin)
	POLYSYNTHETIC   // complex words encode full propositions (Inuktitut)
};

struct MorphologyRules
{
	MorphologyType       type = MorphologyType::FUSIONAL;
	std::vector<std::string> prefixes;
	std::vector<std::string> suffixes;
	std::vector<std::string> infixes;
	std::vector<std::string> circumfixes;
};

#endif // MORPHOLOGY_RULES_H
