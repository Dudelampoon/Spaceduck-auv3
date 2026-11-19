// SpectralDucker.cpp — STFT spectral sidechain ducker core.
// See SpectralDucker.h for the design. Written fresh; no JUCE dependency.

#include "SpectralDucker.h"

#include <algorithm>
#include <cmath>

namespace spaceduck {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-12f;

// Hann window, length N.
std::vector<float> makeHannWindow(int n) {
    std::vector<float> w(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        w[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n)));
    return w;
}

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

SpectralDucker::SpectralDucker()
    : window_(makeHannWindow(kFftSize)) {
    computeBandEdges();
}

SpectralDucker::~SpectralDucker() = default;

void SpectralDucker::computeBandEdges() {
    bandEdgesHz_.resize(static_cast<size_t>(kNumBands) + 1);
    const float ratio = std::pow(kBandHighHz / kBandLowHz, 1.0f / static_cast<float>(kNumBands));
    for (int i = 0; i <= kNumBands; ++i)
        bandEdgesHz_[static_cast<size_t>(i)] = kBandLowHz * std::pow(ratio, static_cast<float>(i));

    bandBinStart_.assign(static_cast<size_t>(kNumBands), 0);
    bandBinEnd_.assign(static_cast<size_t>(kNumBands), 0);
    // Filled in prepare() once the sample rate is known (bins depend on it).
    bandLevelDb_.assign(static_cast<size_t>(kNumBands), -120.0f);
    bandReductionDb_.assign(static_cast<size_t>(kNumBands), 0.0f);
    bandGain_.assign(static_cast<size_t>(kNumBands), 1.0f);
    linkedLevelDb_.assign(static_cast<size_t>(kNumBands), -120.0f);
}

void SpectralDucker::prepare(double sampleRate, int numMainChannels) {
    sampleRate_ = sampleRate;
    numMainChannels_ = std::max(1, std::min(2, numMainChannels));

    // Map band edges to FFT bins (only the positive-frequency half is used;
    // the spectrum is mirrored when gains are applied).
    const int numBins = kFftSize / 2 + 1;
    for (int b = 0; b < kNumBands; ++b) {
        const float f0 = bandEdgesHz_[static_cast<size_t>(b)];
        const float f1 = bandEdgesHz_[static_cast<size_t>(b) + 1];
        int bin0 = static_cast<int>(std::floor(f0 * kFftSize / sampleRate_));
        int bin1 = static_cast<int>(std::ceil(f1 * kFftSize / sampleRate_));
        bin0 = std::max(1, std::min(numBins - 1, bin0)); // skip DC
        bin1 = std::max(bin0 + 1, std::min(numBins, bin1));
        bandBinStart_[static_cast<size_t>(b)] = bin0;
        bandBinEnd_[static_cast<size_t>(b)] = bin1;
    }

    // Ring buffers: one per main channel + one mono sidechain.
    // Capacity 8192: the dry-delay read in process() lags the write
    // position by up to latency + one host block, so the ring must hold
    // more than fftSize + hop + the largest expected block.
    constexpr int kRingCapacity = 8192;
    inputRing_.assign(static_cast<size_t>(numMainChannels_ + 1),
                      std::vector<float>(static_cast<size_t>(kRingCapacity), 0.0f));
    inputWritePos_ = 0;
    nextFrameEnd_ = kFftSize;

    ola_.assign(static_cast<size_t>(numMainChannels_),
                std::vector<float>(static_cast<size_t>(kFftSize), 0.0f));
    outFifo_.assign(static_cast<size_t>(numMainChannels_), std::deque<float>());

    mainSpectrum_.assign(static_cast<size_t>(kFftSize), {0.0f, 0.0f});
    mainSpectrum2_.assign(static_cast<size_t>(kFftSize), {0.0f, 0.0f});
    scSpectrum_.assign(static_cast<size_t>(kFftSize), {0.0f, 0.0f});

    updateCoefficients();
    reset();
}

void SpectralDucker::reset() {
    for (auto& ring : inputRing_)
        std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& buf : ola_)
        std::fill(buf.begin(), buf.end(), 0.0f);
    for (auto& fifo : outFifo_)
        fifo.clear();
    std::fill(bandLevelDb_.begin(), bandLevelDb_.end(), -120.0f);
    std::fill(bandReductionDb_.begin(), bandReductionDb_.end(), 0.0f);
    std::fill(bandGain_.begin(), bandGain_.end(), 1.0f);
    inputWritePos_ = 0;
    nextFrameEnd_ = kFftSize;
}

void SpectralDucker::setMasterMode(bool enabled) {
    masterMode_ = enabled;
}

