// test_ducker.cpp — standalone test harness for SpectralDucker (no JUCE).
// Build:  g++ -std=c++17 -O2 -I../Source/DSP ../Source/DSP/SpectralDucker.cpp test_ducker.cpp -o test_ducker
// Run:    ./test_ducker
//
// Feeds synthetic signals through the ducker in 512-sample blocks (like a
// host would) and checks: unity passthrough, band-selective ducking,
// release recovery, Amount=0, listen mode, and latency.

#include "SpectralDucker.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

int g_failures = 0;

void check(bool ok, const char* name, const char* detail = "") {
    std::printf("[%s] %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        ++g_failures;
}

float rms(const std::vector<float>& v, size_t from, size_t to) {
    double sum = 0.0;
    for (size_t i = from; i < to && i < v.size(); ++i)
        sum += static_cast<double>(v[i]) * v[i];
    const size_t n = (to > from) ? (to - from) : 1;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

// Run the ducker over full signals, block by block. Returns stereo output.
std::vector<std::vector<float>> runDucker(spaceduck::SpectralDucker& d,
                                          const std::vector<float>& mainL,
                                          const std::vector<float>& mainR,
                                          const std::vector<float>* scL,
                                          const std::vector<float>* scR) {
    const size_t total = mainL.size();
    std::vector<std::vector<float>> out(2, std::vector<float>(total, 0.0f));
    for (size_t pos = 0; pos < total; pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, total - pos));
        const float* mainIn[2] = {mainL.data() + pos, mainR.data() + pos};
        const float* scIn[2] = {nullptr, nullptr};
        if (scL != nullptr) {
            scIn[0] = scL->data() + pos;
            scIn[1] = (scR != nullptr) ? scR->data() + pos : scL->data() + pos;
        }
        float* outPtr[2] = {out[0].data() + pos, out[1].data() + pos};
        d.process(mainIn, 2, scIn, scL != nullptr ? 2 : 0, outPtr, n);
    }
    return out;
}

std::vector<float> sine(double freq, double seconds, float amp, double phase = 0.0) {
    const size_t n = static_cast<size_t>(seconds * kSr);
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = amp * static_cast<float>(std::sin(2.0 * 3.141592653589793 * freq * static_cast<double>(i) / kSr + phase));
    return v;
}

std::vector<float> addSignals(const std::vector<std::vector<float>>& parts) {
    std::vector<float> v(parts[0].size(), 0.0f);
    for (const auto& p : parts)
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += p[i];
    return v;
}

// Find which band contains a frequency.
int bandForFreq(const spaceduck::SpectralDucker& d, float freq) {
    const auto& edges = d.getBandEdgesHz();
    for (int b = 0; b < spaceduck::SpectralDucker::kNumBands; ++b)
        if (freq >= edges[static_cast<size_t>(b)] && freq < edges[static_cast<size_t>(b) + 1])
            return b;
    return -1;
}

void testUnityPassthrough() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 0.7f;
    d.setParams(p);

    // Chord: 100 Hz + 1 kHz + 8 kHz. Sidechain silent -> output must equal input.
    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    std::vector<float> silence(main.size(), 0.0f);
    auto out = runDucker(d, main, main, &silence, &silence);

    const int lat = d.getLatencySamples();
    // Compare input vs output aligned by the latency, skipping edges.
    const size_t from = static_cast<size_t>(lat) + 4096;
    const size_t to = main.size() - 4096;
    double maxErr = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double err = std::abs(static_cast<double>(out[0][i]) - static_cast<double>(main[i - static_cast<size_t>(lat)]));
        if (err > maxErr)
            maxErr = err;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(max abs err %.5f, latency %d samples)", maxErr, lat);
    check(maxErr < 0.01, "unity passthrough (silent sidechain)", buf);
}

