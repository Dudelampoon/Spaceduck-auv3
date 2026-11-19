// SpaceDuckEditor.cpp — 2026 look: near-black radial glow, frosted-glass
// panels, cyan -> violet gradient accent. See the header for the layout.
// A TUTORIAL toggle in the header opens a second view explaining how to
// dial in the sound; it never touches a parameter.
// The spectrum display is the centrepiece: the sidechain's band spectrum
// (cyan -> violet gradient, filled) with the per-band ducking it causes
// overlaid (warm orange line), on a log frequency axis.

#include "SpaceDuckEditor.h"
#include "PluginParameters.h"
#include "SpaceDuckAudioProcessor.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kEditorWidth = 660;
constexpr int kEditorHeight = 600;

constexpr float kBandLowHz = 40.0f;
constexpr float kBandHighHz = 16000.0f;
constexpr float kDisplayDbMin = -60.0f; // spectrum dB floor shown
constexpr float kMaxReductionShownDb = 24.0f;

// 2026 look: near-black with a soft indigo glow, frosted-glass panels
// (translucent white fills, hairline light borders), and a cyan -> violet
// gradient accent. The ducking curve stays warm orange for contrast.
const juce::Colour kBackground(0xff05070b);    // near-black
const juce::Colour kBackgroundGlow(0xff10142a); // indigo glow, top centre
const juce::Colour kPanel(0x14ffffff);         // frosted glass fill
const juce::Colour kPanelEdge(0x30ffffff);     // frosted glass border
const juce::Colour kGrid(0x0dffffff);          // hairline grid (white, faint)
const juce::Colour kText(0xfff2f5f8);          // primary text
const juce::Colour kTextDim(0xff9aa7b4);       // secondary text
const juce::Colour kAccent(0xff22d3ee);        // cyan end of the accent gradient
const juce::Colour kAccentViolet(0xff8b5cf6);  // violet end of the accent gradient
const juce::Colour kSubtitle(0xffa78bfa);      // soft violet for the subtitle
const juce::Colour kSpectrumLine(0xff8fe6ff);  // spectrum curve (legend swatch)
const juce::Colour kReduction(0xffffa62b);     // ducking curve (warm orange)
const juce::Colour kKnobBody(0xff10161f);      // knob body edge (dark)
const juce::Colour kKnobBodyHi(0xff2b3a4d);    // knob body centre highlight
const juce::Colour kKnobEdge(0x2effffff);      // knob rim (light, translucent)
const juce::Colour kTrack(0x1fffffff);         // knob track (white, faint)

void setupRotary(juce::Slider& slider, bool big) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, big ? 80 : 70, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
}

void setupKnobLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, kTextDim);
    label.setJustificationType(juce::Justification::centred);
}

// Arc path built from line segments (no angle-convention guessing):
// angle `a` is clockwise from 12 o'clock, direction (sin a, -cos a).
void addArcSegments(juce::Path& path, juce::Point<float> centre, float radius,
                    float startAngle, float endAngle) {
    constexpr int kSegments = 48;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = startAngle + (endAngle - startAngle)
                                        * static_cast<float>(i)
                                        / static_cast<float>(kSegments);
        const juce::Point<float> p(centre.x + std::sin(a) * radius,
                                   centre.y - std::cos(a) * radius);
        if (i == 0)
            path.startNewSubPath(p);
        else
            path.lineTo(p);
    }
}

