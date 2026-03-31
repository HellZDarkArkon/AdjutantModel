#ifndef SYNTAX_RULES_H
#define SYNTAX_RULES_H

// ---------------------------------------------------------------------------
// SyntaxRules
// Encodes the canonical phrase-structure and constituent-order properties
// of a Language.
//
// WordOrder    — dominant order of Subject, Verb and Object in declarative
//               clauses (SOV most common cross-linguistically; SVO for English).
// headInitial  — true when phrasal heads precede their complements
//               (English: "red car" = head-initial NP; Japanese is head-final).
// proDropAllowed — subject pronoun can be omitted when recoverable from
//               context (Spanish, Italian, Japanese).
// verbSecond   — main verb must occupy the second constituent position in
//               root clauses (German, Dutch, Icelandic).
// ---------------------------------------------------------------------------
enum class WordOrder
{
	SVO,  // Subject – Verb – Object  (English, French, Swahili)
	SOV,  // Subject – Object – Verb  (Japanese, Turkish, Latin)
	VSO,  // Verb – Subject – Object  (Classical Arabic, Welsh, Irish)
	VOS,  // Verb – Object – Subject  (Malagasy)
	OVS,  // Object – Verb – Subject  (Hixkaryana)
	OSV,  // Object – Subject – Verb  (Warao)
	FREE  // No dominant order; pragmatically driven (Latin prose, Russian)
};

struct SyntaxRules
{
	WordOrder wordOrder      = WordOrder::SVO;
	bool      headInitial    = true;    // head before complement
	bool      proDropAllowed = false;   // null subject pronoun permitted
	bool      verbSecond     = false;   // V2 root-clause constraint
};

#endif // SYNTAX_RULES_H
