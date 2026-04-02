#ifndef PHRASE_CONTOUR_H
#define PHRASE_CONTOUR_H

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

// ---------------------------------------------------------------------------
// PhraseType
// Intonational phrase boundary type, determined by terminal punctuation on
// the last word of a phrase.  Used by PhraseContour::Multiplier() to shape
// the boundary tone in the final 15% of the phrase window.
// ---------------------------------------------------------------------------
enum class PhraseType
{
    DECLARATIVE,    // ends in '.' or ';'  — falling boundary tone
    INTERROGATIVE,  // ends in '?'         — rising boundary tone
    EXCLAMATORY,    // ends in '!'         — elevated pitch throughout
    CONTINUATION,   // ends in ',' or ':'  — slight rise, suspends close
    NEUTRAL         // no terminal punct   — flat tail (treated as declaration)
};

// ---------------------------------------------------------------------------
// TaggedToken
// A single word stripped of punctuation, together with phrase-boundary
// information extracted from the trailing punctuation before stripping.
// ---------------------------------------------------------------------------
struct TaggedToken
{
    std::string word;         // lowercased, punctuation stripped
    bool        isBoundary;   // true when this token closes a phrase
    PhraseType  phraseType;   // type of the phrase that closes here
};

// ---------------------------------------------------------------------------
// PhraseContour
// Generates a smoothly-varying F0 multiplier for any normalised position
// within an intonational phrase using a simplified "hat" model:
//
//   Phase 1  [0, peakPos]     : initial high plateau falling to neutral 1.0
//   Phase 2  [peakPos, 0.85]  : linear declination from 1.0 to (1 - decl)
//   Phase 3  [0.85, 1.0]      : boundary tone — shape depends on PhraseType
//
// Multiplier() returns a scalar that is applied to baseF0 to produce the
// phrase-level F0 target.  IntonationModel then additionally multiplies by
// its per-syllable stress scale, so the two sources of pitch variation are
// composited independently.
// ---------------------------------------------------------------------------
class PhraseContour
{
public:
    struct Params
    {
        double initialBoost      = 0.08;   // fractional lift at phrase onset (phase 1 peak)
        double peakPosition      = 0.20;   // normalised phrase time where phase 1 ends
        double declinationRate   = 0.10;   // total fractional drop across phase 2 [peakPos, 0.85]
        double declarativeFall   = 0.08;   // additional drop in boundary zone (declarative)
        double interrogativeRise = 0.12;   // rise in boundary zone (interrogative)
        double exclamatoryBoost  = 0.12;   // uniform lift added to all phases (exclamatory)
        double continuationRise  = 0.04;   // gentle rise in boundary zone (continuation)
    };

    // Returns the phrase-level F0 multiplier for a syllable at normalised
    // phrase position t ∈ [0, 1].
    static double Multiplier(double t, PhraseType type, const Params& p)
    {
        t = (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0 : t;

        double base;

        if (t <= p.peakPosition)
        {
            // Phase 1: starts at (1 + initialBoost), declines to 1.0 at peakPos
            double frac = (p.peakPosition > 1e-6) ? (t / p.peakPosition) : 1.0;
            base = 1.0 + p.initialBoost * (1.0 - frac);
        }
        else if (t <= 0.85)
        {
            // Phase 2: linear declination from 1.0 to (1 - declinationRate)
            double span = 0.85 - p.peakPosition;
            double frac = (span > 1e-6) ? (t - p.peakPosition) / span : 1.0;
            base = 1.0 - p.declinationRate * frac;
        }
        else
        {
            // Phase 3: boundary tone
            double frac    = (t - 0.85) / 0.15;          // 0→1 across boundary zone
            double preDecl = 1.0 - p.declinationRate;    // level entering boundary zone

            switch (type)
            {
            case PhraseType::DECLARATIVE:
                base = preDecl - p.declarativeFall * frac;
                break;
            case PhraseType::INTERROGATIVE:
                base = preDecl + p.interrogativeRise * frac;
                break;
            case PhraseType::EXCLAMATORY:
                base = preDecl;  // holds; the uniform boost below handles the lift
                break;
            case PhraseType::CONTINUATION:
                base = preDecl + p.continuationRise * frac;
                break;
            default:  // NEUTRAL — flat tail
                base = preDecl;
                break;
            }
        }

        // Exclamatory phrases receive a uniform pitch lift across all phases
        if (type == PhraseType::EXCLAMATORY)
            base += p.exclamatoryBoost;

        return (base < 0.5) ? 0.5 : base;  // floor at 0.5 × baseF0
    }
};

// ---------------------------------------------------------------------------
// PhraseSegmenter
// Splits raw text into TaggedTokens, extracting phrase-boundary type from
// the trailing punctuation of each whitespace-delimited word.
//
// Rules:
//   trailing '.' or ';'  →  DECLARATIVE boundary
//   trailing '?'         →  INTERROGATIVE boundary
//   trailing '!'         →  EXCLAMATORY boundary
//   trailing ',' or ':'  →  CONTINUATION boundary
//   no relevant punct    →  not a boundary
//
// The very last token is always forced to isBoundary=true (DECLARATIVE if it
// would otherwise be NEUTRAL) so every phrase list is properly closed.
// Pure-digit tokens are expanded to their spoken word equivalents via
// NumberToWords() before being pushed into the result.
// ---------------------------------------------------------------------------
class PhraseSegmenter
{
public:
    // Converts a non-negative integer to its spoken English word string.
    // e.g. 42 → "forty two",  1001 → "one thousand one"
    static std::string NumberToWords(long long n)
    {
        if (n == 0) return "zero";

        static const char* kOnes[] = {
            "", "one", "two", "three", "four", "five", "six", "seven",
            "eight", "nine", "ten", "eleven", "twelve", "thirteen",
            "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
        };
        static const char* kTens[] = {
            "", "", "twenty", "thirty", "forty", "fifty",
            "sixty", "seventy", "eighty", "ninety"
        };

        std::string result;

        if (n >= 1000000000LL)
        {
            result += NumberToWords(n / 1000000000LL) + " billion";
            n %= 1000000000LL;
            if (n) result += " ";
        }
        if (n >= 1000000)
        {
            result += NumberToWords(n / 1000000) + " million";
            n %= 1000000;
            if (n) result += " ";
        }
        if (n >= 1000)
        {
            result += NumberToWords(n / 1000) + " thousand";
            n %= 1000;
            if (n) result += " ";
        }
        if (n >= 100)
        {
            result += std::string(kOnes[n / 100]) + " hundred";
            n %= 100;
            if (n) result += " ";
        }
        if (n >= 20)
        {
            result += kTens[n / 10];
            n %= 10;
            if (n) result += " ";
        }
        if (n > 0)
            result += kOnes[n];

        return result;
    }