void testBandSelectiveDucking() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    p.thresholdDb = -40.0f;
    d.setParams(p);

    // Main: chord across the spectrum. Sidechain: 1 kHz sine burst, 0.5-1.5 s.
    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    auto burst = sine(1000, 2.0, 0.5f);
    for (size_t i = 0; i < burst.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (t < 0.5 || t >= 1.5)
            burst[i] = 0.0f;
    }
    auto out = runDucker(d, main, main, &burst, &burst);

    // Sample the band reductions while the burst is active (t ~ 1.0 s).
    // Re-run and snapshot gains mid-burst.
    spaceduck::SpectralDucker d2;
    d2.prepare(kSr, 2);
    d2.setParams(p);
    std::vector<float> midBurstRed;
    for (size_t pos = 0; pos < main.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
        const float* mainIn[2] = {main.data() + pos, main.data() + pos};
        const float* scIn[2] = {burst.data() + pos, burst.data() + pos};
        float tmp[2][kBlock];
        float* outPtr[2] = {tmp[0], tmp[1]};
        d2.process(mainIn, 2, scIn, 2, outPtr, n);
        const double t = static_cast<double>(pos) / kSr;
        if (t > 1.0 && t < 1.1)
            midBurstRed = d2.getBandReductionsDb();
    }

    const int b1k = bandForFreq(d, 1000.0f);
    const int b100 = bandForFreq(d, 100.0f);
    const int b8k = bandForFreq(d, 8000.0f);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "(1k band %d: %.1f dB, 100Hz band %d: %.1f dB, 8k band %d: %.1f dB)",
                  b1k, midBurstRed[static_cast<size_t>(b1k)],
                  b100, midBurstRed[static_cast<size_t>(b100)],
                  b8k, midBurstRed[static_cast<size_t>(b8k)]);
    const bool selective = midBurstRed[static_cast<size_t>(b1k)] > 6.0f
                        && midBurstRed[static_cast<size_t>(b100)] < 2.0f
                        && midBurstRed[static_cast<size_t>(b8k)] < 2.0f;
    check(selective, "band-selective ducking (1 kHz trigger)", buf);

    // Output level during the burst should drop (the 1 kHz part of the chord
    // is being ducked) compared with before the burst.
    const float rmsBefore = rms(out[0], 9600, 19200);   // 0.2-0.4 s
    const float rmsDuring = rms(out[0], 48000, 57600);  // 1.0-1.2 s
    std::snprintf(buf, sizeof(buf), "(RMS before %.3f, during %.3f)", rmsBefore, rmsDuring);
    check(rmsDuring < rmsBefore * 0.9f, "output level drops during trigger", buf);
}

void testCutRange() {
    // Same 1 kHz trigger as the selective test, but with the detector cut
    // range moved so the trigger falls outside it: no ducking must happen
    // at 1 kHz in either case, while the full-range control still ducks.
    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    auto burst = sine(1000, 2.0, 0.5f);
    for (size_t i = 0; i < burst.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (t < 0.5 || t >= 1.5)
            burst[i] = 0.0f;
    }

    auto midBurstReductions = [&](spaceduck::DuckerParams p) {
        spaceduck::SpectralDucker d;
        d.prepare(kSr, 2);
        d.setParams(p);
        std::vector<float> red;
        for (size_t pos = 0; pos < main.size(); pos += kBlock) {
            const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
            const float* mainIn[2] = {main.data() + pos, main.data() + pos};
            const float* scIn[2] = {burst.data() + pos, burst.data() + pos};
            float tmp[2][kBlock];
            float* outPtr[2] = {tmp[0], tmp[1]};
            d.process(mainIn, 2, scIn, 2, outPtr, n);
            const double t = static_cast<double>(pos) / kSr;
            if (t > 1.0 && t < 1.1)
                red = d.getBandReductionsDb();
        }
        return red;
    };

    spaceduck::DuckerParams base;
    base.amount = 1.0f;
    base.attackMs = 5.0f;
    base.releaseMs = 150.0f;
    base.thresholdDb = -40.0f;

    spaceduck::SpectralDucker probe;
    probe.prepare(kSr, 2);
    const int b1k = bandForFreq(probe, 1000.0f);
    char buf[256];

    const auto redFull = midBurstReductions(base);
    std::snprintf(buf, sizeof(buf), "(full range, 1k band: %.1f dB)",
                  redFull[static_cast<size_t>(b1k)]);
    check(redFull[static_cast<size_t>(b1k)] > 6.0f,
          "cut range control (full range ducks 1 kHz)", buf);

    auto lowCut = base;
    lowCut.lowCutHz = 2000.0f; // 1 kHz trigger is below the low cut
    const auto redLow = midBurstReductions(lowCut);
    std::snprintf(buf, sizeof(buf), "(low cut 2 kHz, 1k band: %.1f dB)",
                  redLow[static_cast<size_t>(b1k)]);
    check(redLow[static_cast<size_t>(b1k)] < 0.5f, "low cut excludes 1 kHz trigger", buf);

    auto highCut = base;
    highCut.highCutHz = 500.0f; // 1 kHz trigger is above the high cut
    const auto redHigh = midBurstReductions(highCut);
    std::snprintf(buf, sizeof(buf), "(high cut 500 Hz, 1k band: %.1f dB)",
                  redHigh[static_cast<size_t>(b1k)]);
    check(redHigh[static_cast<size_t>(b1k)] < 0.5f, "high cut excludes 1 kHz trigger", buf);
}