void SpectralDucker::setLinkedBandLevels(const float* levelDb, int count) {
    if (levelDb == nullptr || count < kNumBands)
        return;
    std::copy(levelDb, levelDb + kNumBands, linkedLevelDb_.begin());
    useLinkedLevels_ = true;
}

void SpectralDucker::clearLinkedBandLevels() {
    useLinkedLevels_ = false;
}

void SpectralDucker::updateCoefficients() {
    const float hopSeconds = static_cast<float>(kHop) / static_cast<float>(sampleRate_);
    const float attack = std::max(0.001f, params_.attackMs / 1000.0f);
    const float release = std::max(0.001f, params_.releaseMs / 1000.0f);
    attackCoeff_ = 1.0f - std::exp(-hopSeconds / attack);
    releaseCoeff_ = 1.0f - std::exp(-hopSeconds / release);
}

void SpectralDucker::setParams(const DuckerParams& params) {
    params_.amount = std::max(0.0f, std::min(1.0f, params.amount));
    params_.attackMs = std::max(1.0f, std::min(100.0f, params.attackMs));
    params_.releaseMs = std::max(50.0f, std::min(500.0f, params.releaseMs));
    params_.thresholdDb = params.thresholdDb;
    params_.maxReductionDb = std::max(0.0f, std::min(48.0f, params.maxReductionDb));
    // Detector cut range: clamp each end to the band span, then keep them
    // ordered (a low cut above the high cut just swaps them) so the range
    // is always valid.
    const float lo = std::max(kBandLowHz, std::min(8000.0f, params.lowCutHz));
    const float hi = std::max(100.0f, std::min(kBandHighHz, params.highCutHz));
    params_.lowCutHz = std::min(lo, hi);
    params_.highCutHz = std::max(lo, hi);
    params_.mix = std::max(0.0f, std::min(1.0f, params.mix));
    params_.outputGainDb = std::max(-12.0f, std::min(12.0f, params.outputGainDb));
    params_.monitor = std::max(0, std::min(2, params.monitor));
    params_.freeze = params.freeze;
    params_.stereoMode = std::max(0, std::min(2, params.stereoMode));
    updateCoefficients();
}

