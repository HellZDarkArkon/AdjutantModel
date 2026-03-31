#include "SpeechEngine.h"
#include "../AdjutantEngine.h"
#include <sstream>
#include <cctype>
#ifdef _DEBUG
#include <windows.h>
#include <cmath>
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
	auto wordGap    = BeepSynth::Silence(30, sr);
	auto unknownGap = BeepSynth::Silence(50, sr);

	std::vector<short> pcm;
	auto tokens = TokenizeText(text);

	for (const auto& token : tokens)
	{
		if (std::all_of(token.begin(), token.end(), ::isdigit)) continue;

		std::vector<PhonemeType> types;
		if (!mDict.Lookup(token, types))
		{
#ifdef _DEBUG
			std::string miss = "[SPEECH]   MISS: \"" + token + "\"\n";
			OutputDebugStringA(miss.c_str());
#endif
			pcm.insert(pcm.end(), unknownGap.begin(), unknownGap.end());
			continue;
		}

#ifdef _DEBUG
		{
			std::ostringstream wh;
			wh << "[SPEECH] word=\"" << token << "\" phonemes=" << types.size() << "  [";
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
		for (PhonemeType pt : types)
			phonemes.emplace_back(pt);

		auto syllables = mSyllableBuilder.Build(phonemes);
		auto pw        = mFootParser.Parse(syllables);
		mMoraicGrid.Compute(pw);

		auto flatSyls = pw.FlatSyllables();
		std::vector<SonorityProfile> contours;
		contours.reserve(flatSyls.size());
		for (auto& [syl, stress] : flatSyls)
			contours.push_back(SonorityContour::Compute(syl));

		auto params = mIntonationModel.Compute(pw, mMoraicGrid, contours);

		mSynth.BeginWord();
		for (int si = 0; si < (int)flatSyls.size(); ++si)
		{
			auto allPhonemes      = flatSyls[si].first.GetAll();
			const auto& sylParams = params[si];
			for (int pi = 0; pi < (int)allPhonemes.size() && pi < (int)sylParams.size(); ++pi)
			{
				auto samples = mSynth.Generate(allPhonemes[pi], sylParams[pi]);
				pcm.insert(pcm.end(), samples.begin(), samples.end());

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

		pcm.insert(pcm.end(), wordGap.begin(), wordGap.end());
	}

	if (!pcm.empty())
		vo.PlayPCM(pcm.data(), pcm.size());

	return (double)pcm.size() / mSynth.GetSampleRate();
}

double SpeechEngine::PlayBeep(BeepType type, VoiceOutputEngine& vo)
{
	auto pcm = BeepSynth::Generate(type, mSynth.GetSampleRate());
	vo.PlayPCM(pcm.data(), pcm.size());
	return (double)pcm.size() / mSynth.GetSampleRate();
}