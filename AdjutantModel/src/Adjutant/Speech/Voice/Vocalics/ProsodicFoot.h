#ifndef PROSODIC_FOOT_H
#define PROSODIC_FOOT_H

#include "Syllable.h"
#include <vector>
#include <cassert>

// ---------------------------------------------------------------------------
// StressLevel
// Assigned to each syllable once foot structure is resolved.
// PRIMARY   — nuclear/main-word stress (one per prosodic word).
// SECONDARY — head of a non-primary foot (one or more per word).
// UNSTRESSED — foot-internal non-head (weak position).
// ---------------------------------------------------------------------------
enum class StressLevel
{
	PRIMARY,
	SECONDARY,
	UNSTRESSED,
};

// ---------------------------------------------------------------------------
// FootType
// Classical foot templates from metrical phonology.
// The head (strong/stressed) position is encoded in ProsodicFoot::headIndex.
// ---------------------------------------------------------------------------
enum class FootType
{
	TROCHEE,     // SW  — head-initial; English default (TA-ble, WIN-dow)
	IAMB,        // WS  — head-final; Arabic, Persian, some Romance
	DACTYL,      // SWW — ternary head-initial; Latin, Greek, Finnish
	AMPHIBRACH,  // WSW — ternary mid-head; some Slavic/Baltic
	SPONDEE,     // SS  — both positions heavy; degenerate equality
	DEGENERATE,  // S   — single heavy syllable with no weak partner
};

// ---------------------------------------------------------------------------
// ProsodicFoot
// A grouping of 1–3 syllables with one designated head (the stressed member).
//
// Syllables are stored by value (copied from the SyllableBuilder output).
// headIndex is always a valid index into syllables[].
// ---------------------------------------------------------------------------
struct ProsodicFoot
{
	std::vector<Syllable> syllables; // 1–3 syllables in linear order
	int       headIndex = 0;         // Index of the stressed (head) syllable
	FootType  type      = FootType::TROCHEE;

	// Convenience constructors
	ProsodicFoot() = default;

	explicit ProsodicFoot(const Syllable& sole)
		: syllables{ sole }, headIndex(0), type(FootType::DEGENERATE) {}

	ProsodicFoot(const Syllable& a, const Syllable& b, int head, FootType ft)
		: syllables{ a, b }, headIndex(head), type(ft)
	{
		assert(head == 0 || head == 1);
	}

	ProsodicFoot(const Syllable& a, const Syllable& b, const Syllable& c,
	             int head, FootType ft)
		: syllables{ a, b, c }, headIndex(head), type(ft)
	{
		assert(head >= 0 && head <= 2);
	}

	// -----------------------------------------------------------------------
	// Accessors
	// -----------------------------------------------------------------------

	const Syllable& Head() const { return syllables[headIndex]; }
	Syllable&       Head()       { return syllables[headIndex]; }

	int  Size()         const { return (int)syllables.size(); }
	bool IsBinary()     const { return syllables.size() == 2; }
	bool IsTernary()    const { return syllables.size() == 3; }
	bool IsDegenerate() const { return syllables.size() == 1; }

	// True if every syllable in the foot is heavy or superheavy (spondee-like).
	bool IsAllHeavy() const
	{
		for (const Syllable& s : syllables)
			if (s.GetWeight() == SyllableWeight::LIGHT) return false;
		return true;
	}

	// Returns the stress level of syllable at position i within this foot.
	// The head receives at least SECONDARY; the caller upgrades one head to
	// PRIMARY after the full foot list is assembled.
	StressLevel StressAt(int i) const
	{
		assert(i >= 0 && i < Size());
		return (i == headIndex) ? StressLevel::SECONDARY : StressLevel::UNSTRESSED;
	}
};

#endif // PROSODIC_FOOT_H