void testReleaseRecovery() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    d.setParams(p);

    auto main = addSignals({sine(1000, 2.0, 0.3f)});
    auto burst = sine(1000, 2.0, 0.5f);
    for (size_t i = 0; i < burst.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (t >= 0.5)
            burst[i] = 0.0f; // trigger only in the first 0.5 s
    }
    // Run, sampling reductions late (t ~ 1.5 s, i.e. 1 s after trigger end
    // with a 150 ms release -> fully recovered).
    std::vector<float> lateRed;
    for (size_t pos = 0; pos < main.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
        const float* mainIn[2] = {main.data() + pos, main.data() + pos};
        const float* scIn[2] = {burst.data() + pos, burst.data() + pos};
        float tmp[2][kBlock];
        float* outPtr[2] = {tmp[0], tmp[1]};
        d.process(mainIn, 2, scIn, 2, outPtr, n);
        const double t = static_cast<double>(pos) / kSr;
        if (t > 1.5 && t < 1.6)
            lateRed = d.getBandReductionsDb();
    }
    const int b1k = bandForFreq(d, 1000.0f);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(1k band reduction at t=1.5s: %.2f dB)", lateRed[static_cast<size_t>(b1k)]);
    check(lateRed[static_cast<size_t>(b1k)] < 0.5f, "release recovery (gains return to ~0 dB)", buf);
}

void testAmountZero() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 0.0f;
    d.setParams(p);

    auto main = addSignals({sine(1000, 2.0, 0.3f)});
    auto burst = sine(1000, 2.0, 0.8f); // loud trigger
    auto out = runDucker(d, main, main, &burst, &burst);
    const int lat = d.getLatencySamples();
    double maxErr = 0.0;
    for (size_t i = static_cast<size_t>(lat) + 4096; i < main.size() - 4096; ++i) {
        const double err = std::abs(static_cast<double>(out[0][i]) - static_cast<double>(main[i - static_cast<size_t>(lat)]));
        if (err > maxErr)
            maxErr = err;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(max abs err %.5f)", maxErr);
    check(maxErr < 0.01, "Amount=0 means no ducking", buf);
}

void testListenMode() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.monitor = 1; // sidechain listen
    d.setParams(p);

    auto main = addSignals({sine(200, 1.0, 0.3f)});
    auto sc = sine(3000, 1.0, 0.4f);
    auto out = runDucker(d, main, main, &sc, &sc);
    double maxErr = 0.0;
    for (size_t i = 0; i < main.size(); ++i) {
        const double err = std::abs(static_cast<double>(out[0][i]) - static_cast<double>(sc[i]));
        if (err > maxErr)
            maxErr = err;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(max abs err %.5f)", maxErr);
    check(maxErr < 0.001, "listen mode outputs the sidechain", buf);
}

