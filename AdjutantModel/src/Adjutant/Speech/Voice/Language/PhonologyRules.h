#ifndef PHONOLOGY_RULES_H
#define PHONOLOGY_RULES_H

#include "../Vocalics/Phoneme.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// PhonologyRules
// Defines the phoneme inventory and phonotactic constraints for a Language.
//
// inventory        — the set of PhonemeTypes active in this language.
//                    SyllableBuilder onset/coda cluster validation should
//                    reject any phoneme absent from this set.
// phonemesDatPath  — path to the Phonemes.dat acoustic data file from which
//                    PhonemeSynth loads formant values for this language.
// syllablesDatPath — path to the Syllables.dat (or language-specific dat)
//                    from which SyllableBuilder, FootParser, MoraicGrid and
//                    IntonationModel load their parameters.
// maxOnsetLength   — hard cap on onset consonant cluster size.
// maxCodaLength    — hard cap on coda consonant cluster size.
// stressIsPhonemic — true when lexical stress is contrastive (English, Russian);
//                    false for fixed-stress languages (French, Finnish).
// ---------------------------------------------------------------------------
struct PhonologyRules
{
	std::vector<PhonemeType> inventory;    // active phonemes for this language
	std::string phonemesDatPath;           // path to Phonemes.dat
	std::string syllablesDatPath;          // path to Syllables.dat (pipeline params)
	int  maxOnsetLength  = 3;
	int  maxCodaLength   = 3;
	bool stressIsPhonemic = true;
};

#endif // PHONOLOGY_RULES_H
