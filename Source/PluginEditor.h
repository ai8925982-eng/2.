#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

// ปุ่มสไตล์แผงควบคุม (ข้อความเปลี่ยนตามค่าพารามิเตอร์ได้)
class PanelButton : public juce::Button
{
public:
    explicit PanelButton (const juce::String& name) : juce::Button (name) {}

    std::function<juce::String()> getText;
    std::function<bool()> isLit;

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override;
};

class SpectrumScopeEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit SpectrumScopeEditor (SpectrumScopeProcessor&);
    ~SpectrumScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // --- การวิเคราะห์ ---
    void rebuildFFT (int order);
    void rebuildWeighting();
    void computeSpectrum();
    void analyse (const std::vector<float>& signal, int curve, bool useRms, float alpha, bool holdOn);
    void updateScope();
    void updateMeters();

    // --- การวาด ---
    void paintSpectrum (juce::Graphics&);
    void paintScope (juce::Graphics&);
    void paintMeters (juce::Graphics&);
    void paintControlLabels (juce::Graphics&);
    juce::Path makeCurve (const std::vector<float>& db, bool& ok, float& firstX, float& lastX) const;

    float xOfFreq (double f) const;
    double freqOfX (float x) const;
    float yOfDb (float db) const;

    // --- ตัวช่วยพารามิเตอร์ ---
    int  choiceIndex (const char* id) const;
    bool boolValue (const char* id) const;
    void cycleChoice (const char* id);
    void toggleBool (const char* id);
    void clearAll();

    // --- มุมมอง (Navigate / Zoom) ---
    void pan (float dxFrac, float dyFrac);
    void zoom (float factor);
    void resetZoom();

    SpectrumScopeProcessor& proc;

    // พื้นที่
    juce::Rectangle<float> specArea, plotArea, scopeArea, meterArea, navArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> controlLabels;

    // มุมมองสเปกตรัม
    static constexpr float defFMin = 5.5f, defFMax = 20500.0f, defDbTop = 0.0f, defDbBot = -80.0f;
    float viewFMin = defFMin, viewFMax = defFMax, viewDbTop = defDbTop, viewDbBot = defDbBot;

    // FFT
    int fftOrder = 0, fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    std::vector<float> fftBuf, sigL, sigR, sigA, sigB;
    std::array<std::vector<float>, 2> smoothPow, holdDb, curveDb;
    std::vector<float> weightDb;
    double weightFs = 0.0;
    int weightMode = -1;
    int nCurves = 1;
    double fs = 44100.0;

    // สโคปเฟส
    std::vector<juce::Point<float>> scopePts;

    // มิเตอร์
    std::array<float, 3> meterLevel { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> meterHoldDb { -200.0f, -200.0f, -200.0f };
    std::array<bool, 2> clipShown { false, false };

    // ปุ่ม
    PanelButton btnLf { "lf" }, btnWeight { "weight" }, btnFreeze { "freeze" }, btnShow { "show" },
                btnHold { "hold" }, btnClear { "clear" }, btnDetect { "detect" }, btnResponse { "response" };
    PanelButton navLeft { "nl" }, navRight { "nr" }, navUp { "nu" }, navDown { "nd" },
                zoomIn { "zi" }, zoomOut { "zo" }, btnResetZoom { "rz" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumScopeEditor)
};
