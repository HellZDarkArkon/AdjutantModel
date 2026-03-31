#ifndef SYLLABLE_H
#define SYLLABLE_H

#include "Phoneme.h"
#include <vector>
#include <string>

// Syllable weight classes used for prosodic/stress assignment.
enum class SyllableWeight
{
	LIGHT,       // Open syllable with short vowel (V, CV)
	HEAVY,       // Closed syllable or long vowel (CVC, CVV)
	SUPERHEAVY,  // Doubly-closed or extra-long (CVCC, CVVV, etc.)
};

// A single syllable in the Onset-Nucleus-Coda (ONC) model.
// Onset and Coda are optional consonant sequences; Nucleus is obligatory.
// Maximum attested sizes (cross-linguistic): onset 0-4, coda 0-4.
struct Syllable
{
	std::vector<Phoneme> onset;   // 0-4 consonants preceding the nucleus
	Phoneme              nucleus; // Obligatory vowel (or syllabic sonorant)
	std::vector<Phoneme> coda;    // 0-4 consonants following the nucleus

	explicit Syllable(const Phoneme& nuc) : nucleus(nuc) {}

	bool HasOnset() const { return !onset.empty(); }
	bool HasCoda()  const { return !coda.empty(); }

	// Total phoneme count in this syllable
	int Size() const { return (int)onset.size() + 1 + (int)coda.size(); }

	// Returns the abstract CV shape string (e.g. "CVC", "CCVCC")
	std::string GetShape() const
	{
		std::string s;
		for (size_t i = 0; i < onset.size(); ++i) s += 'C';
		s += 'V';
		for (size_t i = 0; i < coda.size(); ++i) s += 'C';
		return s;
	}

	// Returns phonological weight for prosodic/stress computation.
	// Heavy = closed (has coda); Superheavy = coda of 2+ consonants.
	SyllableWeight GetWeight() const
	{
		if (coda.size() >= 2) return SyllableWeight::SUPERHEAVY;
		if (!coda.empty())    return SyllableWeight::HEAVY;
		return SyllableWeight::LIGHT;
	}

	// Returns true if the rhyme (nucleus + coda) is branching — used in
	// metrical phonology to determine stress-bearing potential.
	bool IsBranching() const { return !coda.empty(); }

	// Returns the Rhyme portion (nucleus + coda) as a flat phoneme list.
	std::vector<Phoneme> GetRhyme() const
	{
		std::vector<Phoneme> rhyme;
		rhyme.push_back(nucleus);
		for (const Phoneme& p : coda) rhyme.push_back(p);
		return rhyme;
	}

	// Returns all phonemes in linear order (onset + nucleus + coda).
	std::vector<Phoneme> GetAll() const
	{
		std::vector<Phoneme> all;
		for (const Phoneme& p : onset)  all.push_back(p);
		all.push_back(nucleus);
		for (const Phoneme& p : coda)   all.push_back(p);
		return all;
	}
};

#endif // SYLLABLE_H
