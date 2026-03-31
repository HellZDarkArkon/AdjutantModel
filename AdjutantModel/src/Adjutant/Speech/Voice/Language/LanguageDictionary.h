#ifndef LANGUAGE_DICTIONARY_H
#define LANGUAGE_DICTIONARY_H

#include "../Vocalics/Phoneme.h"
#include "PartOfSpeech.h"
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// LanguageDictionary
// Maps orthographic words to their canonical phoneme sequences.
//
// Dat file format (one entry per line):
//   word - PHONEME_TYPE_A - PHONEME_TYPE_B - ...
//   Lines beginning with '#' are section headers or comments and are skipped.
//   The token delimiter is ' - ' (space-hyphen-space), consistent with the
//   rest of the pipeline dat format.
//
// Usage:
//   LanguageDictionary dict;
//   dict.Load("EnglishDict.dat");
//   std::vector<PhonemeType> phones;
//   if (dict.Lookup("cat", phones)) { ... }
// ---------------------------------------------------------------------------
class LanguageDictionary
{
public:
	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	// Parse a dictionary dat file.  Existing entries are preserved; duplicate
	// words are overwritten by later entries in the file.
	bool Load(const std::string& path)
	{
		std::ifstream f(path);
		if (!f.is_open()) return false;

		std::string line;
		while (std::getline(f, line))
		{
			// Trim leading whitespace
			line.erase(0, line.find_first_not_of(" \t\r\n"));

			// Skip section headers and blank lines
			if (line.empty() || line[0] == '#') continue;

			// Strip inline comments
			auto cpos = line.find('#');
			if (cpos != std::string::npos) line = line.substr(0, cpos);
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty()) continue;

			auto tokens = Tokenise(line);
			if (tokens.size() < 2) continue;

			// Check for a [TAG] annotation on the word token, e.g. "read[VERB_PAST]"
			std::string word = tokens[0];
			PartOfSpeech pos = PartOfSpeech::UNKNOWN;
			auto tagOpen  = word.find('[');
			if (tagOpen != std::string::npos)
			{
				auto tagClose = word.find(']', tagOpen);
				if (tagClose != std::string::npos)
				{
					pos  = PartOfSpeechFromString(word.substr(tagOpen + 1, tagClose - tagOpen - 1));
					word = word.substr(0, tagOpen);
				}
			}

			if (!mCaseSensitive)
			{
				std::transform(word.begin(), word.end(), word.begin(),
							   [](unsigned char c) { return (char)std::tolower(c); });
			}

			std::vector<PhonemeType> phonemes;
			phonemes.reserve(tokens.size() - 1);
			for (size_t i = 1; i < tokens.size(); ++i)
			{
				// Split on whitespace so composite tokens like "EY IH" or "AY IH"
				// are resolved as two separate phoneme lookups.
				std::istringstream ss(tokens[i]);
				std::string subtok;
				while (ss >> subtok)
				{
					PhonemeType pt;
					if (PhonemeTypeFromString(subtok, pt))
						phonemes.push_back(pt);
				}
			}
			if (!phonemes.empty())
			{
				if (pos != PartOfSpeech::UNKNOWN)
					mContextEntries[word][pos] = std::move(phonemes);
				else
					mEntries[word] = std::move(phonemes);
			}
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Lookup & mutation
	// -----------------------------------------------------------------------

	// Returns true and fills `out` using the context-specific entry when
	// `pos` is not UNKNOWN, falling back to the plain entry on a miss.
	bool LookupWithContext(const std::string& word, PartOfSpeech pos,
						   std::vector<PhonemeType>& out) const
	{
		if (pos != PartOfSpeech::UNKNOWN)
		{
			std::string key = word;
			if (!mCaseSensitive)
			{
				std::transform(key.begin(), key.end(), key.begin(),
							   [](unsigned char c) { return (char)std::tolower(c); });
			}
			auto cit = mContextEntries.find(key);
			if (cit != mContextEntries.end())
			{
				auto pit = cit->second.find(pos);
				if (pit != cit->second.end())
				{
					out = pit->second;
					return true;
				}
			}
		}
		return Lookup(word, out);
	}

	// Returns true and fills `out` if `word` is in the dictionary.
	bool Lookup(const std::string& word, std::vector<PhonemeType>& out) const
	{
		std::string key = word;
		if (!mCaseSensitive)
		{
			std::transform(key.begin(), key.end(), key.begin(),
			               [](unsigned char c) { return (char)std::tolower(c); });
		}
		auto it = mEntries.find(key);
		if (it == mEntries.end()) return false;
		out = it->second;
		return true;
	}

	void Insert(const std::string& word, const std::vector<PhonemeType>& phonemes)
	{
		std::string key = word;
		if (!mCaseSensitive)
		{
			std::transform(key.begin(), key.end(), key.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
		}
		mEntries[key] = phonemes;
	}

	void InsertContext(const std::string& word, PartOfSpeech pos,
					   const std::vector<PhonemeType>& phonemes)
	{
		std::string key = word;
		if (!mCaseSensitive)
		{
			std::transform(key.begin(), key.end(), key.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
		}
		mContextEntries[key][pos] = phonemes;
	}

	bool ContainsContext(const std::string& word, PartOfSpeech pos) const
	{
		std::vector<PhonemeType> dummy;
		return LookupWithContext(word, pos, dummy) &&
			   pos != PartOfSpeech::UNKNOWN;
	}

	bool    Contains(const std::string& word) const
	{
		std::vector<PhonemeType> dummy;
		return Lookup(word, dummy);
	}

	size_t  Size()          const { return mEntries.size(); }
	size_t  ContextSize()   const
	{
		size_t n = 0;
		for (auto& kv : mContextEntries) n += kv.second.size();
		return n;
	}
	bool    Empty()         const { return mEntries.empty(); }
	void    Clear()               { mEntries.clear(); mContextEntries.clear(); }

	void SetCaseSensitive(bool v) { mCaseSensitive = v; }

private:
	std::map<std::string, std::vector<PhonemeType>>                            mEntries;
	std::map<std::string, std::map<PartOfSpeech, std::vector<PhonemeType>>>   mContextEntries;
	bool mCaseSensitive = false;

	// -----------------------------------------------------------------------
	// Dat parsing helpers
	// -----------------------------------------------------------------------
	static std::vector<std::string> Tokenise(const std::string& line)
	{
		std::vector<std::string> tokens;
		std::string remaining = line;
		const std::string delim = " - ";
		size_t pos = 0;
		while ((pos = remaining.find(delim)) != std::string::npos)
		{
			std::string tok = remaining.substr(0, pos);
			tok.erase(tok.find_last_not_of(" \t") + 1);
			if (!tok.empty()) tokens.push_back(tok);
			remaining = remaining.substr(pos + delim.size());
		}
		remaining.erase(remaining.find_last_not_of(" \t\r\n") + 1);
		if (!remaining.empty()) tokens.push_back(remaining);
		return tokens;
	}

	public:
	// Resolves a token string to a PhonemeType.
	// Accepts both full enum-name strings (e.g. "PLOSIVE_ALVEOLAR_VOICELESS")
	// and short ARPAbet-style aliases used in .dict files (e.g. "T").
	// Returns false if the token is not recognised.
	static bool PhonemeTypeFromString(const std::string& name, PhonemeType& out)
	{
		static const std::map<std::string, PhonemeType> kTable = {
			// -----------------------------------------------------------------
			// Full enum names — vowels
			// -----------------------------------------------------------------
			{ "OPEN_FRONT_UNROUNDED",              PhonemeType::OPEN_FRONT_UNROUNDED              },
			{ "OPEN_FRONT_ROUNDED",                PhonemeType::OPEN_FRONT_ROUNDED                },
			{ "NEAR_OPEN_FRONT_UNROUNDED",         PhonemeType::NEAR_OPEN_FRONT_UNROUNDED         },
			{ "OPEN_MID_FRONT_UNROUNDED",          PhonemeType::OPEN_MID_FRONT_UNROUNDED          },
			{ "OPEN_MID_FRONT_ROUNDED",            PhonemeType::OPEN_MID_FRONT_ROUNDED            },
			{ "MID_FRONT_UNROUNDED",               PhonemeType::MID_FRONT_UNROUNDED               },
			{ "MID_FRONT_ROUNDED",                 PhonemeType::MID_FRONT_ROUNDED                 },
			{ "CLOSE_MID_FRONT_UNROUNDED",         PhonemeType::CLOSE_MID_FRONT_UNROUNDED         },
			{ "CLOSE_MID_FRONT_ROUNDED",           PhonemeType::CLOSE_MID_FRONT_ROUNDED           },
			{ "NEAR_CLOSE_NEAR_FRONT_UNROUNDED",   PhonemeType::NEAR_CLOSE_NEAR_FRONT_UNROUNDED   },
			{ "NEAR_CLOSE_NEAR_FRONT_ROUNDED",     PhonemeType::NEAR_CLOSE_NEAR_FRONT_ROUNDED     },
			{ "CLOSE_FRONT_UNROUNDED",             PhonemeType::CLOSE_FRONT_UNROUNDED             },
			{ "CLOSE_FRONT_ROUNDED",               PhonemeType::CLOSE_FRONT_ROUNDED               },
			{ "OPEN_CENTRAL_UNROUNDED",            PhonemeType::OPEN_CENTRAL_UNROUNDED            },
			{ "NEAR_OPEN_CENTRAL",                 PhonemeType::NEAR_OPEN_CENTRAL                 },
			{ "OPEN_MID_CENTRAL_UNROUNDED",        PhonemeType::OPEN_MID_CENTRAL_UNROUNDED        },
			{ "OPEN_MID_CENTRAL_ROUNDED",          PhonemeType::OPEN_MID_CENTRAL_ROUNDED          },
			{ "MID_CENTRAL",                       PhonemeType::MID_CENTRAL                       },
			{ "CLOSE_MID_CENTRAL_UNROUNDED",       PhonemeType::CLOSE_MID_CENTRAL_UNROUNDED       },
			{ "CLOSE_MID_CENTRAL_ROUNDED",         PhonemeType::CLOSE_MID_CENTRAL_ROUNDED         },
			{ "CLOSE_CENTRAL_UNROUNDED",           PhonemeType::CLOSE_CENTRAL_UNROUNDED           },
			{ "CLOSE_CENTRAL_ROUNDED",             PhonemeType::CLOSE_CENTRAL_ROUNDED             },
			{ "OPEN_BACK_UNROUNDED",               PhonemeType::OPEN_BACK_UNROUNDED               },
			{ "OPEN_BACK_ROUNDED",                 PhonemeType::OPEN_BACK_ROUNDED                 },
			{ "OPEN_MID_BACK_UNROUNDED",           PhonemeType::OPEN_MID_BACK_UNROUNDED           },
			{ "OPEN_MID_BACK_ROUNDED",             PhonemeType::OPEN_MID_BACK_ROUNDED             },
			{ "MID_BACK_UNROUNDED",                PhonemeType::MID_BACK_UNROUNDED                },
			{ "MID_BACK_ROUNDED",                  PhonemeType::MID_BACK_ROUNDED                  },
			{ "CLOSE_MID_BACK_UNROUNDED",          PhonemeType::CLOSE_MID_BACK_UNROUNDED          },
			{ "CLOSE_MID_BACK_ROUNDED",            PhonemeType::CLOSE_MID_BACK_ROUNDED            },
			{ "NEAR_CLOSE_NEAR_BACK_ROUNDED",      PhonemeType::NEAR_CLOSE_NEAR_BACK_ROUNDED      },
			{ "CLOSE_BACK_UNROUNDED",              PhonemeType::CLOSE_BACK_UNROUNDED              },
			{ "CLOSE_BACK_ROUNDED",                PhonemeType::CLOSE_BACK_ROUNDED                },
			// -----------------------------------------------------------------
			// Full enum names — consonants
			// -----------------------------------------------------------------
			{ "NASAL_BILABIAL",                    PhonemeType::NASAL_BILABIAL                    },
			{ "NASAL_LABIODENTAL",                 PhonemeType::NASAL_LABIODENTAL                 },
			{ "NASAL_ALVEOLAR",                    PhonemeType::NASAL_ALVEOLAR                    },
			{ "NASAL_RETROFLEX",                   PhonemeType::NASAL_RETROFLEX                   },
			{ "NASAL_PALATAL",                     PhonemeType::NASAL_PALATAL                     },
			{ "NASAL_VELAR",                       PhonemeType::NASAL_VELAR                       },
			{ "NASAL_UVULAR",                      PhonemeType::NASAL_UVULAR                      },
			{ "PLOSIVE_BILABIAL_VOICELESS",        PhonemeType::PLOSIVE_BILABIAL_VOICELESS        },
			{ "PLOSIVE_BILABIAL_VOICED",           PhonemeType::PLOSIVE_BILABIAL_VOICED           },
			{ "PLOSIVE_ALVEOLAR_VOICELESS",        PhonemeType::PLOSIVE_ALVEOLAR_VOICELESS        },
			{ "PLOSIVE_ALVEOLAR_VOICED",           PhonemeType::PLOSIVE_ALVEOLAR_VOICED           },
			{ "PLOSIVE_RETROFLEX_VOICELESS",       PhonemeType::PLOSIVE_RETROFLEX_VOICELESS       },
			{ "PLOSIVE_RETROFLEX_VOICED",          PhonemeType::PLOSIVE_RETROFLEX_VOICED          },
			{ "PLOSIVE_PALATAL_VOICELESS",         PhonemeType::PLOSIVE_PALATAL_VOICELESS         },
			{ "PLOSIVE_PALATAL_VOICED",            PhonemeType::PLOSIVE_PALATAL_VOICED            },
			{ "PLOSIVE_VELAR_VOICELESS",           PhonemeType::PLOSIVE_VELAR_VOICELESS           },
			{ "PLOSIVE_VELAR_VOICED",              PhonemeType::PLOSIVE_VELAR_VOICED              },
			{ "PLOSIVE_UVULAR_VOICELESS",          PhonemeType::PLOSIVE_UVULAR_VOICELESS          },
			{ "PLOSIVE_UVULAR_VOICED",             PhonemeType::PLOSIVE_UVULAR_VOICED             },
			{ "PLOSIVE_GLOTTAL",                   PhonemeType::PLOSIVE_GLOTTAL                   },
			{ "SIBAFF_ALVEOLAR_VOICELESS",         PhonemeType::SIBAFF_ALVEOLAR_VOICELESS         },
			{ "SIBAFF_ALVEOLAR_VOICED",            PhonemeType::SIBAFF_ALVEOLAR_VOICED            },
			{ "SIBAFF_POSTALVEOLAR_VOICELESS",     PhonemeType::SIBAFF_POSTALVEOLAR_VOICELESS     },
			{ "SIBAFF_POSTALVEOLAR_VOICED",        PhonemeType::SIBAFF_POSTALVEOLAR_VOICED        },
			{ "SIBAFF_RETROFLEX_VOICELESS",        PhonemeType::SIBAFF_RETROFLEX_VOICELESS        },
			{ "SIBAFF_RETROFLEX_VOICED",           PhonemeType::SIBAFF_RETROFLEX_VOICED           },
			{ "SIBAFF_PALATAL_VOICELESS",          PhonemeType::SIBAFF_PALATAL_VOICELESS          },
			{ "SIBAFF_PALATAL_VOICED",             PhonemeType::SIBAFF_PALATAL_VOICED             },
			{ "AFFRICATE_LABIODENTAL_VOICELESS",   PhonemeType::AFFRICATE_LABIODENTAL_VOICELESS   },
			{ "AFFRICATE_LABIODENTAL_VOICED",      PhonemeType::AFFRICATE_LABIODENTAL_VOICED      },
			{ "AFFRICATE_DENTAL_VOICELESS",        PhonemeType::AFFRICATE_DENTAL_VOICELESS        },
			{ "AFFRICATE_DENTAL_VOICED",           PhonemeType::AFFRICATE_DENTAL_VOICED           },
			{ "SIBFRIC_ALVEOLAR_VOICELESS",        PhonemeType::SIBFRIC_ALVEOLAR_VOICELESS        },
			{ "SIBFRIC_ALVEOLAR_VOICED",           PhonemeType::SIBFRIC_ALVEOLAR_VOICED           },
			{ "SIBFRIC_POSTALVEOLAR_VOICELESS",    PhonemeType::SIBFRIC_POSTALVEOLAR_VOICELESS    },
			{ "SIBFRIC_POSTALVEOLAR_VOICED",       PhonemeType::SIBFRIC_POSTALVEOLAR_VOICED       },
			{ "SIBFRIC_RETROFLEX_VOICELESS",       PhonemeType::SIBFRIC_RETROFLEX_VOICELESS       },
			{ "SIBFRIC_RETROFLEX_VOICED",          PhonemeType::SIBFRIC_RETROFLEX_VOICED          },
			{ "SIBFRIC_PALATAL_VOICELESS",         PhonemeType::SIBFRIC_PALATAL_VOICELESS         },
			{ "SIBFRIC_PALATAL_VOICED",            PhonemeType::SIBFRIC_PALATAL_VOICED            },
			{ "FRICATIVE_BILABIAL_VOICELESS",      PhonemeType::FRICATIVE_BILABIAL_VOICELESS      },
			{ "FRICATIVE_BILABIAL_VOICED",         PhonemeType::FRICATIVE_BILABIAL_VOICED         },
			{ "FRICATIVE_LABIODENTAL_VOICELESS",   PhonemeType::FRICATIVE_LABIODENTAL_VOICELESS   },
			{ "FRICATIVE_LABIODENTAL_VOICED",      PhonemeType::FRICATIVE_LABIODENTAL_VOICED      },
			{ "FRICATIVE_DENTAL_VOICELESS",        PhonemeType::FRICATIVE_DENTAL_VOICELESS        },
			{ "FRICATIVE_DENTAL_VOICED",           PhonemeType::FRICATIVE_DENTAL_VOICED           },
			{ "FRICATIVE_ALVEOLAR_VOICELESS",      PhonemeType::FRICATIVE_ALVEOLAR_VOICELESS      },
			{ "FRICATIVE_ALVEOLAR_VOICED",         PhonemeType::FRICATIVE_ALVEOLAR_VOICED         },
			{ "FRICATIVE_VELAR_VOICELESS",         PhonemeType::FRICATIVE_VELAR_VOICELESS         },
			{ "FRICATIVE_VELAR_VOICED",            PhonemeType::FRICATIVE_VELAR_VOICED            },
			{ "FRICATIVE_UVULAR_VOICELESS",        PhonemeType::FRICATIVE_UVULAR_VOICELESS        },
			{ "FRICATIVE_UVULAR_VOICED",           PhonemeType::FRICATIVE_UVULAR_VOICED           },
			{ "FRICATIVE_PHARYNGEAL_VOICELESS",    PhonemeType::FRICATIVE_PHARYNGEAL_VOICELESS    },
			{ "FRICATIVE_PHARYNGEAL_VOICED",       PhonemeType::FRICATIVE_PHARYNGEAL_VOICED       },
			{ "FRICATIVE_GLOTTAL_VOICELESS",       PhonemeType::FRICATIVE_GLOTTAL_VOICELESS       },
			{ "FRICATIVE_GLOTTAL_VOICED",          PhonemeType::FRICATIVE_GLOTTAL_VOICED          },
			{ "APPROX_LABIODENTAL",                PhonemeType::APPROX_LABIODENTAL                },
			{ "APPROX_ALVEOLAR",                   PhonemeType::APPROX_ALVEOLAR                   },
			{ "APPROX_RETROFLEX",                  PhonemeType::APPROX_RETROFLEX                  },
			{ "APPROX_PALATAL",                    PhonemeType::APPROX_PALATAL                    },
			{ "APPROX_VELAR",                      PhonemeType::APPROX_VELAR                      },
			{ "APPROX_LABIOVELAR",                 PhonemeType::APPROX_LABIOVELAR                 },
			{ "FLAP_LABIODENTAL",                  PhonemeType::FLAP_LABIODENTAL                  },
			{ "FLAP_ALVEOLAR",                     PhonemeType::FLAP_ALVEOLAR                     },
			{ "FLAP_RETROFLEX",                    PhonemeType::FLAP_RETROFLEX                    },
			{ "TRILL_BILABIAL",                    PhonemeType::TRILL_BILABIAL                    },
			{ "TRILL_ALVEOLAR",                    PhonemeType::TRILL_ALVEOLAR                    },
			{ "TRILL_UVULAR",                      PhonemeType::TRILL_UVULAR                      },
			{ "LATAFF_ALVEOLAR_VOICELESS",         PhonemeType::LATAFF_ALVEOLAR_VOICELESS         },
			{ "LATAFF_ALVEOLAR_VOICED",            PhonemeType::LATAFF_ALVEOLAR_VOICED            },
			{ "LATFRIC_ALVEOLAR_VOICELESS",        PhonemeType::LATFRIC_ALVEOLAR_VOICELESS        },
			{ "LATFRIC_ALVEOLAR_VOICED",           PhonemeType::LATFRIC_ALVEOLAR_VOICED           },
			{ "LATAPP_ALVEOLAR",                   PhonemeType::LATAPP_ALVEOLAR                   },
			{ "LATAPP_RETROFLEX",                  PhonemeType::LATAPP_RETROFLEX                  },
			{ "LATAPP_PALATAL",                    PhonemeType::LATAPP_PALATAL                    },
			{ "LATAPP_VELAR",                      PhonemeType::LATAPP_VELAR                      },
			{ "LATFLAP_ALVEOLAR",                  PhonemeType::LATFLAP_ALVEOLAR                  },
			{ "LATFLAP_RETROFLEX",                 PhonemeType::LATFLAP_RETROFLEX                 },
			// -----------------------------------------------------------------
			// Short ARPAbet-style aliases — used in .dict files for readability
			//
			// Vowels (American English)
			//   AE  æ   NEAR_OPEN_FRONT_UNROUNDED       cat, bad, that
			//   AA  ɑ   OPEN_BACK_UNROUNDED             father, hot, bother
			//   AH  ə   MID_CENTRAL                     about (unstressed), but (stressed ʌ — merged here)
			//   AO  ɔ   OPEN_MID_BACK_ROUNDED           thought, law, off
			//   EH  ɛ   OPEN_MID_FRONT_UNROUNDED        bed, set, pen
			//   ER  ɜ   OPEN_MID_CENTRAL_UNROUNDED      bird (rhotic; r follows separately)
			//   EY  e   CLOSE_MID_FRONT_UNROUNDED       day — 1st element; IH follows for full diphthong
			//   IH  ɪ   NEAR_CLOSE_NEAR_FRONT_UNROUNDED bit, sit, him
			//   IY  i   CLOSE_FRONT_UNROUNDED           beet, see, key
			//   OW  o   CLOSE_MID_BACK_ROUNDED          boat — 1st element; UH follows for full diphthong
			//   UH  ʊ   NEAR_CLOSE_NEAR_BACK_ROUNDED    book, put, foot
			//   UW  u   CLOSE_BACK_ROUNDED              boot, two, blue
			// -----------------------------------------------------------------
			{ "AE",  PhonemeType::NEAR_OPEN_FRONT_UNROUNDED    },
			{ "AA",  PhonemeType::OPEN_BACK_UNROUNDED           },
			{ "AH",  PhonemeType::MID_CENTRAL                   },
			{ "AO",  PhonemeType::OPEN_MID_BACK_ROUNDED         },
			{ "EH",  PhonemeType::OPEN_MID_FRONT_UNROUNDED      },
			{ "ER",  PhonemeType::OPEN_MID_CENTRAL_UNROUNDED    },
			{ "EY",  PhonemeType::CLOSE_MID_FRONT_UNROUNDED     },
			{ "IH",  PhonemeType::NEAR_CLOSE_NEAR_FRONT_UNROUNDED },
			{ "IY",  PhonemeType::CLOSE_FRONT_UNROUNDED         },
			{ "OW",  PhonemeType::CLOSE_MID_BACK_ROUNDED        },
			{ "UH",  PhonemeType::NEAR_CLOSE_NEAR_BACK_ROUNDED  },
			{ "UW",  PhonemeType::CLOSE_BACK_ROUNDED            },
			// -----------------------------------------------------------------
			// Short ARPAbet-style aliases — consonants
			//   P   p   PLOSIVE_BILABIAL_VOICELESS
			//   B   b   PLOSIVE_BILABIAL_VOICED
			//   T   t   PLOSIVE_ALVEOLAR_VOICELESS
			//   D   d   PLOSIVE_ALVEOLAR_VOICED
			//   K   k   PLOSIVE_VELAR_VOICELESS
			//   G   ɡ   PLOSIVE_VELAR_VOICED
			//   CH  tʃ  SIBAFF_POSTALVEOLAR_VOICELESS
			//   JH  dʒ  SIBAFF_POSTALVEOLAR_VOICED
			//   F   f   FRICATIVE_LABIODENTAL_VOICELESS
			//   V   v   FRICATIVE_LABIODENTAL_VOICED
			//   TH  θ   FRICATIVE_DENTAL_VOICELESS
			//   DH  ð   FRICATIVE_DENTAL_VOICED
			//   S   s   SIBFRIC_ALVEOLAR_VOICELESS
			//   Z   z   SIBFRIC_ALVEOLAR_VOICED
			//   SH  ʃ   SIBFRIC_POSTALVEOLAR_VOICELESS
			//   ZH  ʒ   SIBFRIC_POSTALVEOLAR_VOICED
			//   HH  h   FRICATIVE_GLOTTAL_VOICELESS
			//   M   m   NASAL_BILABIAL
			//   N   n   NASAL_ALVEOLAR
			//   NG  ŋ   NASAL_VELAR
			//   L   l   LATAPP_ALVEOLAR
			//   R   ɹ   APPROX_ALVEOLAR
			//   W   w   APPROX_LABIOVELAR
			//   Y   j   APPROX_PALATAL
			// -----------------------------------------------------------------
			{ "P",   PhonemeType::PLOSIVE_BILABIAL_VOICELESS     },
			{ "B",   PhonemeType::PLOSIVE_BILABIAL_VOICED         },
			{ "T",   PhonemeType::PLOSIVE_ALVEOLAR_VOICELESS      },
			{ "D",   PhonemeType::PLOSIVE_ALVEOLAR_VOICED         },
			{ "K",   PhonemeType::PLOSIVE_VELAR_VOICELESS         },
			{ "G",   PhonemeType::PLOSIVE_VELAR_VOICED            },
			{ "CH",  PhonemeType::SIBAFF_POSTALVEOLAR_VOICELESS   },
			{ "JH",  PhonemeType::SIBAFF_POSTALVEOLAR_VOICED      },
			{ "F",   PhonemeType::FRICATIVE_LABIODENTAL_VOICELESS },
			{ "V",   PhonemeType::FRICATIVE_LABIODENTAL_VOICED    },
			{ "TH",  PhonemeType::FRICATIVE_DENTAL_VOICELESS      },
			{ "DH",  PhonemeType::FRICATIVE_DENTAL_VOICED         },
			{ "S",   PhonemeType::SIBFRIC_ALVEOLAR_VOICELESS      },
			{ "Z",   PhonemeType::SIBFRIC_ALVEOLAR_VOICED         },
			{ "SH",  PhonemeType::SIBFRIC_POSTALVEOLAR_VOICELESS  },
			{ "ZH",  PhonemeType::SIBFRIC_POSTALVEOLAR_VOICED     },
			{ "HH",  PhonemeType::FRICATIVE_GLOTTAL_VOICELESS     },
			{ "M",   PhonemeType::NASAL_BILABIAL                  },
			{ "N",   PhonemeType::NASAL_ALVEOLAR                  },
			{ "NG",  PhonemeType::NASAL_VELAR                     },
			{ "L",   PhonemeType::LATAPP_ALVEOLAR                 },
			{ "R",   PhonemeType::APPROX_ALVEOLAR                 },
			{ "W",   PhonemeType::APPROX_LABIOVELAR               },
			{ "Y",   PhonemeType::APPROX_PALATAL                  },
			// Diphthong onset aliases — used when composite tokens are split
			{ "AY",  PhonemeType::OPEN_BACK_UNROUNDED             }, // /aɪ/ onset (sky, night, life)
			{ "AW",  PhonemeType::OPEN_BACK_UNROUNDED             }, // /aʊ/ onset (now, out, how)
		};
		auto it = kTable.find(name);
		if (it == kTable.end()) return false;
		out = it->second;
		return true;
	}
};

#endif // LANGUAGE_DICTIONARY_H
