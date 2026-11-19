// SpaceDuckAudioProcessor.cpp — see header for the bus layout.

#include "SpaceDuckAudioProcessor.h"
#include "SpaceDuckEditor.h"

namespace {
constexpr int kMaxMainChannels = 2;
}

SpaceDuckAudioProcessor::SpaceDuckAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "SpaceDuck", spaceduck::createParameterLayout()) {
    // The sidechain bus is declared enabled by default (see README: a JUCE
    // team member notes sidechain buses must be activated by default, and
    // Cubasis only shows its sidechain button for effects that declare one).
    // canAddBus()/canRemoveBus() are deliberately left at their defaults
    // (false): buses are fixed at init, which also sidesteps the JUCE 9.0.1
    // AU SetBusCount bug.
}

SpaceDuckAudioProcessor::~SpaceDuckAudioProcessor() = default;

void SpaceDuckAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    const int numMainChannels = std::max(1, std::min(kMaxMainChannels, getMainBusNumInputChannels()));
    ducker_.prepare(sampleRate, numMainChannels);
    ducker_.reset();
    setLatencySamples(ducker_.getLatencySamples());
}

void SpaceDuckAudioProcessor::releaseResources() {
    ducker_.reset();
}

bool SpaceDuckAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Constrain only the main bus (must match in/out, must not be disabled)
    // and accept ANY sidechain layout — including disabled, mono, or stereo —
    // so the plugin still loads when the host connects nothing to bus 1.
    // (Pattern from JUCE's own NoiseGatePluginDemo.)
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && !layouts.getMainInputChannelSet().isDisabled();
}

spaceduck::InputMode SpaceDuckAudioProcessor::getInputMode() const {
    auto* param = apvts_.getParameter(spaceduck::Param::kMode);
    const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param);
    const int index = choice != nullptr ? static_cast<int>(choice->getIndex()) : 0;
    return static_cast<spaceduck::InputMode>(index);
}

void SpaceDuckAudioProcessor::pushParamsToCore() {
    spaceduck::DuckerParams p;
    p.amount = apvts_.getRawParameterValue(spaceduck::Param::kAmount)->load() / 100.0f;
    p.attackMs = apvts_.getRawParameterValue(spaceduck::Param::kAttack)->load();
    p.releaseMs = apvts_.getRawParameterValue(spaceduck::Param::kRelease)->load();
    p.thresholdDb = apvts_.getRawParameterValue(spaceduck::Param::kThreshold)->load();
    p.lowCutHz = apvts_.getRawParameterValue(spaceduck::Param::kLowCut)->load();
    p.highCutHz = apvts_.getRawParameterValue(spaceduck::Param::kHighCut)->load();
    p.mix = apvts_.getRawParameterValue(spaceduck::Param::kMix)->load() / 100.0f;
    p.outputGainDb = apvts_.getRawParameterValue(spaceduck::Param::kOutput)->load();
    p.maxReductionDb = apvts_.getRawParameterValue(spaceduck::Param::kCeiling)->load();
    p.monitor = static_cast<int>(
        apvts_.getRawParameterValue(spaceduck::Param::kMonitor)->load());
    p.freeze = apvts_.getRawParameterValue(spaceduck::Param::kFreeze)->load() > 0.5f;
    p.stereoMode = static_cast<int>(
        apvts_.getRawParameterValue(spaceduck::Param::kStereo)->load());
    ducker_.setParams(p);
}

void SpaceDuckAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    pushParamsToCore();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    auto mainBus = getBusBuffer(buffer, true, 0);
    const int numMainChannels = std::min(kMaxMainChannels, mainBus.getNumChannels());
    if (numMainChannels <= 0)
        return;

    // Is anything actually connected to the sidechain bus?
    const bool scConnected = getBus(true, 1) != nullptr
        && getBus(true, 1)->isEnabled()
        && getChannelCountOfBus(true, 1) > 0;
    sidechainConnected_.store(scConnected, std::memory_order_relaxed);

    const auto mode = getInputMode();
    const int linkChannel = static_cast<int>(
        apvts_.getRawParameterValue(spaceduck::Param::kLinkChannel)->load());

    // Build channel pointer arrays. The core reads all input samples before
    // writing any output, so in-place processing on the main bus is safe.
    const float* mainIn[kMaxMainChannels] = {nullptr, nullptr};
    float* mainOut[kMaxMainChannels] = {nullptr, nullptr};
    for (int ch = 0; ch < numMainChannels; ++ch) {
        mainIn[ch] = mainBus.getReadPointer(ch);
        mainOut[ch] = mainBus.getWritePointer(ch);
    }

    if (mode == spaceduck::InputMode::LinkMaster) {
        // MASTER: analyse the main input (the trigger track), pass it
        // through unducked, publish the measured band levels.
        ducker_.setMasterMode(true);
        ducker_.clearLinkedBandLevels();
        ducker_.process(mainIn, numMainChannels, nullptr, 0, mainOut, numSamples);
        spaceduck::LinkHub::instance().publish(linkChannel,
                                               ducker_.getBandLevelsDb().data());
        linkActive_.store(true, std::memory_order_relaxed);
        return;
    }

    ducker_.setMasterMode(false);

    if (mode == spaceduck::InputMode::LinkSlave) {
        // SLAVE: prefer the MASTER's published levels; fall back to the
        // sidechain bus if no MASTER is publishing on this channel.
        float linkedLevels[spaceduck::LinkHub::kNumBands];
        if (spaceduck::LinkHub::instance().read(linkChannel, linkedLevels)) {
            ducker_.setLinkedBandLevels(linkedLevels, spaceduck::LinkHub::kNumBands);
            linkActive_.store(true, std::memory_order_relaxed);
            ducker_.process(mainIn, numMainChannels, nullptr, 0, mainOut, numSamples);
        } else {
            linkActive_.store(false, std::memory_order_relaxed);
            ducker_.clearLinkedBandLevels();
            if (scConnected) {
                auto scBus = getBusBuffer(buffer, true, 1);
                const int numScChannels = std::min(kMaxMainChannels, scBus.getNumChannels());
                const float* scIn[kMaxMainChannels] = {nullptr, nullptr};
                for (int ch = 0; ch < numScChannels; ++ch)
                    scIn[ch] = scBus.getReadPointer(ch);
                ducker_.process(mainIn, numMainChannels, scIn, numScChannels, mainOut, numSamples);
            } else {
                ducker_.process(mainIn, numMainChannels, nullptr, 0, mainOut, numSamples);
            }
        }
        return;
    }

    // Sidechain mode (default): trigger comes from the AU sidechain bus.
    // An unconnected sidechain reads as silence, which the core treats as
    // "no trigger" — clean passthrough, never runaway ducking.
    linkActive_.store(false, std::memory_order_relaxed);
    if (scConnected) {
        auto scBus = getBusBuffer(buffer, true, 1);
        const int numScChannels = std::min(kMaxMainChannels, scBus.getNumChannels());
        const float* scIn[kMaxMainChannels] = {nullptr, nullptr};
        for (int ch = 0; ch < numScChannels; ++ch)
            scIn[ch] = scBus.getReadPointer(ch);
        ducker_.process(mainIn, numMainChannels, scIn, numScChannels, mainOut, numSamples);
    } else {
        ducker_.process(mainIn, numMainChannels, nullptr, 0, mainOut, numSamples);
    }
}

juce::String SpaceDuckAudioProcessor::getStatusText() const {
    const auto mode = getInputMode();
    const int linkChannel = static_cast<int>(
        apvts_.getRawParameterValue(spaceduck::Param::kLinkChannel)->load());
    const bool frozen = apvts_.getRawParameterValue(spaceduck::Param::kFreeze)->load() > 0.5f;
    juce::String text;
    switch (mode) {
        case spaceduck::InputMode::Sidechain:
            text = sidechainConnected_.load(std::memory_order_relaxed)
                ? "Sidechain bus — trigger connected"
                : "Sidechain bus — no trigger connected (passing through clean)";
            break;
        case spaceduck::InputMode::LinkMaster:
            text = "Link Master, channel " + juce::String(linkChannel) + " — publishing levels";
            break;
        case spaceduck::InputMode::LinkSlave:
            text = linkActive_.load(std::memory_order_relaxed)
                ? "Link Slave, channel " + juce::String(linkChannel) + " — linked to Master"
                : "Link Slave, channel " + juce::String(linkChannel)
                      + " — no Master found (check the Master instance and channel)";
            break;
    }
    if (frozen)
        text += " · curve frozen";
    return text;
}

juce::AudioProcessorEditor* SpaceDuckAudioProcessor::createEditor() {
    return new SpaceDuckEditor(*this);
}

std::vector<float> SpaceDuckAudioProcessor::getBandLevelsDb() const {
    return ducker_.getBandLevelsDb();
}

std::vector<float> SpaceDuckAudioProcessor::getBandReductionsDb() const {
    return ducker_.getBandReductionsDb();
}

std::vector<float> SpaceDuckAudioProcessor::getBandEdgesHz() const {
    return ducker_.getBandEdgesHz();
}

void SpaceDuckAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpaceDuckAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

// Factory function required by JUCE.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SpaceDuckAudioProcessor();
}