// Stroke an arc in short segments, lerping the colour from `from` to `to`
// along the sweep — a gradient arc (JUCE has no gradient stroke primitive).
void strokeGradientArc(juce::Graphics& g, juce::Point<float> centre, float radius,
                       float startAngle, float endAngle, juce::Colour from,
                       juce::Colour to, float strokeWidth) {
    constexpr int kSegments = 64;
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
        juce::Path seg;
        addArcSegments(seg, centre, radius,
                       startAngle + (endAngle - startAngle) * t0,
                       startAngle + (endAngle - startAngle) * t1);
        g.setColour(from.interpolatedWith(to, (t0 + t1) * 0.5f));
        g.strokePath(seg, juce::PathStrokeType(strokeWidth,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

// One-tap starting points (editor-only; they just set parameter values).
// Sound only — they never touch routing (input mode, link channel, monitor).
struct Preset {
    int id;
    const char* name;
    float amount, attack, release, threshold, lowCut, highCut, mix, output, ceiling;
    int stereoMode; // 0 stereo, 1 mid, 2 side
};

const Preset kPresets[] = {
    {1, "Vocal over beat", 70.0f, 10.0f, 180.0f, -42.0f, 120.0f, 9000.0f, 100.0f, 0.0f, 18.0f, 1},
    {2, "Kick pump", 85.0f, 5.0f, 120.0f, -35.0f, 40.0f, 500.0f, 100.0f, 0.0f, 24.0f, 0},
    {3, "Gentle glue", 35.0f, 25.0f, 300.0f, -45.0f, 80.0f, 12000.0f, 60.0f, 0.0f, 12.0f, 0},
    {4, "Default", 70.0f, 10.0f, 150.0f, -40.0f, 40.0f, 16000.0f, 100.0f, 0.0f, 24.0f, 0},
};

// Tutorial tab content: short plain-language sections on dialling in the
// sound. Bodies use '\n' line breaks (juce::Label draws them as separate
// lines); keep lines short enough to fit the 628 px view at 12 pt.
struct TutorialSection {
    const char* heading;
    const char* body;
};

const TutorialSection kTutorialSections[] = {
    {"SETUP — GET THE TRIGGER IN",
     "Put SpaceDuck on the track to duck — usually the beat. In Cubasis 3.3+,\n"
     "tap the sidechain button in the Effect Rack and pick the vocal track.\n"
     "No button? Use LINK mode: Link Master on the vocal, Link Slave on the beat."},
    {"AMOUNT — HOW MUCH IT DUCKS",
     "Start around 70%. Turn it up until the vocal sits clearly over the beat.\n"
     "If you can hear it pumping, back it off a little."},
    {"THRESHOLD — WHEN IT DUCKS",
     "Lower makes it duck more easily. Set it just under the vocal's level so\n"
     "it only fires when the vocal is actually singing."},
    {"ATTACK / RELEASE — THE FEEL",
     "Fast attack (5–10 ms) grabs the beat the instant the vocal starts.\n"
     "Short release pumps, longer release (around 150 ms) stays smooth."},
    {"LOW CUT / HIGH CUT — WHERE IT DUCKS",
     "Narrow the ducking to where vocal and beat clash. Cut the lows so the\n"
     "kick doesn't trigger it, cut the highs so the hats don't."},
    {"MIX / OUTPUT / CEILING",
     "Mix below 100% blends unducked signal back in. Output makes up lost\n"
     "level. Ceiling caps the deepest cut."},
    {"MONITOR — HEAR WHAT'S HAPPENING",
     "Monitor on Delta lets you hear exactly what's being removed. If you\n"
     "hear the vocal in there, your cuts are too wide."},
    {"PRESETS — STARTING POINTS",
     "Vocal over beat is the all-rounder, Kick pump pumps, Gentle glue is\n"
     "subtle. Pick one, then tweak Amount and Threshold to taste."},
};

} // namespace

// ---------------------------------------------------------------------------
// SpectrumDisplay
// ---------------------------------------------------------------------------

SpaceDuckEditor::SpectrumDisplay::SpectrumDisplay() {
    setOpaque(true);
}

void SpaceDuckEditor::SpectrumDisplay::setEdges(const std::vector<float>& edgesHz) {
    edgesHz_ = edgesHz;
}

void SpaceDuckEditor::SpectrumDisplay::setCutRange(float lowHz, float highHz) {
    lowCutHz_ = lowHz;
    highCutHz_ = highHz;
}

void SpaceDuckEditor::SpectrumDisplay::setData(const std::vector<float>& levelsDb,
                                              const std::vector<float>& reductionsDb) {
    if (levelsDb.size() != 32 || reductionsDb.size() != 32)
        return;
    if (shownLevelsDb_.size() != levelsDb.size()) {
        shownLevelsDb_ = levelsDb;
    } else {
        for (size_t i = 0; i < levelsDb.size(); ++i)
            shownLevelsDb_[i] += 0.35f * (levelsDb[i] - shownLevelsDb_[i]);
    }
    levelsDb_ = levelsDb;
    reductionsDb_ = reductionsDb;
    repaint();
}

float SpaceDuckEditor::SpectrumDisplay::freqToX(float freqHz, float left,
                                               float width) const {
    const float f = std::max(kBandLowHz, std::min(kBandHighHz, freqHz));
    return left + (std::log(f / kBandLowHz) / std::log(kBandHighHz / kBandLowHz)) * width;
}

float SpaceDuckEditor::SpectrumDisplay::levelToY(float db, float top,
                                                float height) const {
    const float clamped = std::max(kDisplayDbMin, std::min(0.0f, db));
    return top + (1.0f - (clamped - kDisplayDbMin) / (0.0f - kDisplayDbMin)) * height;
}

float SpaceDuckEditor::SpectrumDisplay::reductionToY(float db, float top,
                                                    float height) const {
    const float clamped = std::max(0.0f, std::min(kMaxReductionShownDb, db));
    return top + (1.0f - clamped / kMaxReductionShownDb) * height;
}

juce::Path SpaceDuckEditor::SpectrumDisplay::smoothPath(
    const std::vector<juce::Point<float>>& points) const {
    juce::Path path;
    const int n = static_cast<int>(points.size());
    if (n == 0)
        return path;
    path.startNewSubPath(points.front());
    if (n == 1)
        return path;
    for (int i = 0; i < n - 1; ++i) {
        const auto& p0 = points[static_cast<size_t>(std::max(0, i - 1))];
        const auto& p1 = points[static_cast<size_t>(i)];
        const auto& p2 = points[static_cast<size_t>(i + 1)];
        const auto& p3 = points[static_cast<size_t>(std::min(n - 1, i + 2))];
        const auto c1 = p1 + (p2 - p0) * (1.0f / 6.0f);
        const auto c2 = p2 - (p3 - p1) * (1.0f / 6.0f);
        path.cubicTo(c1, c2, p2);
    }
    return path;
}

void SpaceDuckEditor::SpectrumDisplay::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds, 16.0f);
    g.setColour(kPanelEdge);
    g.drawRoundedRectangle(bounds, 16.0f, 1.0f);

    const float left = 46.0f, right = bounds.getWidth() - 12.0f;
    const float top = 22.0f, bottom = bounds.getHeight() - 20.0f;
    const float plotW = right - left;
    const float plotH = bottom - top;

    // Grid + labels.
    g.setColour(kGrid);
    g.setFont(juce::Font(10.0f));
    for (float db : {-20.0f, -40.0f, -60.0f}) {
        const float y = levelToY(db, top, plotH);
        g.drawLine(left, y, right, y, 1.0f);
        g.setColour(kTextDim);
        g.drawText(juce::String(static_cast<int>(db)),
                   juce::Rectangle<float>(6.0f, y - 7.0f, 34.0f, 14.0f),
                   juce::Justification::right);
        g.setColour(kGrid);
    }
    for (float freq : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(freq, left, plotW);
        g.drawLine(x, top, x, bottom, 1.0f);
        g.setColour(kTextDim);
        const juce::String label = freq < 1000.0f ? juce::String(static_cast<int>(freq))
                                                  : juce::String(static_cast<int>(freq / 1000.0f)) + "k";
        g.drawText(label, juce::Rectangle<float>(x - 20.0f, bottom + 3.0f, 40.0f, 14.0f),
                   juce::Justification::centred);
        g.setColour(kGrid);
    }

    // Shade the regions outside the detector cut range (dimmed = not ducked).
    g.setColour(juce::Colour(0x8c05070b));
    const float cutL = freqToX(lowCutHz_, left, plotW);
    const float cutR = freqToX(highCutHz_, left, plotW);
    if (cutL > left)
        g.fillRect(juce::Rectangle<float>(left, top, cutL - left, plotH));
    if (cutR < right)
        g.fillRect(juce::Rectangle<float>(cutR, top, right - cutR, plotH));

    // Legend.
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    const float legendY = 7.0f;
    float legendX = right - 168.0f;
    g.setColour(kSpectrumLine);
    g.drawLine(legendX, legendY + 5.0f, legendX + 18.0f, legendY + 5.0f, 2.0f);
    g.setColour(kTextDim);
    g.drawText("SIDECHAIN", juce::Rectangle<float>(legendX + 22.0f, legendY, 62.0f, 12.0f),
               juce::Justification::left);
    legendX += 88.0f;
    g.setColour(kReduction);
    g.drawLine(legendX, legendY + 5.0f, legendX + 18.0f, legendY + 5.0f, 2.0f);
    g.setColour(kTextDim);
    g.drawText("DUCKING", juce::Rectangle<float>(legendX + 22.0f, legendY, 62.0f, 12.0f),
               juce::Justification::left);

    if (edgesHz_.size() != 33 || shownLevelsDb_.size() != 32
        || reductionsDb_.size() != 32)
        return;

    // Band-centre points for both curves.
    std::vector<juce::Point<float>> spectrumPoints, reductionPoints;
    for (int b = 0; b < 32; ++b) {
        const float centreHz = std::sqrt(edgesHz_[static_cast<size_t>(b)]
                                         * edgesHz_[static_cast<size_t>(b + 1)]);
        const float x = freqToX(centreHz, left, plotW);
        spectrumPoints.push_back({x, levelToY(shownLevelsDb_[static_cast<size_t>(b)], top, plotH)});
        reductionPoints.push_back({x, reductionToY(reductionsDb_[static_cast<size_t>(b)], top, plotH)});
    }

    // Spectrum: gradient fill under a smooth curve.
    const juce::Path spectrumPath = smoothPath(spectrumPoints);
    juce::Path fill = spectrumPath;
    fill.lineTo(spectrumPoints.back().x, bottom);
    fill.lineTo(spectrumPoints.front().x, bottom);
    fill.closeSubPath();
    juce::ColourGradient gradient(kAccent.withAlpha(0.30f), 0.0f, top,
                                  kAccentViolet.withAlpha(0.03f), 0.0f, bottom, false);
    g.setGradientFill(gradient);
    g.fillPath(fill);
    // Spectrum line: soft glow, then a cyan -> violet gradient stroke.
    g.setColour(kAccent.withAlpha(0.15f));
    g.strokePath(spectrumPath, juce::PathStrokeType(6.0f));
    juce::ColourGradient lineGradient(kAccent, spectrumPoints.front().x, 0.0f,
                                      kAccentViolet, spectrumPoints.back().x, 0.0f, false);
    g.setGradientFill(lineGradient);
    g.strokePath(spectrumPath, juce::PathStrokeType(2.0f));

    // Ducking: warm orange line on the same axis (0 dB at the bottom,
    // 24 dB at top), with a soft glow.
    const juce::Path reductionPath = smoothPath(reductionPoints);
    g.setColour(kReduction.withAlpha(0.15f));
    g.strokePath(reductionPath, juce::PathStrokeType(6.0f));
    g.setColour(kReduction);
    g.strokePath(reductionPath, juce::PathStrokeType(2.0f));

    // Idle hint when there is no trigger signal.
    const float maxLevel = *std::max_element(shownLevelsDb_.begin(), shownLevelsDb_.end());
    if (maxLevel <= -115.0f) {
        g.setColour(kTextDim);
        g.setFont(juce::Font(12.0f));
        g.drawText("no sidechain signal — play the vocal",
                   juce::Rectangle<float>(left, top, plotW, plotH),
                   juce::Justification::centred, true);
    }
}

