#include "PluginEditor.h"

#include <array>
#include <cmath>
#include <vector>

void OffsetGraphComponent::pushSample(const Sample& sample)
{
    samples_.push_back(sample);
    while (samples_.size() > maxSamples_)
        samples_.pop_front();
    if (running_)
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
        viewportInitialized_ = true;
    }
    repaint();
}

void OffsetGraphComponent::clear()
{
    samples_.clear();
    hoverProjectTimeSec_.reset();
    viewportInitialized_ = false;
    viewportStartSec_ = 0.0;
    repaint();
}

void OffsetGraphComponent::setRunning(bool running)
{
    running_ = running;
    if (running_)
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
        viewportInitialized_ = true;
    }
}

void OffsetGraphComponent::setCurrentProjectTime(double projectTimeSec)
{
    currentProjectTimeSec_ = projectTimeSec;
    if (running_)
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
        viewportInitialized_ = true;
        repaint();
    }
}

void OffsetGraphComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = bounds.reduced(8.0f);
    constexpr float graphMinMs = -35.0f;
    constexpr float graphMaxMs = 35.0f;
    constexpr int leftLabelPad = 62;
    constexpr float tightMs = 5.0f;
    constexpr float borderlineMs = 10.0f;
    constexpr float looseMs = 20.0f;
    constexpr float sloppyMs = 30.0f;
    const auto graphArea = area.withTrimmedLeft(static_cast<float>(leftLabelPad));

    g.fillAll(juce::Colour(0xff17181f));

    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    ensureViewportInitialized();
    const auto startSec = viewportStartSec_;
    const auto endSec = startSec + viewportWindowSec_;
    const auto spanSec = std::max(0.001, endSec - startSec);

    const auto fillBand = [&](float minMs, float maxMs, juce::Colour colour)
    {
        const auto top = valueToY(maxMs, graphMinMs, graphMaxMs, graphArea);
        const auto bottom = valueToY(minMs, graphMinMs, graphMaxMs, graphArea);
        const auto band = juce::Rectangle<float>(graphArea.getX(), std::min(top, bottom), graphArea.getWidth(), std::abs(bottom - top));
        g.setColour(colour);
        g.fillRect(band);
    };

    fillBand(-looseMs, -borderlineMs, juce::Colour(0xd25256).withAlpha(0.16f));
    fillBand(borderlineMs, looseMs, juce::Colour(0xd25256).withAlpha(0.16f));
    fillBand(-borderlineMs, -tightMs, juce::Colour(0xe6b652).withAlpha(0.14f));
    fillBand(tightMs, borderlineMs, juce::Colour(0xe6b652).withAlpha(0.14f));
    fillBand(-tightMs, tightMs, juce::Colour(0x62c28e).withAlpha(0.16f));
    fillBand(-sloppyMs, -looseMs, juce::Colour(0x9aa6b9).withAlpha(0.08f));
    fillBand(looseMs, sloppyMs, juce::Colour(0x9aa6b9).withAlpha(0.08f));

    g.setFont(11.0f);
    for (int line = -30; line <= 30; line += 10)
    {
        const auto y = valueToY(static_cast<float>(line), graphMinMs, graphMaxMs, graphArea);
        const auto isZero = line == 0;
        g.setColour(isZero ? juce::Colour(0x84ecb0).withAlpha(0.95f) : juce::Colours::white.withAlpha(0.16f));
        g.drawLine(graphArea.getX(), y, graphArea.getRight(), y, isZero ? 1.9f : 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.drawText(juce::String(line > 0 ? "+" : "") + juce::String(line) + " ms",
                   juce::Rectangle<int>(static_cast<int>(area.getX()),
                                        static_cast<int>(y - 8.0f),
                                        leftLabelPad - 8,
                                        16),
                   juce::Justification::centredRight);
    }
    for (float line : { -5.0f, 5.0f })
    {
        const auto y = valueToY(line, graphMinMs, graphMaxMs, graphArea);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawLine(graphArea.getX(), y, graphArea.getRight(), y, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.66f));
        g.drawText(juce::String(line > 0 ? "+" : "") + juce::String(static_cast<int>(line)) + " ms",
                   juce::Rectangle<int>(static_cast<int>(area.getX()),
                                        static_cast<int>(y - 8.0f),
                                        leftLabelPad - 8,
                                        16),
                   juce::Justification::centredRight);
    }

    g.setFont(10.0f);
    g.setColour(juce::Colour(0xd25256).withAlpha(0.75f));
    g.drawText("loose", juce::Rectangle<int>(static_cast<int>(graphArea.getX() + 6.0f), static_cast<int>(valueToY(-looseMs, graphMinMs, graphMaxMs, graphArea) - 14.0f), 70, 12), juce::Justification::left);
    g.setColour(juce::Colour(0x9aa6b9).withAlpha(0.68f));
    g.drawText("sloppy", juce::Rectangle<int>(static_cast<int>(graphArea.getX() + 6.0f), static_cast<int>(valueToY(-sloppyMs, graphMinMs, graphMaxMs, graphArea) - 14.0f), 70, 12), juce::Justification::left);
    g.setColour(juce::Colour(0xe6b652).withAlpha(0.72f));
    g.drawText("borderline", juce::Rectangle<int>(static_cast<int>(graphArea.getX() + 6.0f), static_cast<int>(valueToY(-borderlineMs, graphMinMs, graphMaxMs, graphArea) - 14.0f), 88, 12), juce::Justification::left);
    g.setColour(juce::Colour(0x62c28e).withAlpha(0.66f));
    g.drawText("tight", juce::Rectangle<int>(static_cast<int>(graphArea.getX() + 6.0f), static_cast<int>(valueToY(-tightMs, graphMinMs, graphMaxMs, graphArea) - 14.0f), 70, 12), juce::Justification::left);

    juce::Path path;
    bool hasPath = false;
    std::vector<juce::Point<float>> points;
    points.reserve(samples_.size());
    for (const auto& sample : samples_)
    {
        if (sample.projectTimeSec < startSec || sample.projectTimeSec > endSec)
            continue;
        const auto x = graphArea.getX() + static_cast<float>(((sample.projectTimeSec - startSec) / spanSec) * graphArea.getWidth());
        const auto y = valueToY(sample.errorMs, graphMinMs, graphMaxMs, graphArea);
        if (!hasPath)
        {
            path.startNewSubPath(x, y);
            hasPath = true;
        }
        else
        {
            path.lineTo(x, y);
        }
        if (sample.showPoint)
            points.emplace_back(x, y);
    }

    if (hasPath)
    {
        g.setColour(juce::Colour(0xffffcd54));
        g.strokePath(path, juce::PathStrokeType(2.2f));
        for (const auto& point : points)
            g.fillEllipse(point.x - 2.2f, point.y - 2.2f, 4.4f, 4.4f);
    }

    if (hoverProjectTimeSec_.has_value() && *hoverProjectTimeSec_ >= startSec && *hoverProjectTimeSec_ <= endSec)
    {
        const auto x = graphArea.getX() + static_cast<float>(((*hoverProjectTimeSec_ - startSec) / spanSec) * graphArea.getWidth());
        g.setColour(juce::Colours::white.withAlpha(0.44f));
        g.drawLine(x, graphArea.getY(), x, graphArea.getBottom(), 1.1f);

        const auto text = "t " + formatProjectTime(*hoverProjectTimeSec_);
        const auto textWidth = 120;
        const auto textX = juce::jlimit<int>(static_cast<int>(graphArea.getX()),
                                             static_cast<int>(graphArea.getRight()) - textWidth,
                                             static_cast<int>(x) + 8);
        g.setColour(juce::Colours::black.withAlpha(0.65f));
        g.fillRoundedRectangle(static_cast<float>(textX), graphArea.getY() + 6.0f, static_cast<float>(textWidth), 18.0f, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText(text, juce::Rectangle<int>(textX, static_cast<int>(graphArea.getY() + 6.0f), textWidth, 18), juce::Justification::centred);
    }
}

