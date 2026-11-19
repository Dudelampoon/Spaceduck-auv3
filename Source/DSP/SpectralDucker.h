// SpectralDucker.h — plain-C++ STFT spectral sidechain ducker core.
//
// Written fresh for the SpaceDuck AUv3. No JUCE dependency: this file (and its
// .cpp) compile standalone on any platform so the DSP can be unit-tested on
// Linux. The JUCE wrapper only feeds it audio and parameters.
//
// How it works:
//  - The main input and the sidechain (trigger) input each run through a
//    short-time Fourier transform (FFT 2048, Hann window, hop 512).
//  - The sidechain spectrum is summarised into 32 log-spaced bands from
//    40 Hz to 16 kHz. A band that is loud in the sidechain gets gain
//    reduction on the main signal, scaled by Amount.
//  - Per-band reduction is smoothed with attack/release envelopes, then
//    applied to the main spectrum. Inverse FFT + overlap-add reconstructs
//    the output.
//  - Only the frequencies the trigger occupies are ducked; the rest of the
//    main signal is untouched. That is the whole point vs. a normal
//    sidechain compressor, which turns the whole signal down.
//
// Latency: the STFT pipeline delays the signal by (fftSize - hop) samples
// = 1536 samples, ~32 ms at 48 kHz. Hosts compensate (the JUCE wrapper
// reports this via setLatencySamples).
#pragma once

#include <complex>
#include <deque>
#include <vector>

namespace spaceduck {

struct DuckerParams {
    float amount = 0.7f;        // 0..1 — how much of the computed reduction is applied
    float attackMs = 10.0f;     // 1..100 ms — how fast reduction engages per band
    float releaseMs = 150.0f;   // 50..500 ms — how fast reduction recovers per band
    float thresholdDb = -40.0f; // sidechain band level (dBFS) above which ducking starts
    float maxReductionDb = 24.0f; // cap on per-band reduction (Ceiling knob)
    float lowCutHz = 40.0f;     // detector low cut: bands below this are never ducked
    float highCutHz = 16000.0f; // detector high cut: bands above this are never ducked
    float mix = 1.0f;           // 0..1 — dry/wet blend (1 = fully ducked)
    float outputGainDb = 0.0f;  // -12..+12 dB output gain
    int monitor = 0;            // 0 off, 1 sidechain listen, 2 delta (removed signal)
    bool freeze = false;        // hold the current reduction curve
    int stereoMode = 0;         // 0 stereo, 1 mid only, 2 side only
};

class SpectralDucker {
public:
    static constexpr int kFftSize = 2048;
    static constexpr int kHop = 512;
    static constexpr int kNumBands = 32;
    static constexpr float kBandLowHz = 40.0f;
    static constexpr float kBandHighHz = 16000.0f;

    SpectralDucker();
    ~SpectralDucker();

    // numMainChannels: 1 or 2. The sidechain is downmixed to mono for analysis.
    void prepare(double sampleRate, int numMainChannels);
    void reset();
    void setParams(const DuckerParams& params);

    // Stream any block size. mainIn/scIn are arrays of channel pointers.
    // scIn may be nullptr (no trigger -> no ducking, clean passthrough).
    // out has the same channel count as mainIn.
    void process(const float* const* mainIn, int numMainChannels,
                 const float* const* scIn, int numScChannels,
                 float* const* out, int numSamples);

    // --- LINK mode support ---
    // MASTER instance (sits on the trigger track): analyse the MAIN input
    // for band levels and pass audio through unducked; the wrapper reads the
    // levels via getBandLevelsDb() and hands them to the SLAVE. A SLAVE
    // instance (sits on the ducked track) uses band levels measured by a
    // MASTER instead of analysing its own sidechain input.
    void setMasterMode(bool enabled);
    void setLinkedBandLevels(const float* levelDb, int count);
    void clearLinkedBandLevels();

    // --- test/inspection accessors ---
    int getLatencySamples() const { return kFftSize - kHop; }
    const std::vector<float>& getBandReductionsDb() const { return bandReductionDb_; }
    const std::vector<float>& getBandLevelsDb() const { return bandLevelDb_; }
    const std::vector<float>& getBandEdgesHz() const { return bandEdgesHz_; }
    float getSampleRate() const { return static_cast<float>(sampleRate_); }

private:
    void processFrame(); // runs one STFT hop when enough input is buffered

    // Small radix-2 FFT (forward unscaled, inverse scaled by 1/N).
    static void fft(std::vector<std::complex<float>>& data, bool inverse);

    double sampleRate_ = 48000.0;
    int numMainChannels_ = 2;

    DuckerParams params_;

    std::vector<float> window_; // Hann, length kFftSize

    // Input ring buffers (capacity 4096, enough for fftSize + hop + margin).
    std::vector<std::vector<float>> inputRing_; // [mainCh][ring] + last is sidechain mono
    int inputWritePos_ = 0;                     // monotonic logical write position
    int nextFrameEnd_ = kFftSize;               // logical position where the next frame window ends
    // First frame ends at kFftSize, then every kHop samples after that.

    // Overlap-add output buffers, one per main channel.
    std::vector<std::vector<float>> ola_;
    std::vector<std::deque<float>> outFifo_;

    std::vector<float> bandEdgesHz_;      // kNumBands + 1 edges
    std::vector<int> bandBinStart_;       // first FFT bin of each band
    std::vector<int> bandBinEnd_;         // one-past-last FFT bin of each band
    std::vector<float> bandLevelDb_;      // last measured sidechain band levels (dB)
    std::vector<float> bandReductionDb_;  // smoothed per-band reduction (dB)
    std::vector<float> bandGain_;         // per-band linear gain, recomputed per frame

    // LINK mode: when true, processFrame() uses linkedLevelDb_ instead of
    // analysing the sidechain input.
    bool useLinkedLevels_ = false;
    std::vector<float> linkedLevelDb_;
    // MASTER mode: analyse the main input for band levels, but apply no
    // gain reduction (audio passes through unducked).
    bool masterMode_ = false;

    std::vector<std::complex<float>> mainSpectrum_;  // reused per channel
    std::vector<std::complex<float>> mainSpectrum2_; // second main channel (M/S)
    std::vector<std::complex<float>> scSpectrum_;

    float attackCoeff_ = 0.5f; // per-hop smoothing coefficients
    float releaseCoeff_ = 0.1f;

    void updateCoefficients();
    void computeBandEdges();
};

} // namespace spaceduck