// ---------------------------------------------------------------------------
// SpaceDuckLookAndFeel
// ---------------------------------------------------------------------------

SpaceDuckEditor::SpaceDuckLookAndFeel::SpaceDuckLookAndFeel() {
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff12161d));
    setColour(juce::ComboBox::outlineColourId, kKnobEdge);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::arrowColourId, kTextDim);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0d1117));
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff4338ca));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0b0f14));
    setColour(juce::Slider::textBoxOutlineColourId, kKnobEdge);
    setColour(juce::Slider::textBoxHighlightColourId, kAccent);
}

void SpaceDuckEditor::SpaceDuckLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider) {
    juce::ignoreUnused(slider);
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(width), static_cast<float>(height));
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
    const auto centre = bounds.getCentre();

    // Track arc (gap at the bottom).
    juce::Path track;
    addArcSegments(track, centre, radius, rotaryStartAngle, rotaryEndAngle);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    // Value arc: soft glow under a cyan -> violet gradient arc.
    const float endAngle = rotaryStartAngle
        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (sliderPosProportional > 0.001f) {
        juce::Path valueArc;
        addArcSegments(valueArc, centre, radius, rotaryStartAngle, endAngle);
        g.setColour(kAccent.withAlpha(0.07f));
        g.strokePath(valueArc, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(kAccent.withAlpha(0.14f));
        g.strokePath(valueArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        strokeGradientArc(g, centre, radius, rotaryStartAngle, endAngle,
                          kAccent, kAccentViolet, 4.0f);
    }

    // Knob body: radial highlight, light rim, white pointer.
    const float bodyR = radius * 0.72f;
    juce::ColourGradient bodyGradient(kKnobBodyHi,
                                      centre.x - bodyR * 0.3f, centre.y - bodyR * 0.35f,
                                      kKnobBody,
                                      centre.x + bodyR, centre.y + bodyR, true);
    g.setGradientFill(bodyGradient);
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour(kKnobEdge);
    g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);
    const juce::Point<float> dir(std::sin(endAngle), -std::cos(endAngle));
    g.setColour(kText);
    g.drawLine({centre + dir * bodyR * 0.35f, centre + dir * bodyR * 0.92f}, 2.0f);
}