void testLinkMode() {
    // MASTER: trigger track. Its main input IS the trigger; its band levels
    // are read out. SLAVE: ducked track. It gets the MASTER's levels injected
    // and must duck the same bands without any sidechain input of its own.
    spaceduck::SpectralDucker master;
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    master.prepare(kSr, 2);
    master.setParams(p);
    master.setMasterMode(true); // analyse its main input, pass it through

    spaceduck::SpectralDucker slave;
    slave.prepare(kSr, 2);
    slave.setParams(p);

    auto trigger = sine(1000, 2.0, 0.5f); // 1 kHz trigger, whole file
    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    std::vector<float> silence(main.size(), 0.0f);

    std::vector<float> slaveRed;
    for (size_t pos = 0; pos < main.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
        // MASTER: trigger as its main input, no sidechain bus at all.
        const float* mIn[2] = {trigger.data() + pos, trigger.data() + pos};
        float mTmp[2][kBlock];
        float* mOut[2] = {mTmp[0], mTmp[1]};
        master.process(mIn, 2, nullptr, 0, mOut, n);
        // Hand the MASTER's measured levels to the SLAVE each block.
        slave.setLinkedBandLevels(master.getBandLevelsDb().data(),
                                  spaceduck::SpectralDucker::kNumBands);
        const float* sIn[2] = {main.data() + pos, main.data() + pos};
        float sTmp[2][kBlock];
        float* sOut[2] = {sTmp[0], sTmp[1]};
        slave.process(sIn, 2, nullptr, 0, sOut, n);
        const double t = static_cast<double>(pos) / kSr;
        if (t > 1.0 && t < 1.1)
            slaveRed = slave.getBandReductionsDb();
    }
    const int b1k = bandForFreq(slave, 1000.0f);
    const int b100 = bandForFreq(slave, 100.0f);
    const int b8k = bandForFreq(slave, 8000.0f);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "(1k band %d: %.1f dB, 100Hz band %d: %.1f dB, 8k band %d: %.1f dB)",
                  b1k, slaveRed[static_cast<size_t>(b1k)],
                  b100, slaveRed[static_cast<size_t>(b100)],
                  b8k, slaveRed[static_cast<size_t>(b8k)]);
    const bool ok = slaveRed[static_cast<size_t>(b1k)] > 6.0f
                 && slaveRed[static_cast<size_t>(b100)] < 2.0f
                 && slaveRed[static_cast<size_t>(b8k)] < 2.0f;
    check(ok, "LINK mode (MASTER levels drive SLAVE ducking)", buf);
}

void testMidSide() {
    // In-phase stereo main = pure Mid; anti-phase = pure Side.
    // Sidechain: 1 kHz burst the whole file.
    auto burst = sine(1000, 2.0, 0.5f);
    auto tone = sine(1000, 2.0, 0.3f);
    auto negTone = tone;
    for (auto& s : negTone)
        s = -s;

    auto runMode = [&](int stereoMode, const std::vector<float>& mainL,
                       const std::vector<float>& mainR) {
        spaceduck::SpectralDucker d;
        d.prepare(kSr, 2);
        spaceduck::DuckerParams p;
        p.amount = 1.0f;
        p.attackMs = 5.0f;
        p.releaseMs = 150.0f;
        p.thresholdDb = -40.0f;
        p.stereoMode = stereoMode;
        d.setParams(p);
        return runDucker(d, mainL, mainR, &burst, &burst);
    };

    // Mid mode ducks an in-phase (Mid) signal but leaves an anti-phase
    // (Side) signal alone; Side mode does the opposite.
    auto outMidInPhase = runMode(1, tone, tone);
    auto outSideInPhase = runMode(2, tone, tone);
    auto outMidAntiPhase = runMode(1, tone, negTone);
    auto outSideAntiPhase = runMode(2, tone, negTone);

    const float rmsInPhase = rms(tone, 48000, 57600); // 1.0-1.2 s
    const float rMidIn = rms(outMidInPhase[0], 48000, 57600);
    const float rSideIn = rms(outSideInPhase[0], 48000, 57600);
    const float rMidAnti = rms(outMidAntiPhase[0], 48000, 57600);
    const float rSideAnti = rms(outSideAntiPhase[0], 48000, 57600);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "(in-phase RMS %.3f: Mid mode %.3f, Side mode %.3f; anti-phase: Mid %.3f, Side %.3f)",
                  rmsInPhase, rMidIn, rSideIn, rMidAnti, rSideAnti);
    const bool ok = rMidIn < rmsInPhase * 0.7f && rSideIn > rmsInPhase * 0.9f
                 && rMidAnti > rmsInPhase * 0.9f && rSideAnti < rmsInPhase * 0.7f;
    check(ok, "Mid/Side mode targets the right component", buf);
}

