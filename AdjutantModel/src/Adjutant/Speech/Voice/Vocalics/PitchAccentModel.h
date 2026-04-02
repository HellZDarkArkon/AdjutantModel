#ifndef PITCH_ACCENT_MODEL_H
#define PITCH_ACCENT_MODEL_H

#include "ProsodicWord.h"
#include "PhraseContour.h"
#include <vector>

// ---------------------------------------------------------------------------
// PitchAccentType
// Simplified ToBI-style pitch accent labels used for inter-accent
// coarticulation rule matching.
//
//   H_STAR        — high peak; default primary-stress accent in the medial
//                   region of a declarative phrase.
//   L_STAR        — low valley; assigned to secondary-stressed syllables
//                   (deaccented material).
//   L_PLUS_H_STAR — rising accent (L% precedes H*); phrase-initial primary
//                   accents and interrogative or continuation phrase-finals.
//   H_PLUS_L_STAR — falling-peak accent (H* immediately followed by L%);
//                   phrase-final primary accent in declarative phrases.
//   DOWNSTEP_H    — downstepped high (!H*); assigned to primary accents that
//                   follow an earlier H_STAR or DOWNSTEP_H in the same phrase.
//   NONE          — no pitch accent; unstressed syllables.
// ---------------------------------------------------------------------------
enum class PitchAccentType
{
    NONE,
    H_STAR,
    L_STAR,
    L_PLUS_H_STAR,
    H_PLUS_L_STAR,
    DOWNSTEP_H,
    AUTO          // sentinel: delegate to PitchAccentModel::Assign() (never stored as a result)
    DOWNSTEP_H
};

// ---------------------------------------------------------------------------
// PitchAccentModel
// Assigns ToBI-style pitch accents to every syllable in a phrase, then
// applies pairwise coarticulation rules: when an accented syllable follows
// another, the later syllable's phraseF0Base is multiplied by the scale
// associated with that (preceding, current) accent pair.
//
// Coarticulation table (defaults):
//
//   preceding       current           scale    perceptual effect
//   ---------------------------------------------------------------
//   H_STAR        → H_STAR            0.88     downstep cascade
//   H_STAR        → L_PLUS_H_STAR     0.93     compressed rise
//   H_STAR        → H_PLUS_L_STAR     0.92     fall from lower peak
//   H_STAR        → L_STAR            0.95     post-accent low settling
//   H_STAR        → DOWNSTEP_H        0.88     onset of downstep chain
//   L_STAR        → H_STAR            1.10     contrast boost
//   L_STAR        → L_PLUS_H_STAR     1.05     enhanced rise after low
//   L_PLUS_H_STAR → H_STAR            0.95     post-rise settling
//   L_PLUS_H_STAR → H_PLUS_L_STAR     0.93     compressed following fall
//   H_PLUS_L_STAR → L_STAR            1.05     recovery after boundary fall
//   H_PLUS_L_STAR → H_STAR            1.02     slight recovery
//   DOWNSTEP_H    → DOWNSTEP_H        0.88     continued downstep chain
//   DOWNSTEP_H    → H_STAR            0.92     partial recovery
//   DOWNSTEP_H    → H_PLUS_L_STAR     0.90     compressed boundary fall
//   (all others)                      1.00     no modification
//
// Accent assignment (based on stress level, normalised phrase position, and
// phrase boundary type):
//
//   UNSTRESSED syllable → NONE
//   SECONDARY  syllable → L_STAR
//   PRIMARY    syllable:
//     tPhrase < 0.15                                → L_PLUS_H_STAR  (phrase-initial rise)
//     tPhrase ≥ 0.80 and DECLARATIVE / NEUTRAL      → H_PLUS_L_STAR  (phrase-final fall)
//     tPhrase ≥ 0.80 and INTERROGATIVE / CONTINUATION → L_PLUS_H_STAR (phrase-final rise)
//     preceded by H_STAR or DOWNSTEP_H              → DOWNSTEP_H     (downstep chain)
//     otherwise                                     → H_STAR         (medial default)
// ---------------------------------------------------------------------------
class PitchAccentModel
{
public:
    // Assign accent types and apply coarticulation to phraseF0Bases in place.
    // All three input vectors must be the same length as phraseF0Bases.
    void Apply(const std::vector<StressLevel>& stressLevels,
               const std::vector<double>&      tPhrasePerSyl,
               PhraseType                      phraseType,
               std::vector<double>&            phraseF0Bases) const
    {
        const int n = (int)stressLevels.size();
        if (n == 0) return;

        // --- Step 1: assign accent types to every syllable -----------------
        std::vector<PitchAccentType> accents(n, PitchAccentType::NONE);
        PitchAccentType prevPrimary = PitchAccentType::NONE;

        for (int i = 0; i < n; ++i)
        {
            accents[i] = Assign(stressLevels[i], tPhrasePerSyl[i],
                                phraseType, prevPrimary);
            if (stressLevels[i] == StressLevel::PRIMARY)
                prevPrimary = accents[i];
        }

        // --- Step 2: coarticulation ----------------------------------------
        // Walk the accent sequence; skip NONE syllables.  For each adjacent
        // pair of accented syllables, look up the rule and scale the later one.
        PitchAccentType prevAccent = PitchAccentType::NONE;
        for (int i = 0; i < n; ++i)
        {
            if (accents[i] == PitchAccentType::NONE) continue;

            if (prevAccent != PitchAccentType::NONE)
                phraseF0Bases[i] *= CoarticScale(prevAccent, accents[i]);

            prevAccent = accents[i];
        }
    }