void SpaceDuckEditor::SpaceDuckLookAndFeel::drawToggleButton(
    juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool isOn = button.getToggleState();
    if (isOn) {
        // Gradient pill when frozen.
        juce::ColourGradient gradient(kAccent, bounds.getX(), bounds.getY(),
                                      kAccentViolet, bounds.getRight(),
                                      bounds.getBottom(), false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(juce::Colour(0xff05070b));
    } else {
        g.setColour(kPanel);
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(kPanelEdge);
        g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
        g.setColour(kTextDim);
    }
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
}

void SpaceDuckEditor::SpaceDuckLookAndFeel::drawComboBox(
    juce::Graphics& g, int width, int height, bool isButtonDown,
    int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) {
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                         static_cast<float>(height));
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(kPanelEdge);
    g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
    g.setColour(kText);
    g.setFont(juce::Font(13.0f));
    g.drawText(box.getText(), bounds.reduced(10.0f, 0.0f),
               juce::Justification::centredLeft, true);
    // Down arrow.
    juce::Path arrow;
    const float ax = static_cast<float>(width) - 18.0f;
    const float ay = static_cast<float>(height) * 0.5f;
    arrow.addTriangle({ax - 5.0f, ay - 3.0f}, {ax + 5.0f, ay - 3.0f}, {ax, ay + 4.0f});
    g.setColour(kTextDim);
    g.fillPath(arrow);
}

