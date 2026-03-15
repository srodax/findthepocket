#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

FindThePocketAudioProcessor::FindThePocketAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::mono(), true)
                         .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      parameters_(*this, nullptr, "PARAMS", createParameterLayout()),
      calibration_(24)
{
    loadGlobalCalibration();
}

void FindThePocketAudioProcessor::prepareToPlay(double sampleRate, int)
{
    onsetDetector_.prepare(sampleRate);
    fallbackSamplePosition_ = 0;
}

void FindThePocketAudioProcessor::releaseResources()
{
}

bool FindThePocketAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    return layouts.getMainInputChannelSet() == mono && layouts.getMainOutputChannelSet() == mono;
}

void FindThePocketAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalInputChannels = getTotalNumInputChannels();
    auto totalOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalInputChannels; i < totalOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const auto* sensitivityParam = parameters_.getRawParameterValue("sensitivity");
    const auto* pocketModeParam = parameters_.getRawParameterValue("pocketMode");
    const auto* runDurationParam = parameters_.getRawParameterValue("runDuration");

    const auto sensitivity = sensitivityParam != nullptr ? sensitivityParam->load() : 0.8f;
    const auto pocketModeChoice = pocketModeParam != nullptr ? static_cast<int>(std::lround(pocketModeParam->load())) : 0;
    const auto runChoice = runDurationParam != nullptr ? static_cast<int>(std::lround(runDurationParam->load())) : 1;

    const auto pendingNudge = pendingCalibrationNudgeMs_.exchange(0.0);
    const auto pendingJitter = pendingCalibrationJitterMs_.exchange(std::numeric_limits<double>::quiet_NaN());
    if (std::abs(pendingNudge) > 0.0001 || std::isfinite(pendingJitter))
    {
        const auto nextOffset = static_cast<double>(calibrationOffsetMs_.load()) + pendingNudge;
        const auto nextJitter = std::isfinite(pendingJitter)
            ? pendingJitter
            : static_cast<double>(calibrationJitterMs_.load());
        scoring_.setCalibration(nextOffset, nextJitter);
        calibrationOffsetMs_.store(static_cast<float>(nextOffset));
        calibrationJitterMs_.store(static_cast<float>(nextJitter));
        saveGlobalCalibration();
    }

    scoring_.setPocketMode(pocketModeFromChoice(pocketModeChoice));
    session_.setRunDurationSeconds(runDurationFromChoice(runChoice));

    const auto timing = transportSync_.read(getPlayHead());
    recording_.store(timing.valid && timing.isRecording);
    if (timing.valid)
        projectTimeSec_.store(timing.projectTimeStartSec);
    const auto transportActive = timing.valid && (timing.isPlaying || timing.isRecording);

    if (!calibrating_.load())
    {
        if (timing.valid && timing.isRecording)
        {
            const auto recordingRisingEdge = !previousRecording_;
            if ((canAutoStartOnRecord_ || recordingRisingEdge) && !running_.load())
            {
                startRun(timing.projectTimeStartSec);
                canAutoStartOnRecord_ = false;
            }
        }
        else
        {
            if (running_.load())
                stopSession();
            canAutoStartOnRecord_ = true;
        }
    }
    previousRecording_ = timing.valid && timing.isRecording;

    const auto bpm = timing.valid ? timing.bpm : 120.0;
    const auto beatIntervalMs = 60000.0 / std::max(1.0, bpm);
    session_.setBeatIntervalMs(beatIntervalMs);

    onsetDetector_.setSensitivity(sensitivity);
    onsetDetector_.setGainMultiplier(1.0f + sensitivity * 4.0f);
    onsetDetector_.setBeatIntervalMs(beatIntervalMs);
    onsetDetector_.setDetectEnabled((running_.load() || calibrating_.load()) && transportActive);

    if (running_.load() && transportActive)
    {
        const auto beats = transportSync_.countBeatsCrossed(timing, buffer.getNumSamples(), getSampleRate());
        for (int i = 0; i < beats; ++i)
            scoring_.incrementBeatCount();
        beatCount_.store(static_cast<int>(scoring_.summary().beatCount));
    }

    const auto* input = buffer.getReadPointer(0);
    if (input != nullptr)
    {
        double sumSquares = 0.0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += static_cast<double>(input[i]) * static_cast<double>(input[i]);
        const auto rms = std::sqrt(sumSquares / std::max(1, buffer.getNumSamples()));
        meterPercent_.store(static_cast<float>(juce::jlimit(0.0, 100.0, rms * 300.0)));
    }

    const auto blockStartSample = timing.valid ? timing.timeInSamples : fallbackSamplePosition_;
    if (lastHostTimeInSamples_ >= 0 && blockStartSample < lastHostTimeInSamples_)
    {
        // Transport seek/rewind: reset absolute-sample-based onset cooldown state.
        onsetDetector_.reset();
    }
    lastHostTimeInSamples_ = blockStartSample;
    fallbackSamplePosition_ = blockStartSample + buffer.getNumSamples();

    std::array<ftp::dsp::OnsetEvent, 64> onsetEvents {};
    const auto eventCount = onsetDetector_.process(input,
                                                   buffer.getNumSamples(),
                                                   blockStartSample,
                                                   onsetEvents.data(),
                                                   onsetEvents.size());

    for (std::size_t i = 0; i < eventCount; ++i)
    {
        const auto match = transportSync_.matchToGrid(timing, onsetEvents[i].audioTimeSec, getSampleRate(), 1.0);
        if (!match.valid)
            continue;

        if (calibrating_.load())
        {
            if (calibration_.addSample(match.signedErrorMs, beatIntervalMs))
                calibrationSamples_.store(static_cast<int>(calibration_.currentSampleCount()));

            if (calibration_.isComplete())
            {
                const auto c = calibration_.finalize();
                scoring_.setCalibration(c.offsetMs, c.jitterMs);
                calibrationOffsetMs_.store(static_cast<float>(c.offsetMs));
                calibrationJitterMs_.store(static_cast<float>(c.jitterMs));
                calibrationSamples_.store(0);
                calibrating_.store(false);
                session_.stop();
            }
            continue;
        }

        if (!running_.load())
            continue;

        const auto hit = scoring_.processHit(match.signedErrorMs);
        lastErrorMs_.store(static_cast<float>(hit.correctedErrorMs));
        lastPoints_.store(static_cast<float>(hit.points));

        const auto summary = scoring_.summary();
        totalScorePercent_.store(static_cast<float>(summary.normalizedPercent));
        sessionAccuracy_.store(static_cast<float>(summary.sessionAccuracy));
        sessionConsistency_.store(static_cast<float>(summary.sessionConsistency));
        sessionStability_.store(static_cast<float>(summary.sessionStability));
        sessionPocket_.store(static_cast<float>(summary.sessionPocket));
        hitCount_.store(static_cast<int>(summary.hitCount));
        beatCount_.store(static_cast<int>(summary.beatCount));

        HitTelemetry telemetry;
        telemetry.audioTimeSec = onsetEvents[i].audioTimeSec;
        telemetry.projectTimeSec = match.projectTimeSec;
        telemetry.errorMs = static_cast<float>(hit.correctedErrorMs);
        telemetry.points = static_cast<float>(hit.points);
        telemetry.plottable = true;
        pushHitTelemetry(telemetry);
    }

    const auto nowSec = timing.valid ? (static_cast<double>(timing.timeInSamples) / getSampleRate())
                                     : (static_cast<double>(fallbackSamplePosition_) / getSampleRate());
    updateRemainingFromSession(nowSec);
    if (session_.shouldAutoStop(nowSec))
    {
        session_.stop();
        running_.store(false);
    }
}

