#include "SpeechEngine.h"
#include "../AdjutantEngine.h"
#include "Voice/Vocalics/PhraseContour.h"
#include <sstream>
#include <cctype>
#include <cmath>
#include <algorithm>
#ifdef _DEBUG
#include <windows.h>
#endif

SpeechEngine::SpeechEngine()
{
	GetVoiceOutput().SelectVoice(1); // Select the second available voice (index 1) in the SAPI voice engine
}

SpeechEngine::~SpeechEngine()
{
	Clean(); // Clean up resources when the speech engine is destroyed
}

void SpeechEngine::Clean()
{
	queue.clear(); // Clear the dialogue queue
	pcmQueue.clear(); // Clear the PCM audio queue
}

void SpeechEngine::QueueLine(AdjutantEngine& adj, const std::string& line)
{
	queue.push_back(line); // Queue the line for CPU-side output via Framework::Update -> PopLine
}

void SpeechEngine::QueuePCM(const std::vector<int16_t>& pcmData)
{
	pcmQueue.insert(pcmQueue.end(), pcmData.begin(), pcmData.end()); // Add PCM data to the PCM queue
}

bool SpeechEngine::HasLine() const
{
	return !queue.empty(); // Check if the queue is not empty
}

std::string SpeechEngine::PopLine()
{
	if (queue.empty())
		return ""; // Return empty string if no lines are available

	std::string line = queue.front(); // Get the next line from the front of the queue
	queue.erase(queue.begin()); // Remove the line from the queue
	return ADJUTANT_TAG + line; // Return the line
}

// ---------------------------------------------------------------------------
// Language & speech synthesis
// ---------------------------------------------------------------------------

static std::vector<std::string> TokenizeText(const std::string& text)
{
	static const std::string kStrip = ".,!?;:'\"()-_";
	std::vector<std::string> tokens;
	std::istringstream iss(text);
	std::string word;
	while (iss >> word)
	{
		while (!word.empty() && kStrip.find(word.front()) != std::string::npos)
			word.erase(word.begin());
		while (!word.empty() && kStrip.find(word.back()) != std::string::npos)
			word.pop_back();
		if (!word.empty())
			tokens.push_back(word);
	}
	return tokens;
}

void SpeechEngine::LoadLanguage(const std::string& dictPath)
{
	mDict.SetCaseSensitive(false);
	mLanguageLoaded = mDict.Load(dictPath);

	IntonationModel::Params p;
	p.baseF0             = 220.0;  // female vocal range (Hz)
	p.primaryF0Scale     = 1.30;   // wider pitch excursions on stressed syllables
	p.secondaryF0Scale   = 1.12;
	p.unstressedF0Scale  = 0.88;
	p.declinationRate    = 0.025;  // slightly less pitch fall across the utterance
	mIntonationModel.ApplyParams(p);

	mFootParser.primaryPlacement = FootParser::PrimaryPlacement::LEFTMOST;

	MoraicGrid::Params mp;
	mp.baseMoraMs = 155.0;
	mMoraicGrid.ApplyParams(mp);
}

// Overlap-add crossfade append: blends the last `overlap` samples of dst with
// the first `overlap` samples of src using a raised-cosine window, then appends
// the remainder of src. Eliminates the amplitude dip that occurs when each
// phoneme carries its own independent attack/release envelope.
static void OLAAppend(std::vector<short>& dst, const std::vector<short>& src, int overlap)
{
	if (src.empty()) return;
	int actual = (std::min)(overlap, (int)(std::min)(dst.size(), src.size()));
	if (actual > 0)
	{
		int base = (int)dst.size() - actual;
		for (int i = 0; i < actual; ++i)
		{
			double t       = (actual > 1) ? ((double)i / (double)(actual - 1)) : 0.5;
			double fadeOut = 0.5 + 0.5 * std::cos(3.14159265358979323846 * t);
			double fadeIn  = 0.5 - 0.5 * std::cos(3.14159265358979323846 * t);
			double mixed   = dst[base + i] * fadeOut + src[i] * fadeIn;
			dst[base + i]  = static_cast<short>((std::max)(-32768.0, (std::min)(32767.0, mixed)));
		}
	}
	dst.insert(dst.end(), src.begin() + actual, src.end());
}