void testFreeze() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    p.thresholdDb = -40.0f;
    d.setParams(p);

    auto main = addSignals({sine(1000, 2.0, 0.3f)});
    auto burst = sine(1000, 2.0, 0.5f);
    for (size_t i = 0; i < burst.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (t >= 0.5)
            burst[i] = 0.0f; // trigger only in the first 0.5 s
    }
    const int b1k = bandForFreq(d, 1000.0f);
    std::vector<float> redAtFreeze, redLate;
    bool frozen = false;
    for (size_t pos = 0; pos < main.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
        const float* mainIn[2] = {main.data() + pos, main.data() + pos};
        const float* scIn[2] = {burst.data() + pos, burst.data() + pos};
        float tmp[2][kBlock];
        float* outPtr[2] = {tmp[0], tmp[1]};
        d.process(mainIn, 2, scIn, 2, outPtr, n);
        const double t = static_cast<double>(pos) / kSr;
        if (!frozen && t > 0.4) {
            // Freeze while the trigger is still active.
            redAtFreeze = d.getBandReductionsDb();
            auto pf = p;
            pf.freeze = true;
            d.setParams(pf);
            frozen = true;
        }
        if (t > 1.5 && t < 1.6)
            redLate = d.getBandReductionsDb();
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "(frozen at %.1f dB, still %.1f dB at t=1.5s)",
                  redAtFreeze[static_cast<size_t>(b1k)], redLate[static_cast<size_t>(b1k)]);
    check(redAtFreeze[static_cast<size_t>(b1k)] > 6.0f
              && std::abs(redLate[static_cast<size_t>(b1k)]
                          - redAtFreeze[static_cast<size_t>(b1k)]) < 0.5f,
          "Freeze holds the reduction curve", buf);
}

void testMix() {
    // Mix=0 must output the dry signal (delayed by the latency) even
    // while the trigger is active — no ducking at all.
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.mix = 0.0f;
    d.setParams(p);

    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    auto burst = sine(1000, 2.0, 0.8f); // loud trigger the whole file
    auto out = runDucker(d, main, main, &burst, &burst);
    const int lat = d.getLatencySamples();
    double maxErr = 0.0;
    for (size_t i = static_cast<size_t>(lat) + 4096; i < main.size() - 4096; ++i) {
        const double err = std::abs(static_cast<double>(out[0][i])
                                  - static_cast<double>(main[i - static_cast<size_t>(lat)]));
        if (err > maxErr)
            maxErr = err;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(max abs err %.5f)", maxErr);
    check(maxErr < 0.01, "Mix=0 outputs the dry signal", buf);
}

void testCeiling() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    p.thresholdDb = -40.0f;
    p.maxReductionDb = 6.0f; // ceiling
    d.setParams(p);

    auto main = addSignals({sine(1000, 2.0, 0.3f)});
    auto burst = sine(1000, 2.0, 0.8f); // loud trigger the whole file
    std::vector<float> midRed;
    for (size_t pos = 0; pos < main.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, main.size() - pos));
        const float* mainIn[2] = {main.data() + pos, main.data() + pos};
        const float* scIn[2] = {burst.data() + pos, burst.data() + pos};
        float tmp[2][kBlock];
        float* outPtr[2] = {tmp[0], tmp[1]};
        d.process(mainIn, 2, scIn, 2, outPtr, n);
        const double t = static_cast<double>(pos) / kSr;
        if (t > 1.0 && t < 1.1)
            midRed = d.getBandReductionsDb();
    }
    float maxRed = 0.0f;
    for (float r : midRed)
        maxRed = std::max(maxRed, r);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(max reduction %.1f dB, ceiling 6 dB)", maxRed);
    check(maxRed > 5.0f && maxRed <= 6.5f, "Ceiling caps the reduction", buf);
}