juce::AudioProcessorEditor* FindThePocketAudioProcessor::createEditor()
{
    return new FindThePocketAudioProcessorEditor(*this);
}

bool FindThePocketAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String FindThePocketAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FindThePocketAudioProcessor::acceptsMidi() const
{
    return false;
}

bool FindThePocketAudioProcessor::producesMidi() const
{
    return false;
}

bool FindThePocketAudioProcessor::isMidiEffect() const
{
    return false;
}

double FindThePocketAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FindThePocketAudioProcessor::getNumPrograms()
{
    return 1;
}

int FindThePocketAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FindThePocketAudioProcessor::setCurrentProgram(int)
{
}

const juce::String FindThePocketAudioProcessor::getProgramName(int)
{
    return {};
}

void FindThePocketAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void FindThePocketAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters_.copyState();
    state.setProperty("calibrationOffsetMs", calibrationOffsetMs_.load(), nullptr);
    state.setProperty("calibrationJitterMs", calibrationJitterMs_.load(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FindThePocketAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (!xmlState->hasTagName(parameters_.state.getType()))
        return;

    parameters_.replaceState(juce::ValueTree::fromXml(*xmlState));
    const auto offset = static_cast<double>(parameters_.state.getProperty("calibrationOffsetMs", 0.0f));
    const auto jitter = static_cast<double>(parameters_.state.getProperty("calibrationJitterMs", 5.0f));
    scoring_.setCalibration(offset, jitter);
    calibrationOffsetMs_.store(static_cast<float>(offset));
    calibrationJitterMs_.store(static_cast<float>(jitter));
    saveGlobalCalibration();
}

void FindThePocketAudioProcessor::startRun(double nowSec)
{
    if (running_.load())
        return;
    scoring_.resetSession();
    calibrationSamples_.store(0);
    running_.store(true);
    calibrating_.store(false);
    onsetDetector_.reset();
    session_.startRun(nowSec);
    runEpoch_.fetch_add(1);
}

void FindThePocketAudioProcessor::startCalibration()
{
    // Legacy calibration mode kept for compatibility; UI now uses offset nudge calibration.
    calibration_.reset();
    calibrationSamples_.store(0);
    running_.store(false);
    calibrating_.store(true);
    session_.startCalibration();
}

void FindThePocketAudioProcessor::stopSession()
{
    if (!running_.load() && !calibrating_.load())
        return;
    session_.stop();
    running_.store(false);
    calibrating_.store(false);
    onsetDetector_.reset();
}

void FindThePocketAudioProcessor::nudgeCalibrationOffsetMs(double deltaMs) noexcept
{
    if (!std::isfinite(deltaMs))
        return;
    auto current = pendingCalibrationNudgeMs_.load();
    while (!pendingCalibrationNudgeMs_.compare_exchange_weak(current, current + deltaMs))
    {
    }
}

void FindThePocketAudioProcessor::applyCalibrationEstimate(double offsetDeltaMs, double jitterMs) noexcept
{
    nudgeCalibrationOffsetMs(offsetDeltaMs);
    if (std::isfinite(jitterMs))
        pendingCalibrationJitterMs_.store(jitterMs);
}

FindThePocketAudioProcessor::UiSnapshot FindThePocketAudioProcessor::getUiSnapshot() const noexcept
{
    UiSnapshot out;
    out.inputLevelPercent = meterPercent_.load();
    out.lastErrorMs = lastErrorMs_.load();
    out.lastPoints = lastPoints_.load();
    out.totalScorePercent = totalScorePercent_.load();
    out.sessionAccuracy = sessionAccuracy_.load();
    out.sessionConsistency = sessionConsistency_.load();
    out.sessionStability = sessionStability_.load();
    out.sessionPocket = sessionPocket_.load();
    out.hitCount = hitCount_.load();
    out.beatCount = beatCount_.load();
    out.timeLeftSec = timeLeftSec_.load();
    out.hitsLeft = hitsLeft_.load();
    out.isRunning = running_.load();
    out.isCalibrating = calibrating_.load();
    out.isRecording = recording_.load();
    out.projectTimeSec = projectTimeSec_.load();
    out.runEpoch = runEpoch_.load();
    out.calibrationSamples = calibrationSamples_.load();
    out.calibrationOffsetMs = calibrationOffsetMs_.load();
    out.calibrationJitterMs = calibrationJitterMs_.load();
    return out;
}

int FindThePocketAudioProcessor::drainRecentHits(HitTelemetry* dst, int maxItems) noexcept
{
    if (dst == nullptr || maxItems <= 0)
        return 0;

    int start1, size1, start2, size2;
    telemetryFifo_.prepareToRead(maxItems, start1, size1, start2, size2);
    const auto total = size1 + size2;
    for (int i = 0; i < size1; ++i)
        dst[i] = telemetryData_[static_cast<std::size_t>(start1 + i)];
    for (int i = 0; i < size2; ++i)
        dst[size1 + i] = telemetryData_[static_cast<std::size_t>(start2 + i)];
    telemetryFifo_.finishedRead(total);
    return total;
}

juce::AudioProcessorValueTreeState::ParameterLayout FindThePocketAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity",
                                                                 "Sensitivity",
                                                                 juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                                                                 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("pocketMode",
                                                                  "Pocket Mode",
                                                                  juce::StringArray{ "Strict", "Laid-back", "Push" },
                                                                  0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("runDuration",
                                                                  "Run Duration",
                                                                  juce::StringArray{ "30 sec", "1 min", "3 min", "5 min", "10 min" },
                                                                  1));
    return { params.begin(), params.end() };
}