void SpaceDuckEditor::SpaceDuckLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
    juce::Slider& slider) {
    juce::ignoreUnused(slider);
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                         minSliderPos, maxSliderPos, style, slider);
        return;
    }
    const float trackY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(kTrack);
    g.fillRoundedRectangle(
        juce::Rectangle<float>(minSliderPos, trackY - 2.0f,
                               maxSliderPos - minSliderPos, 4.0f), 2.0f);
    juce::ColourGradient fillGradient(kAccent, minSliderPos, 0.0f,
                                      kAccentViolet, sliderPos, 0.0f, false);
    g.setGradientFill(fillGradient);
    g.fillRoundedRectangle(
        juce::Rectangle<float>(minSliderPos, trackY - 2.0f,
                               sliderPos - minSliderPos, 4.0f), 2.0f);
    // Thumb: soft glow + white dot.
    g.setColour(kAccent.withAlpha(0.25f));
    g.fillEllipse(sliderPos - 10.0f, trackY - 10.0f, 20.0f, 20.0f);
    g.setColour(kText);
    g.fillEllipse(sliderPos - 7.0f, trackY - 7.0f, 14.0f, 14.0f);
}

// ---------------------------------------------------------------------------
// TutorialView
// ---------------------------------------------------------------------------

SpaceDuckEditor::TutorialView::TutorialView() {
    auto addLabel = [this](const juce::String& text, const juce::Font& font,
                           const juce::Colour& colour, int x, int y, int w, int h,
                           juce::Justification justification) {
        auto label = std::make_unique<juce::Label>();
        label->setText(text, juce::dontSendNotification);
        label->setFont(font);
        label->setColour(juce::Label::textColourId, colour);
        label->setJustificationType(justification);
        label->setBounds(x, y, w, h);
        addAndMakeVisible(label.get());
        labels_.push_back(std::move(label));
    };

    addLabel("HOW TO DIAL IN THE SOUND", juce::Font(16.0f, juce::Font::bold), kText,
             0, 0, 628, 22, juce::Justification::centred);
    addLabel("Tap TUTORIAL again to go back to the controls.", juce::Font(11.0f), kTextDim,
             0, 24, 628, 16, juce::Justification::centred);

    int y = 50;
    for (const auto& section : kTutorialSections) {
        int bodyLines = 1;
        for (const char* p = section.body; *p != '\0'; ++p)
            if (*p == '\n')
                ++bodyLines;
        addLabel(section.heading, juce::Font(12.0f, juce::Font::bold), kAccent,
                 0, y, 628, 16, juce::Justification::topLeft);
        addLabel(section.body, juce::Font(12.0f), kTextDim,
                 0, y + 16, 628, bodyLines * 16, juce::Justification::topLeft);
        y += 16 + bodyLines * 16 + 8;
    }
}

// ---------------------------------------------------------------------------
// SpaceDuckEditor
// ---------------------------------------------------------------------------