    // Overload that accepts per-syllable pitch accent overrides from the
    // prosody dictionary.  For each index i where accentOverrides[i] is not
    // AUTO, that value replaces the algorithmic Assign() result before
    // coarticulation is propagated to neighbouring accented syllables.
    void Apply(const std::vector<StressLevel>&    stressLevels,
               const std::vector<double>&         tPhrasePerSyl,
               PhraseType                         phraseType,
               std::vector<double>&               phraseF0Bases,
               const std::vector<PitchAccentType>& accentOverrides) const
    {
        const int n = (int)stressLevels.size();
        if (n == 0) return;

        std::vector<PitchAccentType> accents(n, PitchAccentType::NONE);
        PitchAccentType prevPrimary = PitchAccentType::NONE;

        for (int i = 0; i < n; ++i)
        {
            bool hasOverride = i < (int)accentOverrides.size()
                            && accentOverrides[i] != PitchAccentType::AUTO;
            accents[i] = hasOverride
                       ? accentOverrides[i]
                       : Assign(stressLevels[i], tPhrasePerSyl[i],
                                phraseType, prevPrimary);
            if (stressLevels[i] == StressLevel::PRIMARY)
                prevPrimary = accents[i];
        }

        PitchAccentType prevAccent = PitchAccentType::NONE;
        for (int i = 0; i < n; ++i)
        {
            if (accents[i] == PitchAccentType::NONE) continue;

            if (prevAccent != PitchAccentType::NONE)
                phraseF0Bases[i] *= CoarticScale(prevAccent, accents[i]);

            prevAccent = accents[i];
        }
    }

private:
    // Assign an accent type from stress, phrase position, boundary type, and
    // the type assigned to the most recent PRIMARY syllable in the phrase.
    static PitchAccentType Assign(StressLevel     sl,
                                  double          tPhrase,
                                  PhraseType      phraseType,
                                  PitchAccentType prevPrimary)
    {
        switch (sl)
        {
        case StressLevel::UNSTRESSED:
            return PitchAccentType::NONE;

        case StressLevel::SECONDARY:
            return PitchAccentType::L_STAR;

        case StressLevel::PRIMARY:
        {
            // Phrase-initial accent: rising pre-nuclear
            if (tPhrase < 0.15)
                return PitchAccentType::L_PLUS_H_STAR;

            // Phrase-final accent: boundary-aligned
            if (tPhrase >= 0.80)
            {
                if (phraseType == PhraseType::INTERROGATIVE
                    || phraseType == PhraseType::CONTINUATION)
                    return PitchAccentType::L_PLUS_H_STAR;

                // DECLARATIVE, NEUTRAL, EXCLAMATORY all use a falling peak
                return PitchAccentType::H_PLUS_L_STAR;
            }

            // Medial: downstep chain after any H-type accent
            if (prevPrimary == PitchAccentType::H_STAR
                || prevPrimary == PitchAccentType::DOWNSTEP_H)
                return PitchAccentType::DOWNSTEP_H;

            return PitchAccentType::H_STAR;
        }

        default:
            return PitchAccentType::NONE;
        }
    }

    // Coarticulation scale: multiplicative modifier applied to the current
    // syllable's phraseF0Base given the preceding non-NONE accent type.
    static double CoarticScale(PitchAccentType prev, PitchAccentType curr)
    {
        switch (prev)
        {
        case PitchAccentType::H_STAR:
            switch (curr)
            {
            case PitchAccentType::H_STAR:         return 0.88;
            case PitchAccentType::L_PLUS_H_STAR:  return 0.93;
            case PitchAccentType::H_PLUS_L_STAR:  return 0.92;
            case PitchAccentType::L_STAR:          return 0.95;
            case PitchAccentType::DOWNSTEP_H:      return 0.88;
            default: break;
            }
            break;

        case PitchAccentType::L_STAR:
            switch (curr)
            {
            case PitchAccentType::H_STAR:         return 1.10;
            case PitchAccentType::L_PLUS_H_STAR:  return 1.05;
            default: break;
            }
            break;

        case PitchAccentType::L_PLUS_H_STAR:
            switch (curr)
            {
            case PitchAccentType::H_STAR:         return 0.95;
            case PitchAccentType::H_PLUS_L_STAR:  return 0.93;
            default: break;
            }
            break;

        case PitchAccentType::H_PLUS_L_STAR:
            switch (curr)
            {
            case PitchAccentType::L_STAR:          return 1.05;
            case PitchAccentType::H_STAR:          return 1.02;
            default: break;
            }
            break;

        case PitchAccentType::DOWNSTEP_H:
            switch (curr)
            {
            case PitchAccentType::DOWNSTEP_H:      return 0.88;
            case PitchAccentType::H_STAR:          return 0.92;
            case PitchAccentType::H_PLUS_L_STAR:   return 0.90;
            default: break;
            }
            break;

        default:
            break;
        }

        return 1.0;  // no modification
    }
};

#endif // PITCH_ACCENT_MODEL_H
