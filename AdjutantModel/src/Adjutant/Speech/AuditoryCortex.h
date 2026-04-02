#ifndef AUDITORY_CORTEX_H
#define AUDITORY_CORTEX_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <mmdeviceapi.h>
#include <Audioclient.h>

// Captures the default microphone stream via WASAPI.
// Maintains a rolling mono int16 ring buffer and tracks a smoothed
// RMS amplitude level for downstream auditory processing.
//
// Owned by AdjutantEngine; call Init() once, then Start() to begin capture.
// Mirror of the VoiceOutputEngine render path on the input side.
class AuditoryCortex
{
public:
	AuditoryCortex();
	~AuditoryCortex();

	AuditoryCortex(const AuditoryCortex&)            = delete;
	AuditoryCortex& operator=(const AuditoryCortex&) = delete;

	bool Init();  // Open the default WASAPI capture endpoint; returns false on failure
	void Start(); // Start the capture thread
	void Stop();  // Signal the capture thread to stop and join it
	void Clean(); // Release all COM resources

	float GetRMSLevel()  const { return mRMSLevel.load();  } // Smoothed amplitude [0,1]
	bool  IsCapturing()  const { return mRunning.load();   } // Whether the capture thread is active
	int   GetSampleRate() const { return mSampleRate;       } // Native device sample rate (Hz)

	// Copy the most-recent mono int16 samples (up to maxSamples) into dst.
	// Returns the number of samples actually copied.
	size_t PeekPCM(int16_t* dst, size_t maxSamples) const;

private:
	void CaptureLoop(); // Runs on mCaptureThread

	IMMDevice*           mDevice        = nullptr;
	IAudioClient*        mAudioClient   = nullptr;
	IAudioCaptureClient* mCaptureClient = nullptr;

	int    mSampleRate    = 0;
	UINT32 mNumChannels   = 0;
	WORD   mBitsPerSample = 0; // 32 = IEEE float (WASAPI shared default), 16 = PCM int

	std::thread        mCaptureThread;
	std::atomic<bool>  mRunning  { false };
	std::atomic<float> mRMSLevel { 0.0f  };

	mutable std::mutex   mBufMutex;
	std::vector<int16_t> mRingBuffer;
	size_t               mWriteHead = 0;

	// Ring-buffer capacity: ~4 s at 48 kHz mono (worst-case native rate)
	static constexpr size_t kRingCapacity = 48000 * 4;
};

#endif // AUDITORY_CORTEX_H