SpaceDuckEditor::SpaceDuckEditor(SpaceDuckAudioProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor) {
    setSize(kEditorWidth, kEditorHeight);
    setLookAndFeel(&lookAndFeel_);

    titleLabel_.setText("SPACEDUCK", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(28.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, kText);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText("SPECTRAL SIDECHAIN DUCKER", juce::dontSendNotification);
    subtitleLabel_.setFont(juce::Font(11.0f, juce::Font::bold));
    subtitleLabel_.setColour(juce::Label::textColourId, kSubtitle);
    subtitleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitleLabel_);

    statusLabel_.setText("Starting…", juce::dontSendNotification);
    statusLabel_.setFont(juce::Font(11.0f));
    statusLabel_.setColour(juce::Label::textColourId, kTextDim);
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel_);

    presetLabel_.setText("PRESET", juce::dontSendNotification);
    presetLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    presetLabel_.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(presetLabel_);
    for (const auto& preset : kPresets)
        presetBox_.addItem(preset.name, preset.id);
    presetBox_.setText("Presets", juce::dontSendNotification);
    presetBox_.onChange = [this] { applyPreset(presetBox_.getSelectedId()); };
    addAndMakeVisible(presetBox_);

    tutorialButton_.setButtonText("TUTORIAL");
    tutorialButton_.onClick = [this] { setShowingTutorial(tutorialButton_.getToggleState()); };
    addAndMakeVisible(tutorialButton_);
    addChildComponent(tutorialView_);

    addAndMakeVisible(display_);
    display_.setEdges(processor_.getBandEdgesHz());

    setupKnobLabel(amountLabel_, "AMOUNT");
    setupKnobLabel(attackLabel_, "ATTACK");
    setupKnobLabel(releaseLabel_, "RELEASE");
    setupKnobLabel(thresholdLabel_, "THRESHOLD");
    setupKnobLabel(lowCutLabel_, "LOW CUT");
    setupKnobLabel(highCutLabel_, "HIGH CUT");
    setupKnobLabel(mixLabel_, "MIX");
    setupKnobLabel(outputLabel_, "OUTPUT");
    setupKnobLabel(ceilingLabel_, "CEILING");
    addAndMakeVisible(amountLabel_);
    addAndMakeVisible(attackLabel_);
    addAndMakeVisible(releaseLabel_);
    addAndMakeVisible(thresholdLabel_);
    addAndMakeVisible(lowCutLabel_);
    addAndMakeVisible(highCutLabel_);
    addAndMakeVisible(mixLabel_);
    addAndMakeVisible(outputLabel_);
    addAndMakeVisible(ceilingLabel_);

    setupRotary(amountSlider_, true);
    setupRotary(attackSlider_, false);
    setupRotary(releaseSlider_, false);
    setupRotary(thresholdSlider_, false);
    setupRotary(lowCutSlider_, false);
    setupRotary(highCutSlider_, false);
    setupRotary(mixSlider_, false);
    setupRotary(outputSlider_, false);
    setupRotary(ceilingSlider_, false);
    addAndMakeVisible(amountSlider_);
    addAndMakeVisible(attackSlider_);
    addAndMakeVisible(releaseSlider_);
    addAndMakeVisible(thresholdSlider_);
    addAndMakeVisible(lowCutSlider_);
    addAndMakeVisible(highCutSlider_);
    addAndMakeVisible(mixSlider_);
    addAndMakeVisible(outputSlider_);
    addAndMakeVisible(ceilingSlider_);

    modeLabel_.setText("INPUT", juce::dontSendNotification);
    modeLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    modeLabel_.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(modeLabel_);
    modeBox_.addItem("Sidechain bus", 1);
    modeBox_.addItem("Link Master", 2);
    modeBox_.addItem("Link Slave", 3);
    addAndMakeVisible(modeBox_);

    linkChannelLabel_.setText("LINK CH", juce::dontSendNotification);
    linkChannelLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    linkChannelLabel_.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(linkChannelLabel_);
    linkChannelSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    linkChannelSlider_.setRange(1, 4, 1);
    linkChannelSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
    addAndMakeVisible(linkChannelSlider_);

    stereoLabel_.setText("STEREO", juce::dontSendNotification);
    stereoLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    stereoLabel_.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(stereoLabel_);
    stereoBox_.addItem("Stereo", 1);
    stereoBox_.addItem("Mid", 2);
    stereoBox_.addItem("Side", 3);
    addAndMakeVisible(stereoBox_);

    monitorLabel_.setText("MONITOR", juce::dontSendNotification);
    monitorLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    monitorLabel_.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(monitorLabel_);
    monitorBox_.addItem("Off", 1);
    monitorBox_.addItem("Sidechain", 2);
    monitorBox_.addItem("Delta", 3);
    addAndMakeVisible(monitorBox_);

    freezeButton_.setButtonText("FREEZE");
    addAndMakeVisible(freezeButton_);

    auto& apvts = processor_.getAPVTS();
    amountAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kAmount, amountSlider_);
    attackAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kAttack, attackSlider_);
    releaseAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kRelease, releaseSlider_);
    thresholdAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kThreshold, thresholdSlider_);
    lowCutAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kLowCut, lowCutSlider_);
    highCutAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kHighCut, highCutSlider_);
    mixAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kMix, mixSlider_);
    outputAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kOutput, outputSlider_);
    ceilingAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kCeiling, ceilingSlider_);
    linkChannelAttachment_ = std::make_unique<SliderAttachment>(apvts, spaceduck::Param::kLinkChannel, linkChannelSlider_);
    freezeAttachment_ = std::make_unique<ButtonAttachment>(apvts, spaceduck::Param::kFreeze, freezeButton_);
    monitorAttachment_ = std::make_unique<ComboAttachment>(apvts, spaceduck::Param::kMonitor, monitorBox_);
    stereoAttachment_ = std::make_unique<ComboAttachment>(apvts, spaceduck::Param::kStereo, stereoBox_);
    modeAttachment_ = std::make_unique<ComboAttachment>(apvts, spaceduck::Param::kMode, modeBox_);

    startTimerHz(30); // live display refresh
    timerCallback();
}

