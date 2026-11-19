// SpaceDuckAudioProcessor.h — JUCE wrapper around the SpectralDucker core.
//
// Input layout (declared statically in the constructor — never dynamic):
//   bus 0 (input, stereo, main):  the signal to be ducked (e.g. the beat)
//   bus 1 (input, stereo):        the sidechain trigger (e.g. the vocal)
//   bus 0 (output, stereo):       the ducked signal
// There is no "sidechain flag" in JUCE or AU: input element 1 IS the
// sidechain by position. Hosts that support AUv3 sidechain routing
// (Cubasis 3.3+, AUM, Logic Pro for iPad) feed bus 1; hosts that don't
// (GarageBand) leave it silent, and the plugin falls back gracefully.
//
// LINK fallback: when the host cannot route a sidechain bus, use two
// instances — Link Master on the trigger track (publishes band levels via
// LinkHub), Link Slave on the ducked track (reads them). See LinkHub.h and
// README.md for the evidence and the known limitations.

#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../DSP/SpectralDucker.h"
#include "LinkHub.h"
#include "PluginParameters.h"

class SpaceDuckAudioProcessor : public juce::AudioProcessor {
public:
    SpaceDuckAudioProcessor();
    ~SpaceDuckAudioProcessor() override;

    // --- AudioProcessor overrides ---
    const juce::String getName() const override { return "SpaceDuck"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Used by the editor ---
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }
    spaceduck::InputMode getInputMode() const;
    juce::String getStatusText() const;

    // Per-band DSP data for the editor's spectrum display (copies of the
    // core's vectors — cheap at 32 floats, safe to call on the message
    // thread; values are only ever a frame stale at worst).
    std::vector<float> getBandLevelsDb() const;
    std::vector<float> getBandReductionsDb() const;
    std::vector<float> getBandEdgesHz() const;

private:
    void pushParamsToCore();

    juce::AudioProcessorValueTreeState apvts_;
    spaceduck::SpectralDucker ducker_;

    // Set on the audio thread in processBlock, read by the editor timer.
    std::atomic<bool> sidechainConnected_{false};
    std::atomic<bool> linkActive_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpaceDuckAudioProcessor)
};