double SpeechEngine::SpeakLine(const std::string& text, VoiceOutputEngine& vo)
{
	if (!mLanguageLoaded || text.empty()) return 0.0;

#ifdef _DEBUG
	{
		std::string hdr = "[SPEECH] \"" + text + "\"\n";
		OutputDebugStringA(hdr.c_str());
	}
#endif

	int sr = mSynth.GetSampleRate();
	const int    kPhonemeOverlap = sr / 50;
	const double kWordGapSec     = 70.0 / 1000.0;
	auto wordGap    = BeepSynth::Silence(70, sr);
	auto unknownGap = BeepSynth::Silence(50, sr);

	// -----------------------------------------------------------------------
	// Pass 1 — analysis: phoneme lookup, syllabification, moraic timing.
	// Builds one WordData per recognised token and records utterance-absolute
	// onset times so the phrase contour can be computed across word boundaries.
	// -----------------------------------------------------------------------
	struct WordData
	{
		ProsodicWord             pw;
		std::vector<SonorityProfile> contours;
		double                   startSec;
		double                   durationSec;
		std::vector<double>      phraseF0Bases;  // one per syllable
	};

	auto tagged = PhraseSegmenter::Tokenize(text);
	std::vector<WordData> words;
	std::vector<int>      tagToWord(tagged.size(), -1);
	words.reserve(tagged.size());

	double cursor = 0.0;
	for (int ti = 0; ti < (int)tagged.size(); ++ti)
	{
		std::vector<PhonemeType> types;
		if (!mDict.Lookup(tagged[ti].word, types))
		{
#ifdef _DEBUG
			std::string miss = "[SPEECH]   MISS: \"" + tagged[ti].word + "\"\n";
			OutputDebugStringA(miss.c_str());
#endif
			cursor += unknownGap.size() / (double)sr;
			continue;
		}

#ifdef _DEBUG
		{
			std::ostringstream wh;
			wh << "[SPEECH] word=\"" << tagged[ti].word << "\" phonemes=" << types.size() << "  [";
			for (size_t i = 0; i < types.size(); ++i)
			{
				if (i) wh << ' ';
				wh << Phoneme::TypeName(types[i]);
			}
			wh << "]\n";
			OutputDebugStringA(wh.str().c_str());
		}
#endif

		std::vector<Phoneme> phonemes;
		phonemes.reserve(types.size());
		for (PhonemeType pt : types) phonemes.emplace_back(pt);

		auto syllables = mSyllableBuilder.Build(phonemes);
		auto pw        = mFootParser.Parse(syllables);
		mMoraicGrid.Compute(pw);

		double dur    = mMoraicGrid.TotalDuration();
		auto flatSyls = pw.FlatSyllables();

		std::vector<SonorityProfile> contours;
		contours.reserve(flatSyls.size());
		for (auto& [syl, stress] : flatSyls)
			contours.push_back(SonorityContour::Compute(syl));

		WordData wd;
		wd.pw          = std::move(pw);
		wd.contours    = std::move(contours);
		wd.startSec    = cursor;
		wd.durationSec = dur;
		wd.phraseF0Bases.assign(flatSyls.size(), mIntonationModel.GetBaseF0());

		tagToWord[ti] = (int)words.size();
		words.push_back(std::move(wd));
		cursor += dur + kWordGapSec;
	}

	// -----------------------------------------------------------------------
	// Phrase segmentation + contour computation.
	// Group consecutive words into intonational phrases using the boundary
	// tags from PhraseSegmenter, then compute a phrase-level F0 multiplier
	// for every syllable based on its normalised position within the phrase.
	// -----------------------------------------------------------------------
	{
		struct PhraseSpan { std::vector<int> wordIndices; PhraseType type; };
		std::vector<PhraseSpan> phrases;
		PhraseSpan current;

		for (int ti = 0; ti < (int)tagged.size(); ++ti)
		{
			if (tagToWord[ti] >= 0)
				current.wordIndices.push_back(tagToWord[ti]);
			if (tagged[ti].isBoundary)
			{
				current.type = tagged[ti].phraseType;
				if (!current.wordIndices.empty())
					phrases.push_back(current);
				current = {};
			}
		}
		if (!current.wordIndices.empty())
		{
			current.type = PhraseType::DECLARATIVE;
			phrases.push_back(current);
		}

		PhraseContour::Params cp;
		for (auto& phrase : phrases)
		{
			if (phrase.wordIndices.empty()) continue;

			double phraseStart = words[phrase.wordIndices.front()].startSec;
			double phraseEnd   = words[phrase.wordIndices.back()].startSec
							   + words[phrase.wordIndices.back()].durationSec;
			double phraseDur   = phraseEnd - phraseStart;
			if (phraseDur < 1e-6) phraseDur = 1e-6;

				std::vector<StressLevel>          phraseStress;
				std::vector<double>               phraseTimes;
				std::vector<std::pair<int, int>>  phraseSylIds;

				for (int wi : phrase.wordIndices)
				{
					mMoraicGrid.Compute(words[wi].pw);
					const auto& timings = mMoraicGrid.Syllables();
					auto flatSyls       = words[wi].pw.FlatSyllables();

					for (int si = 0; si < (int)words[wi].phraseF0Bases.size(); ++si)
					{
						double sylMid = words[wi].startSec
							+ (si < (int)timings.size()
								? timings[si].startSeconds + timings[si].durationSeconds * 0.5
								: words[wi].durationSec * 0.5);
						double tPhrase = (sylMid - phraseStart) / phraseDur;
						words[wi].phraseF0Bases[si] =
							mIntonationModel.GetBaseF0()
							* PhraseContour::Multiplier(tPhrase, phrase.type, cp);

						phraseStress.push_back(si < (int)flatSyls.size()
							? flatSyls[si].second : StressLevel::UNSTRESSED);
						phraseTimes.push_back(tPhrase);
						phraseSylIds.emplace_back(wi, si);
					}
				}

				// Apply pitch accent coarticulation: consecutive accented syllables
				// modify each other's F0 target according to the rule table.
				{
					std::vector<double> flatF0;
					flatF0.reserve(phraseSylIds.size());
					for (auto& [wi, si] : phraseSylIds)
						flatF0.push_back(words[wi].phraseF0Bases[si]);

					mPitchAccentModel.Apply(phraseStress, phraseTimes, phrase.type, flatF0);

					for (int i = 0; i < (int)phraseSylIds.size(); ++i)
						words[phraseSylIds[i].first].phraseF0Bases[phraseSylIds[i].second] = flatF0[i];
				}
			}
	}

	// -----------------------------------------------------------------------
	// Pass 2 — synthesis.
	// Restore moraic timings per word, call IntonationModel with the per-
	// syllable phrase F0 bases computed above, then OLA-append each word.
	// -----------------------------------------------------------------------
	std::vector<short> pcm;
	for (int ti = 0; ti < (int)tagged.size(); ++ti)
	{
		int wi = tagToWord[ti];
		if (wi < 0)
		{
			pcm.insert(pcm.end(), unknownGap.begin(), unknownGap.end());
			if (tagged[ti].isBoundary)
			{
				int pauseMs = (tagged[ti].phraseType == PhraseType::DECLARATIVE)   ? 220
							: (tagged[ti].phraseType == PhraseType::INTERROGATIVE) ? 180
							: (tagged[ti].phraseType == PhraseType::EXCLAMATORY)   ? 160
							: (tagged[ti].phraseType == PhraseType::CONTINUATION)  ? 120
							: 80;
				auto phraseGap = BeepSynth::Silence(pauseMs, sr);
				pcm.insert(pcm.end(), phraseGap.begin(), phraseGap.end());
			}
			continue;
		}

		WordData& wd = words[wi];
		mMoraicGrid.Compute(wd.pw);

		auto flatSyls = wd.pw.FlatSyllables();
		auto params   = mIntonationModel.Compute(wd.pw, mMoraicGrid,
												 wd.contours, wd.phraseF0Bases);

		std::vector<short> wordBuf;
		mSynth.BeginWord();
		for (int si = 0; si < (int)flatSyls.size(); ++si)
		{
			auto allPhonemes      = flatSyls[si].first.GetAll();
			const auto& sylParams = params[si];
			for (int pi = 0; pi < (int)allPhonemes.size() && pi < (int)sylParams.size(); ++pi)
			{
				mSynth.SetSuppressEnvelopes(true, true);
				auto samples = mSynth.Generate(allPhonemes[pi], sylParams[pi]);
				OLAAppend(wordBuf, samples, kPhonemeOverlap);

#ifdef _DEBUG
				{
					double ss = 0.0, peak = 0.0;
					for (short s : samples)
					{
						double n = s / 32767.0;
						ss   += n * n;
						if (n < 0) n = -n;
						if (n > peak) peak = n;
					}
					double rms = samples.empty() ? 0.0 : std::sqrt(ss / samples.size());
					const Phoneme& ph = allPhonemes[pi];
					ConsonantManner manner = ph.GetManner();
					std::ostringstream ph_line;
					ph_line.precision(4);
					ph_line << "[SPEECH]   ["
							<< Phoneme::MannerName(manner) << "] "
							<< Phoneme::TypeName(ph.GetType())
							<< "  dur=" << (int)(sylParams[pi].durationSeconds * 1000.0) << "ms"
							<< "  f0=" << (int)sylParams[pi].f0Start << "->" << (int)sylParams[pi].f0End << "Hz"
							<< "  amp=" << sylParams[pi].amplitudeScale
							<< "  rms=" << rms
							<< "  peak=" << peak
							<< "\n";
					OutputDebugStringA(ph_line.str().c_str());
				}
#endif
			}
		}

		if (pcm.empty())
			pcm = wordBuf;
		else
			OLAAppend(pcm, wordBuf, kPhonemeOverlap);
		pcm.insert(pcm.end(), wordGap.begin(), wordGap.end());
		if (tagged[ti].isBoundary)
		{
			int pauseMs = (tagged[ti].phraseType == PhraseType::DECLARATIVE)   ? 220
						: (tagged[ti].phraseType == PhraseType::INTERROGATIVE) ? 180
						: (tagged[ti].phraseType == PhraseType::EXCLAMATORY)   ? 160
						: (tagged[ti].phraseType == PhraseType::CONTINUATION)  ? 120
						: 80;
			auto phraseGap = BeepSynth::Silence(pauseMs, sr);
			pcm.insert(pcm.end(), phraseGap.begin(), phraseGap.end());
		}
	}

	if (!pcm.empty())
	{
		int atkSamp = (std::min)((int)(sr * 0.020), (int)pcm.size());
		for (int i = 0; i < atkSamp; ++i)
			pcm[i] = static_cast<short>(pcm[i] * i / atkSamp);
		int relSamp = (std::min)((int)(sr * 0.030), (int)pcm.size());
		for (int i = (int)pcm.size() - relSamp; i < (int)pcm.size(); ++i)
			pcm[i] = static_cast<short>(pcm[i] * ((int)pcm.size() - i) / relSamp);
		vo.PlayPCM(pcm.data(), pcm.size());
	}

	return (double)pcm.size() / mSynth.GetSampleRate();
}

double SpeechEngine::PlayBeep(BeepType type, VoiceOutputEngine& vo)
{
	auto pcm = BeepSynth::Generate(type, mSynth.GetSampleRate());
	vo.PlayPCM(pcm.data(), pcm.size());
	return (double)pcm.size() / mSynth.GetSampleRate();
}