void OffsetGraphComponent::mouseMove(const juce::MouseEvent& event)
{
    ensureViewportInitialized();
    const auto area = getLocalBounds().toFloat().reduced(8.0f).withTrimmedLeft(62.0f);
    if (!area.contains(event.position))
    {
        hoverProjectTimeSec_.reset();
        repaint();
        return;
    }
    const auto xNorm = juce::jlimit(0.0, 1.0, static_cast<double>((event.position.x - area.getX()) / area.getWidth()));
    hoverProjectTimeSec_ = viewportStartSec_ + xNorm * viewportWindowSec_;
    repaint();
}

void OffsetGraphComponent::mouseExit(const juce::MouseEvent&)
{
    hoverProjectTimeSec_.reset();
    repaint();
}

void OffsetGraphComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (running_ || samples_.empty())
        return;
    ensureViewportInitialized();

    const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    if (std::abs(delta) < 0.001f)
        return;

    const auto shiftSec = static_cast<double>(delta) * viewportWindowSec_ * 0.22;
    const auto firstTime = samples_.front().projectTimeSec;
    const auto lastTime = samples_.back().projectTimeSec;
    const auto maxStart = std::max(firstTime, lastTime - viewportWindowSec_);
    viewportStartSec_ = juce::jlimit(firstTime, maxStart, viewportStartSec_ + shiftSec);
    repaint();
}

