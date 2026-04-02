#ifndef PROSODIC_WORD_H
#define PROSODIC_WORD_H

#include "ProsodicFoot.h"
#include <vector>
#include <utility>
#include <cassert>

// ---------------------------------------------------------------------------
// ProsodicWord
// A sequence of prosodic feet spanning one lexical word (or a short
// phonological phrase).  Built exclusively by FootParser::Parse().
//
// Stress hierarchy:
//   - Exactly one foot is the primary foot; its head syllable carries PRIMARY.
//   - Every other foot head carries SECONDARY stress.
//   - All non-head positions within any foot carry UNSTRESSED.
// ---------------------------------------------------------------------------
struct ProsodicWord
{
	std::vector<ProsodicFoot> feet;
	int primaryFootIndex = 0; // index into feet[] whose head is PRIMARY

	// When non-empty and size == TotalSyllables(), these stress levels override
	// the foot-structure-derived values in FlatSyllables() and MoraicGrid.
	// Set by SpeechEngine after a ProsodyDictionary hit; cleared per phrase.
	std::vector<StressLevel> stressOverride;

	// -----------------------------------------------------------------------
	// Stress query
	// -----------------------------------------------------------------------

	// Returns the stress level of syllable sylIdx within foot footIdx.
	StressLevel GetSyllableStress(int footIdx, int sylIdx) const
	{
		assert(footIdx >= 0 && footIdx < (int)feet.size());
		const ProsodicFoot& foot = feet[footIdx];
		assert(sylIdx >= 0 && sylIdx < foot.Size());
		if (sylIdx != foot.headIndex) return StressLevel::UNSTRESSED;
		return (footIdx == primaryFootIndex) ? StressLevel::PRIMARY
		                                     : StressLevel::SECONDARY;
	}

	// -----------------------------------------------------------------------
	// Flat accessors — downstream consumers (MoraicGrid, IntonationModel)
	// iterate these rather than indexing into nested feet.
	// -----------------------------------------------------------------------

	// Returns every (Syllable, StressLevel) pair in linear phonological order.
	// When stressOverride is populated and its size matches TotalSyllables(),
	// those values are used instead of the foot-structure-derived levels.
	std::vector<std::pair<Syllable, StressLevel>> FlatSyllables() const
	{
		std::vector<std::pair<Syllable, StressLevel>> result;
		result.reserve(TotalSyllables());

		const bool useOverride = !stressOverride.empty()
							  && (int)stressOverride.size() == TotalSyllables();
		int overrideIdx = 0;

		for (int fi = 0; fi < (int)feet.size(); ++fi)
		{
			const ProsodicFoot& foot = feet[fi];
			for (int si = 0; si < foot.Size(); ++si)
			{
				StressLevel sl = useOverride
							   ? stressOverride[overrideIdx++]
							   : GetSyllableStress(fi, si);
				result.emplace_back(foot.syllables[si], sl);
			}
		}
		return result;
	}

	// Total number of syllables across all feet.
	int TotalSyllables() const
	{
		int n = 0;
		for (const ProsodicFoot& f : feet) n += f.Size();
		return n;
	}

	bool IsEmpty() const { return feet.empty(); }
};

#endif // PROSODIC_WORD_H