// Iterative radix-2 FFT. Forward is unscaled; inverse scales by 1/N.
void SpectralDucker::fft(std::vector<std::complex<float>>& data, bool inverse) {
    const int n = static_cast<int>(data.size());
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
    }
    // Cooley-Tukey butterflies.
    for (int len = 2; len <= n; len <<= 1) {
        const float angle = (inverse ? 2.0f : -2.0f) * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = data[static_cast<size_t>(i + j)];
                const std::complex<float> v = data[static_cast<size_t>(i + j + len / 2)] * w;
                data[static_cast<size_t>(i + j)] = u + v;
                data[static_cast<size_t>(i + j + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(n);
        for (auto& x : data)
            x *= scale;
    }
}

void SpectralDucker::process(const float* const* mainIn, int numMainChannels,
                             const float* const* scIn, int numScChannels,
                             float* const* out, int numSamples) {
    const int nCh = std::max(1, std::min(numMainChannels_, numMainChannels));
    const int ringCap = static_cast<int>(inputRing_[0].size());

    // Push incoming samples into the ring buffers (always, even in listen
    // mode, so the STFT state stays fresh if listen is toggled off).
    for (int n = 0; n < numSamples; ++n) {
        const int pos = (inputWritePos_ + n) % ringCap;
        for (int ch = 0; ch < nCh; ++ch)
            inputRing_[static_cast<size_t>(ch)][static_cast<size_t>(pos)] = mainIn[ch][n];
        float scMix = 0.0f;
        if (scIn != nullptr && numScChannels > 0) {
            for (int c = 0; c < numScChannels; ++c)
                scMix += scIn[c][n];
            scMix /= static_cast<float>(numScChannels);
        }
        inputRing_[static_cast<size_t>(nCh)][static_cast<size_t>(pos)] = scMix;
    }
    inputWritePos_ += numSamples;

    if (params_.monitor == 1 && scIn != nullptr && numScChannels > 0) {
        // Monitor mode: sidechain straight through (mono mix to all channels).
        // Latency is not compensated here; this is a listening aid only.
        for (int n = 0; n < numSamples; ++n) {
            float scMix = 0.0f;
            for (int c = 0; c < numScChannels; ++c)
                scMix += scIn[c][n];
            scMix /= static_cast<float>(numScChannels);
            for (int ch = 0; ch < nCh; ++ch)
                out[ch][n] = scMix;
        }
        return;
    }

    // Run one STFT frame per hop of buffered input. The first frame ends at
    // sample kFftSize; after that a frame ends every kHop samples.
    // Guard: if a host hands us an absurdly large block at once, the ring
    // only holds the last 4096 samples, so fast-forward to the latest frame
    // boundary instead of reading overwritten data.
    if (inputWritePos_ - nextFrameEnd_ > 2048) {
        const int frames = (inputWritePos_ - kFftSize) / kHop;
        nextFrameEnd_ = kFftSize + frames * kHop;
    }
    while (inputWritePos_ >= nextFrameEnd_)
        processFrame();

    // Drain the output FIFOs (zero-pad at startup, before the first frame).
    // The wet signal is delayed by the STFT latency; the dry signal is read
    // from the input ring at the same delay so Mix blends phase-aligned.
    // Delta monitor outputs dry minus wet (the removed signal).
    const float outGain = dbToLinear(params_.outputGainDb);
    const int lat = getLatencySamples();
    int dryIdx = (inputWritePos_ - numSamples - lat) % ringCap;
    if (dryIdx < 0)
        dryIdx += ringCap;
    for (int n = 0; n < numSamples; ++n) {
        for (int ch = 0; ch < nCh; ++ch) {
            float wet = 0.0f;
            auto& fifo = outFifo_[static_cast<size_t>(ch)];
            if (!fifo.empty()) {
                wet = fifo.front();
                fifo.pop_front();
            }
            const float dry = inputRing_[static_cast<size_t>(ch)][static_cast<size_t>(dryIdx)];
            const float sample = (params_.monitor == 2)
                ? (dry - wet)
                : (dry * (1.0f - params_.mix) + wet * params_.mix);
            out[ch][n] = sample * outGain;
        }
        if (++dryIdx >= ringCap)
            dryIdx -= ringCap;
    }
}

void SpectralDucker::processFrame() {
    const int ringCap = static_cast<int>(inputRing_[0].size());
    // The frame covers the last kFftSize samples ending at nextFrameEnd_.
    const int frameStart = nextFrameEnd_ - kFftSize;
    nextFrameEnd_ += kHop;

    auto readRing = [&](const std::vector<float>& ring, int logicalIndex) {
        int idx = logicalIndex % ringCap;
        if (idx < 0)
            idx += ringCap;
        return ring[static_cast<size_t>(idx)];
    };

    // --- Sidechain analysis: window, FFT, per-band levels. ---
    // In LINK mode (SLAVE instance) the levels come from the MASTER instance
    // instead — the sidechain FFT is skipped entirely. In MASTER mode the
    // levels are measured from the MAIN input (the trigger track's audio).
    if (useLinkedLevels_) {
        std::copy(linkedLevelDb_.begin(), linkedLevelDb_.end(), bandLevelDb_.begin());
    } else {
        // Analysis source: main input channel 0 in MASTER mode, the mono
        // sidechain mix otherwise.
        const std::vector<float>& analysisRing =
            masterMode_ ? inputRing_.front() : inputRing_.back();
        for (int i = 0; i < kFftSize; ++i) {
            const float s = readRing(analysisRing, frameStart + i) * window_[static_cast<size_t>(i)];
            scSpectrum_[static_cast<size_t>(i)] = {s, 0.0f};
        }
        fft(scSpectrum_, false);

        // Per-band sidechain level (dBFS, normalised so a full-scale sine
        // reads ~0 dB: a Hann-windowed sine of amplitude A lands in its bin
        // with magnitude A*N/4).
        const float norm = 4.0f / static_cast<float>(kFftSize);
        for (int b = 0; b < kNumBands; ++b) {
            const int bin0 = bandBinStart_[static_cast<size_t>(b)];
            const int bin1 = bandBinEnd_[static_cast<size_t>(b)];
            float meanMag = 0.0f;
            for (int k = bin0; k < bin1; ++k)
                meanMag += std::abs(scSpectrum_[static_cast<size_t>(k)]);
            meanMag /= static_cast<float>(bin1 - bin0);
            bandLevelDb_[static_cast<size_t>(b)] = 20.0f * std::log10(meanMag * norm + kEps);
        }
    }

    // Target reduction per band: Amount scales how much of the excess over
    // the threshold is applied, capped at maxReductionDb. Bands outside
    // the detector cut range [lowCutHz, highCutHz] are never ducked.
    // Freeze holds the current reductions: the analysis above keeps
    // running (the display stays live) but the gains stop moving.
    if (!params_.freeze) {
        for (int b = 0; b < kNumBands; ++b) {
            const float levelDb = bandLevelDb_[static_cast<size_t>(b)];
            float target = 0.0f;
            const float centreHz = std::sqrt(bandEdgesHz_[static_cast<size_t>(b)]
                                             * bandEdgesHz_[static_cast<size_t>(b + 1)]);
            const bool inRange = centreHz >= params_.lowCutHz
                              && centreHz <= params_.highCutHz;
            if (inRange && levelDb > params_.thresholdDb)
                target = std::min(params_.amount * (levelDb - params_.thresholdDb),
                                  params_.maxReductionDb);

            float& current = bandReductionDb_[static_cast<size_t>(b)];
            const float coeff = (target > current) ? attackCoeff_ : releaseCoeff_;
            current += (target - current) * coeff;
            bandGain_[static_cast<size_t>(b)] = dbToLinear(-current);
        }
    }

    // MASTER mode: measure levels but apply no reduction — the trigger
    // track's audio passes through untouched.
    if (masterMode_)
        std::fill(bandGain_.begin(), bandGain_.end(), 1.0f);

    // --- Main path: window, FFT, apply per-band gains, IFFT, overlap-add. ---
    // With a Hann window and hop = N/4, the overlap-add of window^2 is the
    // constant 1.5 (the cosine terms cancel over the 4 overlapping frames),
    // so the IFFT output is divided by 1.5 for unity gain.
    constexpr float kOlaNorm = 1.5f;

    // FFT each main channel into its own spectrum buffer.
    std::vector<std::complex<float>>* specs[2] = {&mainSpectrum_, &mainSpectrum2_};
    for (int ch = 0; ch < numMainChannels_; ++ch) {
        auto& spec = *specs[ch];
        for (int i = 0; i < kFftSize; ++i) {
            const float s = readRing(inputRing_[static_cast<size_t>(ch)], frameStart + i)
                          * window_[static_cast<size_t>(i)];
            spec[static_cast<size_t>(i)] = {s, 0.0f};
        }
        fft(spec, false);
    }

    // Apply gains: positive-frequency bins by band, mirrored to the
    // negative half so the IFFT stays real. Bins below the first band edge
    // (incl. DC) and above the last edge are untouched (gain 1).
    auto applyGains = [&](std::vector<std::complex<float>>& spec) {
        for (int b = 0; b < kNumBands; ++b) {
            const float g = bandGain_[static_cast<size_t>(b)];
            const int bin0 = bandBinStart_[static_cast<size_t>(b)];
            const int bin1 = bandBinEnd_[static_cast<size_t>(b)];
            for (int k = bin0; k < bin1; ++k) {
                spec[static_cast<size_t>(k)] *= g;
                spec[static_cast<size_t>(kFftSize - k)] *= g;
            }
        }
    };

    if (numMainChannels_ == 2 && params_.stereoMode != 0) {
        // Mid/Side: transform to M/S in place (mainSpectrum_ <- Mid,
        // mainSpectrum2_ <- Side), duck only the chosen component, then
        // reconstruct L/R. The transform covers the whole spectrum; the
        // gains only touch the band range, as in stereo mode.
        for (int k = 0; k < kFftSize; ++k) {
            const auto l = mainSpectrum_[static_cast<size_t>(k)];
            const auto r = mainSpectrum2_[static_cast<size_t>(k)];
            mainSpectrum_[static_cast<size_t>(k)] = (l + r) * 0.5f;
            mainSpectrum2_[static_cast<size_t>(k)] = (l - r) * 0.5f;
        }
        applyGains(params_.stereoMode == 1 ? mainSpectrum_ : mainSpectrum2_);
        for (int k = 0; k < kFftSize; ++k) {
            const auto m = mainSpectrum_[static_cast<size_t>(k)];
            const auto s = mainSpectrum2_[static_cast<size_t>(k)];
            mainSpectrum_[static_cast<size_t>(k)] = m + s;
            mainSpectrum2_[static_cast<size_t>(k)] = m - s;
        }
    } else {
        // Stereo (or mono): the same gains on every channel.
        for (int ch = 0; ch < numMainChannels_; ++ch)
            applyGains(*specs[ch]);
    }

    // IFFT + overlap-add per channel.
    for (int ch = 0; ch < numMainChannels_; ++ch) {
        auto& spec = *specs[ch];
        fft(spec, true);

        auto& ola = ola_[static_cast<size_t>(ch)];
        for (int i = 0; i < kFftSize; ++i) {
            const float v = spec[static_cast<size_t>(i)].real()
                          * window_[static_cast<size_t>(i)] / kOlaNorm;
            ola[static_cast<size_t>(i)] += v;
        }
        // Emit the first hop of the overlap-add buffer, then shift it.
        auto& fifo = outFifo_[static_cast<size_t>(ch)];
        for (int i = 0; i < kHop; ++i)
            fifo.push_back(ola[static_cast<size_t>(i)]);
        std::copy(ola.begin() + kHop, ola.end(), ola.begin());
        std::fill(ola.end() - kHop, ola.end(), 0.0f);
    }
}

} // namespace spaceduck