juce::String OffsetGraphComponent::formatProjectTime(double seconds)
{
    const auto safe = std::max(0.0, seconds);
    const auto totalMs = static_cast<int64_t>(std::llround(safe * 1000.0));
    const auto mins = totalMs / 60000;
    const auto secs = (totalMs / 1000) % 60;
    const auto ms = totalMs % 1000;
    return juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2) + "." + juce::String(ms).paddedLeft('0', 3);
}

float OffsetGraphComponent::valueToY(float valueMs, float minMs, float maxMs, const juce::Rectangle<float>& area)
{
    const auto clamped = juce::jlimit(minMs, maxMs, valueMs);
    const auto normalized = (clamped - minMs) / (maxMs - minMs);
    return area.getBottom() - normalized * area.getHeight();
}

void OffsetGraphComponent::ensureViewportInitialized()
{
    if (viewportInitialized_)
        return;
    if (!samples_.empty())
    {
        const auto end = samples_.back().projectTimeSec;
        viewportStartSec_ = std::max(0.0, end - viewportWindowSec_);
    }
    else
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
    }
    viewportInitialized_ = true;
}

FindThePocketAudioProcessorEditor::FindThePocketAudioProcessorEditor(FindThePocketAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(900, 600);

    titleLabel_.setText("Find The Pocket (VST3)", juce::dontSendNotification);
    addAndMakeVisible(titleLabel_);

    sensitivitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivitySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
    sensitivitySlider_.setRange(0.0, 1.0, 0.01);
    addAndMakeVisible(sensitivitySlider_);

    pocketModeBox_.addItemList({ "Strict", "Laid-back", "Push" }, 1);
    runDurationBox_.addItemList({ "30 sec", "1 min", "3 min", "5 min", "10 min" }, 1);
    addAndMakeVisible(pocketModeBox_);
    addAndMakeVisible(runDurationBox_);

    addAndMakeVisible(calibrateButton_);

    calibrateButton_.onClick = [this] { processor_.startCalibration(); };

    addAndMakeVisible(offsetGraph_);

    for (auto* label : { &meterLabel_,
                         &lastErrorLabel_,
                         &lastPointsLabel_,
                         &totalScoreLabel_,
                         &hitsLabel_,
                         &beatsLabel_,
                         &timeLeftLabel_,
                         &hitsLeftLabel_,
                         &calibrationLabel_ })
    {
        label->setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }

    sensitivityAttachment_ = std::make_unique<SliderAttachment>(processor_.parameters(), "sensitivity", sensitivitySlider_);
    pocketAttachment_ = std::make_unique<ComboAttachment>(processor_.parameters(), "pocketMode", pocketModeBox_);
    durationAttachment_ = std::make_unique<ComboAttachment>(processor_.parameters(), "runDuration", runDurationBox_);

    startTimerHz(30);
}

void FindThePocketAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101116));
}

void FindThePocketAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(14);
    titleLabel_.setBounds(area.removeFromTop(34));
    area.removeFromTop(6);

    auto controlRow = area.removeFromTop(30);
    sensitivitySlider_.setBounds(controlRow.removeFromLeft(320));
    controlRow.removeFromLeft(8);
    pocketModeBox_.setBounds(controlRow.removeFromLeft(150));
    controlRow.removeFromLeft(8);
    runDurationBox_.setBounds(controlRow.removeFromLeft(130));
    controlRow.removeFromLeft(8);
    calibrateButton_.setBounds(controlRow.removeFromLeft(90));

    area.removeFromTop(8);
    auto topStats = area.removeFromTop(50);
    meterLabel_.setBounds(topStats.removeFromLeft(160));
    lastErrorLabel_.setBounds(topStats.removeFromLeft(150));
    lastPointsLabel_.setBounds(topStats.removeFromLeft(130));
    totalScoreLabel_.setBounds(topStats.removeFromLeft(180));
    hitsLabel_.setBounds(topStats.removeFromLeft(110));
    beatsLabel_.setBounds(topStats.removeFromLeft(110));
    timeLeftLabel_.setBounds(topStats.removeFromLeft(120));
    hitsLeftLabel_.setBounds(topStats.removeFromLeft(120));

    area.removeFromTop(6);
    calibrationLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    offsetGraph_.setBounds(area);
}

void FindThePocketAudioProcessorEditor::timerCallback()
{
    std::array<FindThePocketAudioProcessor::HitTelemetry, 128> hits {};
    const auto count = processor_.drainRecentHits(hits.data(), static_cast<int>(hits.size()));
    for (int i = 0; i < count; ++i)
    {
        if (hits[static_cast<std::size_t>(i)].plottable)
        {
            OffsetGraphComponent::Sample sample;
            sample.projectTimeSec = hits[static_cast<std::size_t>(i)].projectTimeSec;
            sample.errorMs = hits[static_cast<std::size_t>(i)].errorMs;
            sample.showPoint = true;
            offsetGraph_.pushSample(sample);
        }
    }

    const auto snapshot = processor_.getUiSnapshot();
    if (snapshot.isRunning && !wasRunning_)
        offsetGraph_.clear();
    wasRunning_ = snapshot.isRunning;
    offsetGraph_.setRunning(snapshot.isRunning);
    offsetGraph_.setCurrentProjectTime(snapshot.projectTimeSec);
    meterLabel_.setText("Input: " + juce::String(snapshot.inputLevelPercent, 1) + "%", juce::dontSendNotification);
    lastErrorLabel_.setText("Last: " + formatSignedMs(snapshot.lastErrorMs), juce::dontSendNotification);
    lastPointsLabel_.setText("Points: " + juce::String(snapshot.lastPoints, 3), juce::dontSendNotification);
    totalScoreLabel_.setText("Score: " + juce::String(snapshot.totalScorePercent, 1), juce::dontSendNotification);
    hitsLabel_.setText("Hits: " + juce::String(snapshot.hitCount), juce::dontSendNotification);
    beatsLabel_.setText("Beats: " + juce::String(snapshot.beatCount), juce::dontSendNotification);
    timeLeftLabel_.setText("Time: " + juce::String(snapshot.timeLeftSec) + "s", juce::dontSendNotification);
    hitsLeftLabel_.setText("Left: " + juce::String(snapshot.hitsLeft), juce::dontSendNotification);

    juce::String state = "Idle";
    if (snapshot.isCalibrating)
        state = "Calibrating";
    else if (snapshot.isRecording)
        state = "Recording (host)";
    else if (snapshot.isRunning)
        state = "Running";
    calibrationLabel_.setText(state + " | cal samples: " + juce::String(snapshot.calibrationSamples)
                                  + " | offset " + juce::String(snapshot.calibrationOffsetMs, 1) + " ms"
                                  + " | jitter " + juce::String(snapshot.calibrationJitterMs, 1) + " ms",
                              juce::dontSendNotification);
}

juce::String FindThePocketAudioProcessorEditor::formatSignedMs(float value)
{
    if (value > 0.0f)
        return "+" + juce::String(value, 1) + " ms";
    return juce::String(value, 1) + " ms";
}
