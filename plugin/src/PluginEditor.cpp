#include "PluginEditor.h"

#include <algorithm>
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
        targetViewportStartSec_ = viewportStartSec_;
        targetViewportWindowSec_ = viewportWindowSec_;
        scrollVelocitySec_ = 0.0;
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
    targetViewportStartSec_ = 0.0;
    viewportWindowSec_ = 15.0;
    targetViewportWindowSec_ = viewportWindowSec_;
    scrollVelocitySec_ = 0.0;
    isAnimatingViewport_ = false;
    stopTimer();
    repaint();
}

void OffsetGraphComponent::setRunning(bool running)
{
    running_ = running;
    if (running_)
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
        targetViewportStartSec_ = viewportStartSec_;
        targetViewportWindowSec_ = viewportWindowSec_;
        scrollVelocitySec_ = 0.0;
        isAnimatingViewport_ = false;
        stopTimer();
        viewportInitialized_ = true;
    }
}

void OffsetGraphComponent::setCurrentProjectTime(double projectTimeSec)
{
    currentProjectTimeSec_ = projectTimeSec;
    if (running_)
    {
        viewportStartSec_ = std::max(0.0, currentProjectTimeSec_ - viewportWindowSec_);
        targetViewportStartSec_ = viewportStartSec_;
        viewportInitialized_ = true;
        repaint();
    }
}

void OffsetGraphComponent::zoomY(double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    yHalfRangeMs_ *= factor;
    clampYRange();
    repaint();
}

void OffsetGraphComponent::resetYRange()
{
    yHalfRangeMs_ = defaultYHalfRangeMs_;
    clampYRange();
    repaint();
}

bool OffsetGraphComponent::autoYRange()
{
    if (samples_.empty())
        return false;

    double maxAbs = 0.0;
    for (const auto& sample : samples_)
        maxAbs = std::max(maxAbs, std::abs(static_cast<double>(sample.errorMs)));

    if (maxAbs < 0.5)
        yHalfRangeMs_ = defaultYHalfRangeMs_;
    else
    {
        const auto padded = maxAbs * 1.2;
        yHalfRangeMs_ = std::ceil(padded / 10.0) * 10.0;
    }
    clampYRange();
    repaint();
    return true;
}

bool OffsetGraphComponent::getMedianAndMadMs(double& outMedianMs, double& outMadMs) const
{
    if (samples_.empty())
        return false;

    std::vector<float> values;
    values.reserve(samples_.size());
    for (const auto& sample : samples_)
        values.push_back(sample.errorMs);

    const auto mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    auto medianValue = static_cast<double>(values[mid]);
    if ((values.size() % 2) == 0 && mid > 0)
    {
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid - 1), values.end());
        medianValue = (medianValue + static_cast<double>(values[mid - 1])) * 0.5;
    }
    outMedianMs = medianValue;

    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (const auto v : values)
        deviations.push_back(std::abs(static_cast<double>(v) - outMedianMs));

    const auto dMid = deviations.size() / 2;
    std::nth_element(deviations.begin(),
                     deviations.begin() + static_cast<std::ptrdiff_t>(dMid),
                     deviations.end());
    auto mad = deviations[dMid];
    if ((deviations.size() % 2) == 0 && dMid > 0)
    {
        std::nth_element(deviations.begin(),
                         deviations.begin() + static_cast<std::ptrdiff_t>(dMid - 1),
                         deviations.end());
        mad = (mad + deviations[dMid - 1]) * 0.5;
    }
    outMadMs = mad;
    return std::isfinite(outMedianMs) && std::isfinite(outMadMs);
}

void OffsetGraphComponent::shiftAllErrors(float deltaMs)
{
    if (!std::isfinite(deltaMs) || std::abs(deltaMs) < 0.0001f)
        return;
    for (auto& sample : samples_)
        sample.errorMs += deltaMs;
    repaint();
}

void OffsetGraphComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = bounds.reduced(8.0f);
    const float graphMinMs = static_cast<float>(-yHalfRangeMs_);
    const float graphMaxMs = static_cast<float>(yHalfRangeMs_);
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
    const auto maxLine = static_cast<int>(std::floor(yHalfRangeMs_ / 10.0)) * 10;
    for (int line = -maxLine; line <= maxLine; line += 10)
    {
        const auto y = valueToY(static_cast<float>(line), graphMinMs, graphMaxMs, graphArea);
        const auto isZero = line == 0;
        if (isZero)
        {
            g.setColour(juce::Colour(0x84ecb0).withAlpha(0.95f));
            g.drawLine(graphArea.getX(), y, graphArea.getRight(), y, 1.9f);
        }
        else if (std::abs(line) > 30)
        {
            g.setColour(juce::Colours::white.withAlpha(0.28f));
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine(juce::Line<float>(graphArea.getX(), y, graphArea.getRight(), y), dashes, 2, 1.0f);
        }
        else if (std::abs(line) == 30)
        {
            g.setColour(juce::Colours::white.withAlpha(0.36f));
            const float dashes[] = { 5.0f, 4.0f };
            g.drawDashedLine(juce::Line<float>(graphArea.getX(), y, graphArea.getRight(), y), dashes, 2, 1.2f);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.16f));
            g.drawLine(graphArea.getX(), y, graphArea.getRight(), y, 1.0f);
        }
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

    // Include one sample before and after visible range to preserve line continuity.
    int firstVisibleIndex = -1;
    int lastVisibleIndex = -1;
    for (int i = 0; i < static_cast<int>(samples_.size()); ++i)
    {
        const auto t = samples_[static_cast<std::size_t>(i)].projectTimeSec;
        if (t >= startSec && t <= endSec)
        {
            if (firstVisibleIndex < 0)
                firstVisibleIndex = i;
            lastVisibleIndex = i;
        }
    }
    if (firstVisibleIndex >= 0)
    {
        const auto drawStart = std::max(0, firstVisibleIndex - 1);
        const auto drawEnd = std::min(static_cast<int>(samples_.size()) - 1, lastVisibleIndex + 1);
        for (int i = drawStart; i <= drawEnd; ++i)
        {
            const auto& sample = samples_[static_cast<std::size_t>(i)];
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
            if (sample.showPoint && sample.projectTimeSec >= startSec && sample.projectTimeSec <= endSec)
                points.emplace_back(x, y);
        }
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

void OffsetGraphComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (running_ || samples_.empty())
        return;
    ensureViewportInitialized();

    const auto absX = std::abs(wheel.deltaX);
    const auto absY = std::abs(wheel.deltaY);
    const auto dominant = absX > absY ? wheel.deltaX : wheel.deltaY;
    if (std::abs(dominant) < 0.0005f)
        return;

    if (absX >= absY * 1.1f)
    {
        const auto shiftSec = static_cast<double>(wheel.deltaX) * targetViewportWindowSec_ * 0.35;
        targetViewportStartSec_ += shiftSec;
        scrollVelocitySec_ += shiftSec * 0.18;
    }
    else
    {
        const auto oldWindow = targetViewportWindowSec_;
        const auto zoomFactor = std::exp(-static_cast<double>(wheel.deltaY) * 0.6);
        targetViewportWindowSec_ = juce::jlimit(3.0, 120.0, oldWindow * zoomFactor);

        const auto area = getLocalBounds().toFloat().reduced(8.0f).withTrimmedLeft(62.0f);
        const auto xNorm = area.getWidth() > 1.0f
            ? juce::jlimit(0.0, 1.0, static_cast<double>((event.position.x - area.getX()) / area.getWidth()))
            : 0.5;
        const auto anchor = targetViewportStartSec_ + xNorm * oldWindow;
        targetViewportStartSec_ = anchor - xNorm * targetViewportWindowSec_;
    }

    clampViewport();
    startViewportAnimation();
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
    targetViewportStartSec_ = viewportStartSec_;
    targetViewportWindowSec_ = viewportWindowSec_;
    viewportInitialized_ = true;
}

void OffsetGraphComponent::clampViewport()
{
    if (samples_.empty())
    {
        targetViewportStartSec_ = std::max(0.0, targetViewportStartSec_);
        return;
    }
    const auto firstTime = samples_.front().projectTimeSec;
    const auto lastTime = samples_.back().projectTimeSec;
    const auto maxStart = std::max(firstTime, lastTime - targetViewportWindowSec_);
    targetViewportStartSec_ = juce::jlimit(firstTime, maxStart, targetViewportStartSec_);
}

void OffsetGraphComponent::clampYRange()
{
    yHalfRangeMs_ = juce::jlimit(minYHalfRangeMs_, maxYHalfRangeMs_, yHalfRangeMs_);
}

void OffsetGraphComponent::startViewportAnimation()
{
    if (isAnimatingViewport_)
        return;
    isAnimatingViewport_ = true;
    startTimerHz(60);
}