void testDelta() {
    // Delta monitor outputs dry minus wet: ~silence with no trigger,
    // clearly non-zero while the trigger is ducking the signal.
    spaceduck::DuckerParams p;
    p.amount = 1.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 150.0f;
    p.thresholdDb = -40.0f;
    p.monitor = 2;

    auto main = addSignals({sine(100, 2.0, 0.3f), sine(1000, 2.0, 0.3f), sine(8000, 2.0, 0.2f)});
    auto burst = sine(1000, 2.0, 0.5f);
    for (size_t i = 0; i < burst.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (t < 0.5 || t >= 1.5)
            burst[i] = 0.0f;
    }
    std::vector<float> silence(main.size(), 0.0f);

    spaceduck::SpectralDucker dTrig;
    dTrig.prepare(kSr, 2);
    dTrig.setParams(p);
    auto outTrig = runDucker(dTrig, main, main, &burst, &burst);
    const float rmsDuring = rms(outTrig[0], 48000, 57600); // 1.0-1.2 s

    spaceduck::SpectralDucker dQuiet;
    dQuiet.prepare(kSr, 2);
    dQuiet.setParams(p);
    auto outQuiet = runDucker(dQuiet, main, main, &silence, &silence);
    const float rmsQuiet = rms(outQuiet[0], 48000, 57600);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "(RMS during trigger %.4f, silent %.5f)", rmsDuring, rmsQuiet);
    check(rmsDuring > 0.02f && rmsQuiet < 0.001f, "Delta monitor outputs the removed signal", buf);
}

void testLatency() {
    spaceduck::SpectralDucker d;
    d.prepare(kSr, 2);
    spaceduck::DuckerParams p;
    p.amount = 0.0f; // no ducking, pure delay path
    d.setParams(p);

    std::vector<float> impulse(static_cast<size_t>(kSr), 0.0f);
    impulse[4800] = 1.0f; // impulse at 0.1 s
    std::vector<float> silence(impulse.size(), 0.0f);
    auto out = runDucker(d, impulse, impulse, &silence, &silence);
    // Find the output peak.
    size_t peak = 0;
    float peakVal = 0.0f;
    for (size_t i = 0; i < out[0].size(); ++i) {
        if (std::abs(out[0][i]) > peakVal) {
            peakVal = std::abs(out[0][i]);
            peak = i;
        }
    }
    const int measured = static_cast<int>(peak) - 4800;
    const int expected = d.getLatencySamples();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "(measured %d samples, reported %d)", measured, expected);
    check(std::abs(measured - expected) <= 512, "latency matches reported value", buf);
}

} // namespace

int main() {
    std::printf("SpaceDuck DSP core tests (48 kHz, FFT 2048, hop 512)\n");
    testUnityPassthrough();
    testBandSelectiveDucking();
    testCutRange();
    testReleaseRecovery();
    testAmountZero();
    testListenMode();
    testLinkMode();
    testMidSide();
    testFreeze();
    testMix();
    testCeiling();
    testDelta();
    testLatency();
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
