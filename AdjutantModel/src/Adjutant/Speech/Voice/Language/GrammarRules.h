#ifndef GRAMMAR_RULES_H
#define GRAMMAR_RULES_H

// ---------------------------------------------------------------------------
// GrammarRules
// Encodes the grammatical category system of a Language.
//
// caseCount           — number of morphological cases (English ≈ 2:
//                       nominative/oblique; Russian = 6; Finnish = 15).
// nounClassCount      — number of grammatical gender/noun-class distinctions
//                       (0 = none, 2 = masculine/feminine, 3 = m/f/neuter).
// hasGrammaticalGender — shorthand flag; true when nounClassCount > 0.
// hasTonality          — true for tonal languages where F0 encodes lexical
//                        or grammatical contrast (Mandarin, Yoruba, Vietnamese).
//                        When true, IntonationModel F0 targets must not
//                        override lexical tone assignments.
// hasAspect            — language grammaticalises verbal aspect (perfective /
//                        imperfective distinction — Slavic, Greek, Arabic).
// hasMood              — language grammaticalises verbal mood (indicative,
//                        subjunctive, imperative) through morphology.
// ---------------------------------------------------------------------------
struct GrammarRules
{
	int  caseCount            = 2;
	int  nounClassCount       = 0;
	bool hasGrammaticalGender = false;
	bool hasTonality          = false;  // tonal F0 is contrastive
	bool hasAspect            = true;
	bool hasMood              = true;
};

#endif // GRAMMAR_RULES_H