void OffsetGraphComponent::timerCallback()
{
    if (running_)
    {
        stopTimer();
        isAnimatingViewport_ = false;
        return;
    }

    clampViewport();

    const auto distance = targetViewportStartSec_ - viewportStartSec_;
    scrollVelocitySec_ += distance * 0.22;
    scrollVelocitySec_ *= 0.72;
    viewportStartSec_ += scrollVelocitySec_;

    const auto zoomDelta = targetViewportWindowSec_ - viewportWindowSec_;
    viewportWindowSec_ += zoomDelta * 0.28;

    if (!samples_.empty())
    {
        const auto firstTime = samples_.front().projectTimeSec;
        const auto lastTime = samples_.back().projectTimeSec;
        const auto maxStart = std::max(firstTime, lastTime - viewportWindowSec_);
        viewportStartSec_ = juce::jlimit(firstTime, maxStart, viewportStartSec_);
    }

    repaint();

    if (std::abs(distance) < 0.0008 &&
        std::abs(scrollVelocitySec_) < 0.0008 &&
        std::abs(zoomDelta) < 0.0015)
    {
        viewportStartSec_ = targetViewportStartSec_;
        viewportWindowSec_ = targetViewportWindowSec_;
        scrollVelocitySec_ = 0.0;
        stopTimer();
        isAnimatingViewport_ = false;
        repaint();
    }
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
    addAndMakeVisible(yZoomOutButton_);
    addAndMakeVisible(yRangeLabel_);
    addAndMakeVisible(yZoomInButton_);
    addAndMakeVisible(yResetButton_);
    addAndMakeVisible(yAutoButton_);

    calibrateButton_.onClick = [this]
    {
        double medianMs = 0.0;
        double madMs = 0.0;
        if (!offsetGraph_.getMedianAndMadMs(medianMs, madMs))
            return;

        const auto jitterMs = juce::jlimit(0.5, 25.0, madMs * 2.0);
        // Nudge processor calibration offset and update jitter from observed spread.
        processor_.applyCalibrationEstimate(medianMs, jitterMs);
        // Shift currently displayed history for immediate visual feedback.
        offsetGraph_.shiftAllErrors(static_cast<float>(-medianMs));
    };
    yZoomOutButton_.onClick = [this]
    {
        offsetGraph_.zoomY(1.25);
        updateYRangeLabel();
    };
    yZoomInButton_.onClick = [this]
    {
        offsetGraph_.zoomY(0.8);
        updateYRangeLabel();
    };
    yResetButton_.onClick = [this]
    {
        offsetGraph_.resetYRange();
        updateYRangeLabel();
    };
    yAutoButton_.onClick = [this]
    {
        if (offsetGraph_.autoYRange())
            updateYRangeLabel();
    };

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

    updateYRangeLabel();
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
    controlRow.removeFromLeft(10);
    yZoomOutButton_.setBounds(controlRow.removeFromLeft(44));
    controlRow.removeFromLeft(4);
    yRangeLabel_.setBounds(controlRow.removeFromLeft(96));
    controlRow.removeFromLeft(4);
    yZoomInButton_.setBounds(controlRow.removeFromLeft(44));
    controlRow.removeFromLeft(4);
    yResetButton_.setBounds(controlRow.removeFromLeft(72));
    controlRow.removeFromLeft(4);
    yAutoButton_.setBounds(controlRow.removeFromLeft(68));

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
        OffsetGraphComponent::Sample sample;
        sample.projectTimeSec = hits[static_cast<std::size_t>(i)].projectTimeSec;
        sample.errorMs = hits[static_cast<std::size_t>(i)].errorMs;
        sample.showPoint = true;
        offsetGraph_.pushSample(sample);
    }

    const auto snapshot = processor_.getUiSnapshot();
    if (snapshot.runEpoch != lastRunEpoch_)
    {
        offsetGraph_.clear();
        lastRunEpoch_ = snapshot.runEpoch;
    }
    else if (snapshot.isRunning && !wasRunning_)
    {
        offsetGraph_.clear();
    }
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

void FindThePocketAudioProcessorEditor::updateYRangeLabel()
{
    yRangeLabel_.setText("Y: +/-" + juce::String(static_cast<int>(std::llround(offsetGraph_.yHalfRangeMs()))) + " ms",
                         juce::dontSendNotification);
    yRangeLabel_.setJustificationType(juce::Justification::centred);
    yRangeLabel_.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
}