SpaceDuckEditor::~SpaceDuckEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SpaceDuckEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    // Soft indigo glow from the top centre, fading to near-black.
    juce::ColourGradient glow(kBackgroundGlow.withAlpha(0.9f),
                              kEditorWidth * 0.5f, -80.0f,
                              kBackground.withAlpha(0.0f),
                              kEditorWidth * 0.5f, 620.0f, true);
    g.setGradientFill(glow);
    g.fillRect(getLocalBounds());
    // Faint cyan wash from the bottom left.
    juce::ColourGradient wash(juce::Colour(0x1a22d3ee), -40.0f, 620.0f,
                              juce::Colour(0x0022d3ee), 260.0f, 300.0f, true);
    g.setGradientFill(wash);
    g.fillRect(getLocalBounds());
}

void SpaceDuckEditor::resized() {
    titleLabel_.setBounds(18, 8, 320, 28);
    subtitleLabel_.setBounds(18, 34, 320, 16);
    presetLabel_.setBounds(372, 10, 58, 28);
    presetBox_.setBounds(434, 8, 104, 30);
    tutorialButton_.setBounds(548, 8, 96, 30);
    statusLabel_.setBounds(340, 36, 304, 16);
    tutorialView_.setBounds(16, 56, 628, 536);

    display_.setBounds(16, 56, 628, 184);

    // Row 1: six knob slots across the width.
    constexpr int kSlotWidth = 104;
    const int slotX[6] = {16, 16 + kSlotWidth, 16 + 2 * kSlotWidth,
                          16 + 3 * kSlotWidth, 16 + 4 * kSlotWidth,
                          16 + 5 * kSlotWidth};
    const int slotCentre[6] = {slotX[0] + kSlotWidth / 2, slotX[1] + kSlotWidth / 2,
                               slotX[2] + kSlotWidth / 2, slotX[3] + kSlotWidth / 2,
                               slotX[4] + kSlotWidth / 2, slotX[5] + kSlotWidth / 2};
    amountLabel_.setBounds(slotX[0], 248, kSlotWidth, 16);
    attackLabel_.setBounds(slotX[1], 248, kSlotWidth, 16);
    releaseLabel_.setBounds(slotX[2], 248, kSlotWidth, 16);
    thresholdLabel_.setBounds(slotX[3], 248, kSlotWidth, 16);
    lowCutLabel_.setBounds(slotX[4], 248, kSlotWidth, 16);
    highCutLabel_.setBounds(slotX[5], 248, kSlotWidth, 16);
    amountSlider_.setBounds(slotCentre[0] - 60, 266, 120, 124);
    attackSlider_.setBounds(slotCentre[1] - 42, 272, 84, 114);
    releaseSlider_.setBounds(slotCentre[2] - 42, 272, 84, 114);
    thresholdSlider_.setBounds(slotCentre[3] - 42, 272, 84, 114);
    lowCutSlider_.setBounds(slotCentre[4] - 42, 272, 84, 114);
    highCutSlider_.setBounds(slotCentre[5] - 42, 272, 84, 114);

    // Row 2: Mix / Output / Ceiling knobs, then Monitor + Freeze.
    mixLabel_.setBounds(slotX[0], 398, kSlotWidth, 16);
    outputLabel_.setBounds(slotX[1], 398, kSlotWidth, 16);
    ceilingLabel_.setBounds(slotX[2], 398, kSlotWidth, 16);
    mixSlider_.setBounds(slotCentre[0] - 42, 414, 84, 112);
    outputSlider_.setBounds(slotCentre[1] - 42, 414, 84, 112);
    ceilingSlider_.setBounds(slotCentre[2] - 42, 414, 84, 112);
    monitorLabel_.setBounds(340, 420, 70, 28);
    monitorBox_.setBounds(414, 418, 120, 30);
    freezeButton_.setBounds(548, 416, 96, 32);

    // Bottom bar: input routing + stereo mode.
    modeLabel_.setBounds(16, 544, 52, 28);
    modeBox_.setBounds(72, 542, 150, 30);
    linkChannelLabel_.setBounds(238, 544, 64, 28);
    linkChannelSlider_.setBounds(306, 542, 110, 30);
    stereoLabel_.setBounds(432, 544, 60, 28);
    stereoBox_.setBounds(496, 542, 148, 30);
}

