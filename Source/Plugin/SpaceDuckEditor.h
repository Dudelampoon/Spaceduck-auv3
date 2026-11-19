// SpaceDuckEditor.h — plugin UI.
//
// 2026 look: near-black background with a soft indigo radial glow,
// frosted-glass panels (translucent fills, large rounded corners, thin
// light borders), a cyan -> violet gradient accent, and knobs with
// gradient arcs and a soft glow. The live spectrum display (the
// sidechain's 32-band spectrum in a cyan -> violet gradient, the ducking
// it causes in warm orange) and the large Amount knob stay the heroes.
// Also: a large Amount knob, Attack / Release / Threshold / Low Cut /
// High Cut knobs, a second row with Mix / Output / Ceiling, a Monitor
// selector (Off / Sidechain / Delta), Freeze, a Mid/Side stereo mode,
// and presets. A TUTORIAL toggle in the header opens a second view
// explaining how to dial in the sound; it never touches a parameter.
// The display is fed by the processor's per-band levels
// and reductions, refreshed at 30 Hz.

#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SpaceDuckAudioProcessor;

class SpaceDuckEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SpaceDuckEditor(SpaceDuckAudioProcessor& processor);
    ~SpaceDuckEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Live spectrum + ducking display. Band levels are display-smoothed
    // (one-pole) so the curve moves smoothly at the 30 Hz refresh rate.
    class SpectrumDisplay : public juce::Component {
    public:
        SpectrumDisplay();
        void setEdges(const std::vector<float>& edgesHz);
        void setCutRange(float lowHz, float highHz);
        void setData(const std::vector<float>& levelsDb,
                     const std::vector<float>& reductionsDb);
        void paint(juce::Graphics& g) override;

    private:
        std::vector<float> edgesHz_;
        std::vector<float> levelsDb_;
        std::vector<float> reductionsDb_;
        std::vector<float> shownLevelsDb_; // smoothed copy of levelsDb_
        float lowCutHz_ = 40.0f;   // detector cut range, shaded outside it
        float highCutHz_ = 16000.0f;

        juce::Path smoothPath(const std::vector<juce::Point<float>>& points) const;
        float freqToX(float freqHz, float left, float width) const;
        float levelToY(float db, float top, float height) const;
        float reductionToY(float db, float top, float height) const;
    };

    // Dark modern look: rotary knobs with an arc + pointer, pill toggle,
    // dark combo box, thin linear slider.
    class SpaceDuckLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        SpaceDuckLookAndFeel();
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
        void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
        void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonW, int buttonH,
                          juce::ComboBox& box) override;
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              juce::Slider::SliderStyle style,
                              juce::Slider& slider) override;
    };

    // Tutorial tab: a second view in the same editor explaining how to
    // dial in the sound. Shown/hidden by the TUTORIAL toggle in the
    // header; it never touches any parameter or control.
    class TutorialView : public juce::Component {
    public:
        TutorialView();
    private:
        std::vector<std::unique_ptr<juce::Label>> labels_;
    };

    void timerCallback() override; // 30 Hz: display + status refresh
    void applyPreset(int presetId);
    void setParamValue(const char* paramId, float value);
    void setShowingTutorial(bool show);

    SpaceDuckAudioProcessor& processor_;

    SpaceDuckLookAndFeel lookAndFeel_; // declared first: destroyed last
    SpectrumDisplay display_;
    TutorialView tutorialView_;
    bool showingTutorial_ = false;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label statusLabel_;
    juce::Label presetLabel_;
    juce::ComboBox presetBox_;
    juce::ToggleButton tutorialButton_;

    juce::ComboBox modeBox_;
    juce::Label modeLabel_;
    juce::Slider linkChannelSlider_;
    juce::Label linkChannelLabel_;

    juce::ComboBox stereoBox_;
    juce::Label stereoLabel_;
    juce::ComboBox monitorBox_;
    juce::Label monitorLabel_;
    juce::ToggleButton freezeButton_;

    juce::Slider amountSlider_;
    juce::Slider attackSlider_;
    juce::Slider releaseSlider_;
    juce::Slider thresholdSlider_;
    juce::Slider lowCutSlider_;
    juce::Slider highCutSlider_;
    juce::Slider mixSlider_;
    juce::Slider outputSlider_;
    juce::Slider ceilingSlider_;
    juce::Label amountLabel_;
    juce::Label attackLabel_;
    juce::Label releaseLabel_;
    juce::Label thresholdLabel_;
    juce::Label lowCutLabel_;
    juce::Label highCutLabel_;
    juce::Label mixLabel_;
    juce::Label outputLabel_;
    juce::Label ceilingLabel_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> amountAttachment_;
    std::unique_ptr<SliderAttachment> attackAttachment_;
    std::unique_ptr<SliderAttachment> releaseAttachment_;
    std::unique_ptr<SliderAttachment> thresholdAttachment_;
    std::unique_ptr<SliderAttachment> lowCutAttachment_;
    std::unique_ptr<SliderAttachment> highCutAttachment_;
    std::unique_ptr<SliderAttachment> mixAttachment_;
    std::unique_ptr<SliderAttachment> outputAttachment_;
    std::unique_ptr<SliderAttachment> ceilingAttachment_;
    std::unique_ptr<SliderAttachment> linkChannelAttachment_;
    std::unique_ptr<ButtonAttachment> freezeAttachment_;
    std::unique_ptr<ComboAttachment> monitorAttachment_;
    std::unique_ptr<ComboAttachment> stereoAttachment_;
    std::unique_ptr<ComboAttachment> modeAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpaceDuckEditor)
};
