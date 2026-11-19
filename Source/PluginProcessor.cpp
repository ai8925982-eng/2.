#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectrumScopeProcessor::SpectrumScopeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& r : ring)
        r.assign ((size_t) ringSize, 0.0f);

    for (auto& p : peak)    p.store (0.0f);
    for (auto& r : rms)     r.store (0.0f);
    for (auto& c : clipped) c.store (false);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectrumScopeProcessor::createLayout()
{
    using namespace juce;
    using PID = ParameterID;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (PID { ParamIDs::bands, 1 }, "Bands",
                                                        StringArray { "1/3 OCT", "1/6 OCT", "1/12 OCT" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ParamIDs::weight, 1 }, "Weighting",
                                                        StringArray { "FLAT", "A", "C" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ParamIDs::show, 1 }, "Show",
                                                        StringArray { "L+R", "L", "R", "MID", "SIDE" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ParamIDs::detect, 1 }, "Detect",
                                                        StringArray { "RMS", "PEAK" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ParamIDs::response, 1 }, "Response (ms)",
                                                        StringArray { "40", "80", "160", "320", "640", "1000" }, 2));
    layout.add (std::make_unique<AudioParameterBool> (PID { ParamIDs::peakHold, 1 }, "Peak Hold", false));
    layout.add (std::make_unique<AudioParameterBool> (PID { ParamIDs::freeze, 1 }, "Freeze", false));

    return layout;
}

void SpectrumScopeProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz.store (sampleRate);
}

bool SpectrumScopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo();
}

void SpectrumScopeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n   = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0)
        return;

    // ปลั๊กอินนี้เป็นเครื่องมือวัด: สัญญาณผ่านตรง ๆ ไม่ถูกแก้ไข
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (nch > 1 ? 1 : 0);

    constexpr int mask = ringSize - 1;
    const int w = writePos.load (std::memory_order_relaxed);

    float pk[3] { 0.0f, 0.0f, 0.0f };
    double sq[3] { 0.0, 0.0, 0.0 };

    for (int i = 0; i < n; ++i)
    {
        const float a = l[i];
        const float b = r[i];
        const float m = 0.5f * (a + b);

        ring[0][(size_t) ((w + i) & mask)] = a;
        ring[1][(size_t) ((w + i) & mask)] = b;

        pk[0] = juce::jmax (pk[0], std::abs (a));
        pk[1] = juce::jmax (pk[1], std::abs (b));
        pk[2] = juce::jmax (pk[2], std::abs (m));
        sq[0] += (double) a * a;
        sq[1] += (double) b * b;
        sq[2] += (double) m * m;
    }

    writePos.store ((w + n) & mask, std::memory_order_release);

    for (int c = 0; c < 3; ++c)
    {
        if (pk[c] > peak[(size_t) c].load())
            peak[(size_t) c].store (pk[c]);
        rms[(size_t) c].store ((float) std::sqrt (sq[c] / (double) n));
    }

    if (pk[0] >= 0.999f) clipped[0].store (true);
    if (pk[1] >= 0.999f) clipped[1].store (true);
}

void SpectrumScopeProcessor::copyLatest (int channel, float* dest, int n) const
{
    n = juce::jlimit (1, ringSize, n);
    constexpr int mask = ringSize - 1;
    const int end   = writePos.load (std::memory_order_acquire);
    const int start = (end - n) & mask;
    const auto& src = ring[(size_t) juce::jlimit (0, 1, channel)];

    for (int i = 0; i < n; ++i)
        dest[i] = src[(size_t) ((start + i) & mask)];
}

juce::AudioProcessorEditor* SpectrumScopeProcessor::createEditor()
{
    return new SpectrumScopeEditor (*this);
}

void SpectrumScopeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectrumScopeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrumScopeProcessor();
}