void SpaceDuckEditor::timerCallback() {
    display_.setData(processor_.getBandLevelsDb(), processor_.getBandReductionsDb());
    const float lowCut = processor_.getAPVTS()
        .getRawParameterValue(spaceduck::Param::kLowCut)->load();
    const float highCut = processor_.getAPVTS()
        .getRawParameterValue(spaceduck::Param::kHighCut)->load();
    display_.setCutRange(lowCut, highCut);
    statusLabel_.setText(processor_.getStatusText(), juce::dontSendNotification);
    // Link channel only matters in Link Master / Link Slave modes.
    const auto mode = processor_.getInputMode();
    const bool linkRelevant = mode != spaceduck::InputMode::Sidechain;
    linkChannelSlider_.setEnabled(linkRelevant);
    linkChannelLabel_.setEnabled(linkRelevant);
}

void SpaceDuckEditor::setShowingTutorial(bool show) {
    showingTutorial_ = show;
    tutorialButton_.setToggleState(show, juce::dontSendNotification);
    // Hide the main controls; the header (title, preset, TUTORIAL toggle,
    // status) stays visible so the tab can be closed from either view.
    juce::Component* mainControls[] = {
        &display_,
        &amountLabel_, &attackLabel_, &releaseLabel_, &thresholdLabel_,
        &lowCutLabel_, &highCutLabel_, &mixLabel_, &outputLabel_, &ceilingLabel_,
        &amountSlider_, &attackSlider_, &releaseSlider_, &thresholdSlider_,
        &lowCutSlider_, &highCutSlider_, &mixSlider_, &outputSlider_, &ceilingSlider_,
        &modeLabel_, &modeBox_, &linkChannelLabel_, &linkChannelSlider_,
        &stereoLabel_, &stereoBox_, &monitorLabel_, &monitorBox_, &freezeButton_,
    };
    for (auto* c : mainControls)
        c->setVisible(!show);
    tutorialView_.setVisible(show);
    repaint();
}

void SpaceDuckEditor::applyPreset(int presetId) {
    for (const auto& preset : kPresets) {
        if (preset.id != presetId)
            continue;
        setParamValue(spaceduck::Param::kAmount, preset.amount);
        setParamValue(spaceduck::Param::kAttack, preset.attack);
        setParamValue(spaceduck::Param::kRelease, preset.release);
        setParamValue(spaceduck::Param::kThreshold, preset.threshold);
        setParamValue(spaceduck::Param::kLowCut, preset.lowCut);
        setParamValue(spaceduck::Param::kHighCut, preset.highCut);
        setParamValue(spaceduck::Param::kMix, preset.mix);
        setParamValue(spaceduck::Param::kOutput, preset.output);
        setParamValue(spaceduck::Param::kCeiling, preset.ceiling);
        setParamValue(spaceduck::Param::kStereo, static_cast<float>(preset.stereoMode));
        setParamValue(spaceduck::Param::kFreeze, 0.0f);
        return;
    }
}

void SpaceDuckEditor::setParamValue(const char* paramId, float value) {
    if (auto* param = processor_.getAPVTS().getParameter(paramId))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}