double FindThePocketAudioProcessor::runDurationFromChoice(int choiceIdx)
{
    switch (choiceIdx)
    {
        case 0: return 30.0;
        case 1: return 60.0;
        case 2: return 180.0;
        case 3: return 300.0;
        case 4: return 600.0;
        default: return 60.0;
    }
}

ftp::core::PocketMode FindThePocketAudioProcessor::pocketModeFromChoice(int choiceIdx)
{
    switch (choiceIdx)
    {
        case 1: return ftp::core::PocketMode::laidBack;
        case 2: return ftp::core::PocketMode::push;
        case 0:
        default:
            return ftp::core::PocketMode::strict;
    }
}

void FindThePocketAudioProcessor::pushHitTelemetry(const HitTelemetry& hit) noexcept
{
    int start1, size1, start2, size2;
    telemetryFifo_.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
        telemetryData_[static_cast<std::size_t>(start1)] = hit;
    telemetryFifo_.finishedWrite(size1);
}

void FindThePocketAudioProcessor::updateRemainingFromSession(double nowSec)
{
    const auto remaining = session_.remaining(nowSec);
    timeLeftSec_.store(static_cast<int>(std::round(remaining.timeLeftSec)));
    hitsLeft_.store(static_cast<int>(remaining.hitsLeft));
}

juce::File FindThePocketAudioProcessor::globalCalibrationFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("FindThePocket");
    if (!dir.exists())
        dir.createDirectory();
    return dir.getChildFile("calibration.txt");
}

void FindThePocketAudioProcessor::saveGlobalCalibration() const
{
    const auto file = globalCalibrationFile();
    const juce::String text = juce::String(static_cast<double>(calibrationOffsetMs_.load()), 6) + "\n"
                            + juce::String(static_cast<double>(calibrationJitterMs_.load()), 6) + "\n";
    file.replaceWithText(text);
}

void FindThePocketAudioProcessor::loadGlobalCalibration()
{
    const auto file = globalCalibrationFile();
    if (!file.existsAsFile())
        return;

    const auto content = file.loadFileAsString();
    const auto lines = juce::StringArray::fromLines(content);
    if (lines.isEmpty())
        return;

    const auto offset = lines[0].trim().getDoubleValue();
    const auto jitter = lines.size() > 1 ? lines[1].trim().getDoubleValue() : 5.0;
    scoring_.setCalibration(offset, jitter);
    calibrationOffsetMs_.store(static_cast<float>(offset));
    calibrationJitterMs_.store(static_cast<float>(jitter));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FindThePocketAudioProcessor();
}
