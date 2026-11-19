#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

namespace ParamIDs
{
    inline constexpr const char* bands    = "bands";
    inline constexpr const char* weight   = "weight";
    inline constexpr const char* show     = "show";
    inline constexpr const char* detect   = "detect";
    inline constexpr const char* response = "response";
    inline constexpr const char* peakHold = "peakhold";
    inline constexpr const char* freeze   = "freeze";
}

class SpectrumScopeProcessor : public juce::AudioProcessor
{
public:
    static constexpr int ringSize = 1 << 16; // 65536 samples ต่อช่อง

    SpectrumScopeProcessor();
    ~SpectrumScopeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SpectrumScope"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // อ่านตัวอย่างล่าสุด n ตัวจากช่อง ch (0=L, 1=R) -- เรียกจาก UI thread
    void copyLatest (int channel, float* dest, int n) const;
    double getCurrentSampleRate() const { return sampleRateHz.load(); }

    // มิเตอร์: index 0 = L, 1 = R, 2 = Mid
    std::array<std::atomic<float>, 3> peak, rms;
    std::array<std::atomic<bool>, 2>  clipped;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<std::vector<float>, 2> ring;
    std::atomic<int> writePos { 0 };
    std::atomic<double> sampleRateHz { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumScopeProcessor)
};
