#pragma once

#include "PluginProcessor.h"

#include <deque>
#include <optional>

#include <juce_gui_extra/juce_gui_extra.h>

class OffsetGraphComponent final : public juce::Component
{
public:
    struct Sample
    {
        double projectTimeSec = 0.0;
        float errorMs = 0.0f;
        bool showPoint = false;
    };

    void pushSample(const Sample& sample);
    void clear();
    void setRunning(bool running);
    void setCurrentProjectTime(double projectTimeSec);
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    static juce::String formatProjectTime(double seconds);
    static float valueToY(float valueMs, float minMs, float maxMs, const juce::Rectangle<float>& area);
    void ensureViewportInitialized();

    std::deque<Sample> samples_;
    static constexpr std::size_t maxSamples_ = 4000;
    bool running_ = false;
    double currentProjectTimeSec_ = 0.0;
    double viewportStartSec_ = 0.0;
    double viewportWindowSec_ = 15.0;
    bool viewportInitialized_ = false;
    std::optional<double> hoverProjectTimeSec_;
};

class FindThePocketAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit FindThePocketAudioProcessorEditor(FindThePocketAudioProcessor&);
    ~FindThePocketAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String formatSignedMs(float value);

    FindThePocketAudioProcessor& processor_;

    juce::Label titleLabel_;
    juce::Slider sensitivitySlider_;
    juce::ComboBox pocketModeBox_;
    juce::ComboBox runDurationBox_;
    juce::TextButton calibrateButton_ { "Calibrate" };

    juce::Label meterLabel_;
    juce::Label lastErrorLabel_;
    juce::Label lastPointsLabel_;
    juce::Label totalScoreLabel_;
    juce::Label hitsLabel_;
    juce::Label beatsLabel_;
    juce::Label timeLeftLabel_;
    juce::Label hitsLeftLabel_;
    juce::Label calibrationLabel_;

    OffsetGraphComponent offsetGraph_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment_;
    std::unique_ptr<ComboAttachment> pocketAttachment_;
    std::unique_ptr<ComboAttachment> durationAttachment_;
    bool wasRunning_ = false;
};
