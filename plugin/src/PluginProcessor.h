#pragma once

#include "core/CalibrationEngine.h"
#include "core/ScoringEngine.h"
#include "core/SessionEngine.h"
#include "dsp/OnsetDetector.h"
#include "host/TransportSync.h"

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

class FindThePocketAudioProcessor final : public juce::AudioProcessor
{
public:
    struct HitTelemetry
    {
        double audioTimeSec = 0.0;
        double projectTimeSec = 0.0;
        float errorMs = 0.0f;
        float points = 0.0f;
        bool plottable = false;
    };

    struct UiSnapshot
    {
        float inputLevelPercent = 0.0f;
        float lastErrorMs = 0.0f;
        float lastPoints = 0.0f;
        float totalScorePercent = 0.0f;
        float sessionAccuracy = 0.0f;
        float sessionConsistency = 0.0f;
        float sessionStability = 0.5f;
        float sessionPocket = 0.625f;
        int hitCount = 0;
        int beatCount = 0;
        int timeLeftSec = 0;
        int hitsLeft = 0;
        bool isRunning = false;
        bool isCalibrating = false;
        bool isRecording = false;
        double projectTimeSec = 0.0;
        int runEpoch = 0;
        int calibrationSamples = 0;
        float calibrationOffsetMs = 0.0f;
        float calibrationJitterMs = 5.0f;
    };

    FindThePocketAudioProcessor();
    ~FindThePocketAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void startRun(double nowSec);
    void startCalibration();
    void stopSession();
    void nudgeCalibrationOffsetMs(double deltaMs) noexcept;
    UiSnapshot getUiSnapshot() const noexcept;
    int drainRecentHits(HitTelemetry* dst, int maxItems) noexcept;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static double runDurationFromChoice(int choiceIdx);
    static ftp::core::PocketMode pocketModeFromChoice(int choiceIdx);

    void pushHitTelemetry(const HitTelemetry& hit) noexcept;
    void updateRemainingFromSession(double nowSec);
    void saveGlobalCalibration() const;
    void loadGlobalCalibration();
    static juce::File globalCalibrationFile();

    juce::AudioProcessorValueTreeState parameters_;

    ftp::core::CalibrationEngine calibration_;
    ftp::core::ScoringEngine scoring_;
    ftp::core::SessionEngine session_;
    ftp::dsp::OnsetDetector onsetDetector_;
    ftp::host::TransportSync transportSync_;

    static constexpr int telemetryFifoSize = 1024;
    juce::AbstractFifo telemetryFifo_{ telemetryFifoSize };
    std::array<HitTelemetry, telemetryFifoSize> telemetryData_{};

    std::atomic<float> meterPercent_{ 0.0f };
    std::atomic<float> lastErrorMs_{ 0.0f };
    std::atomic<float> lastPoints_{ 0.0f };
    std::atomic<float> totalScorePercent_{ 0.0f };
    std::atomic<float> sessionAccuracy_{ 0.0f };
    std::atomic<float> sessionConsistency_{ 0.0f };
    std::atomic<float> sessionStability_{ 0.5f };
    std::atomic<float> sessionPocket_{ 0.625f };
    std::atomic<int> hitCount_{ 0 };
    std::atomic<int> beatCount_{ 0 };
    std::atomic<int> timeLeftSec_{ 0 };
    std::atomic<int> hitsLeft_{ 0 };
    std::atomic<int> calibrationSamples_{ 0 };
    std::atomic<float> calibrationOffsetMs_{ 0.0f };
    std::atomic<float> calibrationJitterMs_{ 5.0f };
    std::atomic<bool> running_{ false };
    std::atomic<bool> calibrating_{ false };
    std::atomic<bool> recording_{ false };
    std::atomic<double> projectTimeSec_{ 0.0 };
    std::atomic<double> pendingCalibrationNudgeMs_{ 0.0 };
    std::atomic<int> runEpoch_{ 0 };
    bool previousRecording_ = false;
    bool canAutoStartOnRecord_ = true;
    std::int64_t lastHostTimeInSamples_ = -1;
    std::int64_t fallbackSamplePosition_ = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
