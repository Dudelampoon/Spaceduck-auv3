// LinkHub.h — in-process shared trigger store for LINK mode.
//
// LINK mode exists because some iOS hosts (notably Cubasis) cannot route
// audio into an AUv3's sidechain input bus. Instead, two instances of
// SpaceDuck cooperate:
//   - MASTER sits on the trigger track (e.g. the vocal). It analyses its own
//     main input and publishes the per-band sidechain levels here.
//   - SLAVE sits on the ducked track (e.g. the beat). It reads the latest
//     levels from here and ducks with them.
//
// How it works: all instances of the same AUv3 loaded by one host run in
// the same extension process on iOS, so a function-local static is shared
// between them. (If a host ever sandboxed each instance in its own process,
// LINK mode would silently fail — the SLAVE would see no updates and stop
// ducking. That is a known limitation; see README.)
//
// Thread-safety: processBlock may run on different realtime threads for
// different instances. All exchanges are short mutex-protected copies of a
// 32-float array — no allocation, no blocking calls inside the lock.
// A timestamp on each publish lets the SLAVE detect a dead MASTER (no
// update for >500 ms) and fall back to no ducking instead of freezing on
// stale levels.

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace spaceduck {

class LinkHub {
public:
    static constexpr int kNumChannels = 4;
    static constexpr int kNumBands = 32;

    static LinkHub& instance() {
        static LinkHub hub;
        return hub;
    }

    // MASTER: publish the latest per-band sidechain levels (dB) for a channel.
    void publish(int channel, const float* levelDb) {
        if (channel < 1 || channel > kNumChannels || levelDb == nullptr)
            return;
        auto& slot = slots_[static_cast<size_t>(channel - 1)];
        std::lock_guard<std::mutex> lock(slot.mutex);
        slot.levels = {};
        for (int i = 0; i < kNumBands; ++i)
            slot.levels[static_cast<size_t>(i)] = levelDb[i];
        slot.sequence += 1;
        slot.lastPublish = std::chrono::steady_clock::now();
    }

    // SLAVE: read the latest levels for a channel. Returns false if no
    // MASTER has ever published on this channel, or if the last publish is
    // older than maxAgeMs (dead MASTER — the caller should stop ducking).
    bool read(int channel, float* levelDbOut, int maxAgeMs = 500) const {
        if (channel < 1 || channel > kNumChannels || levelDbOut == nullptr)
            return false;
        const auto& slot = slots_[static_cast<size_t>(channel - 1)];
        std::lock_guard<std::mutex> lock(slot.mutex);
        if (slot.sequence == 0)
            return false;
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - slot.lastPublish).count();
        if (age > maxAgeMs)
            return false;
        for (int i = 0; i < kNumBands; ++i)
            levelDbOut[i] = slot.levels[static_cast<size_t>(i)];
        return true;
    }

    // For UI status: has a MASTER published on this channel recently?
    bool isLinked(int channel, int maxAgeMs = 500) const {
        float tmp[kNumBands];
        return read(channel, tmp, maxAgeMs);
    }

private:
    LinkHub() = default;

    struct Slot {
        mutable std::mutex mutex;
        std::array<float, kNumBands> levels{};
        std::uint64_t sequence = 0;
        std::chrono::steady_clock::time_point lastPublish{};
    };

    std::array<Slot, kNumChannels> slots_;
};

} // namespace spaceduck
