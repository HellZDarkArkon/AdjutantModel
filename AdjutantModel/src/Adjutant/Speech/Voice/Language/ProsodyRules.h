#ifndef PROSODY_RULES_H
#define PROSODY_RULES_H

#include "../Vocalics/FootParser.h"

// ---------------------------------------------------------------------------
// ProsodyRules
// A flat, serialisable record of all prosodic pipeline parameters for a
// Language.  Language::Load() populates this struct from the dat file and
// then calls Language::ApplyRulesToPipeline() to push the values into the
// owned FootParser, MoraicGrid and IntonationModel instances.
//
// FootParser config  — directionality, foot type, weight-sensitivity
// MoraicGrid config  — mora duration weights and stress scaling
// Intonation config  — base F0, per-stress F0/amplitude scales, declination
// ---------------------------------------------------------------------------
struct ProsodyRules
{
	// --- FootParser ---------------------------------------------------------
	FootParser::Direction        footDirection     = FootParser::Direction::LEFT_TO_RIGHT;
	FootType                     defaultFootType   = FootType::TROCHEE;
	FootParser::PrimaryPlacement primaryPlacement  = FootParser::PrimaryPlacement::RIGHTMOST;
	bool                         weightSensitive   = true;

	// --- MoraicGrid ---------------------------------------------------------
	double baseMoraMs      = 80.0;   // duration of one mora at neutral stress (ms)
	double onsetPadMs      = 15.0;   // added per onset consonant (ms)
	double primaryScale    = 1.40;   // duration multiplier for PRIMARY stress
	double secondaryScale  = 1.20;   // duration multiplier for SECONDARY stress
	double unstressedScale = 0.85;   // duration multiplier for UNSTRESSED

	// --- IntonationModel ----------------------------------------------------
	double baseF0              = 210.0; // neutral speaker F0 (Hz)
	double primaryF0Scale      = 1.25;
	double secondaryF0Scale    = 1.10;
	double unstressedF0Scale   = 0.90;
	double primaryAmpScale     = 1.20;
	double secondaryAmpScale   = 1.10;
	double unstressedAmpScale  = 0.80;
	double declinationRate     = 0.03; // F0 drop per unit of normalised word position
	double onsetConsonantShare = 0.12; // fraction of syllable duration per onset consonant
	double codaConsonantShare  = 0.10; // fraction of syllable duration per coda consonant
};

#endif // PROSODY_RULES_H