    static std::vector<TaggedToken> Tokenize(const std::string& text)
    {
        static const std::string kStrip = ".,!?;:'\"()-_";

        std::vector<TaggedToken> result;
        std::istringstream iss(text);
        std::string raw;

        while (iss >> raw)
        {
            // Walk backwards through trailing characters to find boundary type
            // before stripping; stop at the first non-punctuation character.
            PhraseType type     = PhraseType::NEUTRAL;
            bool       boundary = false;

            for (int i = (int)raw.size() - 1; i >= 0; --i)
            {
                char c = raw[i];
                if (c == '.' || c == ';') { boundary = true; type = PhraseType::DECLARATIVE;   break; }
                if (c == '?')             { boundary = true; type = PhraseType::INTERROGATIVE; break; }
                if (c == '!')             { boundary = true; type = PhraseType::EXCLAMATORY;   break; }
                if (c == ',' || c == ':') { boundary = true; type = PhraseType::CONTINUATION;  break; }
                if (kStrip.find(c) == std::string::npos) break;  // non-punct — stop scanning
            }

            // Strip leading/trailing punctuation
            while (!raw.empty() && kStrip.find(raw.front()) != std::string::npos)
                raw.erase(raw.begin());
            while (!raw.empty() && kStrip.find(raw.back()) != std::string::npos)
                raw.pop_back();

            if (raw.empty()) continue;

            // Normalize internal hyphens so compound words typed as "on-line"
            // reach the dictionary as "online" — no gap between syllables.
            raw.erase(std::remove(raw.begin(), raw.end(), '-'), raw.end());
            if (raw.empty()) continue;

            // Lowercase
            for (char& c : raw) c = (char)std::tolower((unsigned char)c);

            // Expand pure-digit tokens to their spoken word equivalents.
            // Strings longer than 18 digits would overflow long long — skip them.
            if (std::all_of(raw.begin(), raw.end(), ::isdigit))
            {
                if (raw.size() <= 18)
                {
                    long long val = 0;
                    for (char c : raw) val = val * 10 + (c - '0');
                    std::string expanded = NumberToWords(val);
                    std::istringstream ws(expanded);
                    std::vector<std::string> numWords;
                    std::string nw;
                    while (ws >> nw) numWords.push_back(nw);
                    for (size_t i = 0; i < numWords.size(); ++i)
                    {
                        bool isLast = (i == numWords.size() - 1);
                        result.push_back({ numWords[i], isLast && boundary,
                                           isLast ? type : PhraseType::NEUTRAL });
                    }
                }
                continue;
            }

            result.push_back({ raw, boundary, type });
        }

        // Guarantee the final token closes a phrase
        if (!result.empty() && !result.back().isBoundary)
        {
            result.back().isBoundary = true;
            result.back().phraseType = PhraseType::DECLARATIVE;
        }

        return result;
    }
};

#endif // PHRASE_CONTOUR_H
