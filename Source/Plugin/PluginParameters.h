// PluginParameters.h — parameter IDs, ranges, and defaults for SpaceDuck.
// Shared by the processor (AudioProcessorValueTreeState layout) and the
// editor (attachments). Keep IDs stable: hosts store automation by ID.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace spaceduck {

namespace Param {
    inline constexpr const char* kAmount    = "amount";     // 0..100 %
    inline constexpr const char* kAttack    = "attack";     // 1..100 ms
    inline constexpr const char* kRelease   = "release";    // 50..500 ms
    inline constexpr const char* kThreshold = "threshold";  // -60..0 dB
    inline constexpr const char* kLowCut    = "lowCut";     // 40..8000 Hz
    inline constexpr const char* kHighCut   = "highCut";    // 100..16000 Hz
    inline constexpr const char* kMix       = "mix";        // 0..100 %
    inline constexpr const char* kOutput    = "output";     // -12..+12 dB
    inline constexpr const char* kCeiling   = "ceiling";    // 0..48 dB
    inline constexpr const char* kMonitor   = "monitor";    // choice: off/sidechain/delta
    inline constexpr const char* kFreeze    = "freeze";     // bool
    inline constexpr const char* kStereo    = "stereoMode"; // choice: stereo/mid/side
    inline constexpr const char* kMode      = "mode";       // choice: sidechain/link-master/link-slave
    inline constexpr const char* kLinkChannel = "linkChannel"; // 1..4
}

// Input modes. The default mode is chosen by the processor at construction
// (see SpaceDuckAudioProcessor) based on the host: hosts that expose an AU
// sidechain bus default to Sidechain; Cubasis defaults to Link Slave
// because Cubasis cannot route a sidechain bus to an AUv3 (see README).
enum class InputMode : int {
    Sidechain = 0,  // trigger comes from the AU sidechain input bus (bus 1)
    LinkMaster = 1, // this instance sits on the trigger track, publishes levels
    LinkSlave = 2,  // this instance sits on the ducked track, reads levels
};

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kAmount, "Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kAttack, "Attack",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f, 0.5f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kRelease, "Release",
        juce::NormalisableRange<float>(50.0f, 500.0f, 1.0f, 0.5f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kThreshold, "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.5f), -40.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Detector cut range. Defaults are the band edges (40 Hz / 16 kHz),
    // i.e. no filtering — the same behaviour as before these existed.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kLowCut, "Low Cut",
        juce::NormalisableRange<float>(40.0f, 8000.0f, 1.0f, 0.5f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kHighCut, "High Cut",
        juce::NormalisableRange<float>(100.0f, 16000.0f, 1.0f, 0.5f), 16000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kMix, "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kOutput, "Output",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Param::kCeiling, "Ceiling",
        juce::NormalisableRange<float>(0.0f, 48.0f, 0.5f), 24.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        Param::kMonitor, "Monitor",
        juce::StringArray{"Off", "Sidechain", "Delta"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        Param::kFreeze, "Freeze curve", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        Param::kStereo, "Stereo mode",
        juce::StringArray{"Stereo", "Mid", "Side"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        Param::kMode, "Input mode",
        juce::StringArray{"Sidechain", "Link Master", "Link Slave"},
        static_cast<int>(InputMode::Sidechain)));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        Param::kLinkChannel, "Link channel",
        1, 4, 1));

    return {params.begin(), params.end()};
}

} // namespace spaceduck
