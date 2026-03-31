#ifndef GLIDES_H
#define GLIDES_H

#include "Phoneme.h"
#include <vector>
#include <iostream>

class Glides
{
public:
	Glides(int cnt = 1) : mCnt(cnt) 
	{
		mSequence.reserve(cnt); // Reserve space for the specified number of phonemes in the glide sequence to optimize memory allocation
	}

	~Glides() = default; // Default destructor (no special cleanup needed)

	void AddPhoneme(const Phoneme& p) 
	{
		mSequence.push_back(p); // Add a phoneme to the glide sequence
	}

	const Phoneme& operator[](int index) const
	{
		return mSequence[index]; // Access a phoneme in the glide sequence by index (const version)
	}

	bool isComplete() const
	{
		return mSequence.size() == mCnt; // Check if the glide sequence is complete by comparing the current size of the sequence to the expected count of phonemes
	}

private:
	int mCnt; // Number of phonemes in the glide sequence (e.g., 1 for a monophthong, 2 for a diphthong or consonant-vowel glide, etc.)
	std::vector<Phoneme> mSequence; // Sequence of phonemes that make up the glide (e.g., for a diphthong, this would contain the two phonemes that transition from one to the other)
	
public:
	int GetCount() const { return mCnt; } // Accessor for the count of phonemes in the glide sequence
	const std::vector<Phoneme>& GetSequence() const { return mSequence; } // Accessor for the sequence of phonemes in the glide

	void SetCount(int cnt) { mCnt = cnt; } // Mutator for the count of phonemes in the glide sequence
	
};

#endif // GLIDES_H