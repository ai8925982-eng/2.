#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

// ปุ่มทรงแคปซูลสไตล์พรีเมียม (ข้อความเปลี่ยนตามค่าพารามิเตอร์ได้)
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

    // ลากบนกราฟความถี่เพื่อเลือกช่วง -> Phase Scope จะแสดงเฉพาะช่วงนั้น
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Band
    {
        double lo = 0.0, hi = 0.0, fc = 0.0;
        float db = -120.0f;    // ระดับปัจจุบัน
        float hold = -120.0f;  // ค่าค้างสูงสุด
        float corr = 0.0f;     // ความสัมพันธ์เฟส L/R ของแถบนี้ (-1..+1)
        float weight = 0.0f;   // น้ำหนัก A/C เป็น dB
    };

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
        void setLowPass (double fs, double f, double q);
        void setHighPass (double fs, double f, double q);
        void reset() { z1 = z2 = 0.0f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void timerCallback() override;

    // --- การวิเคราะห์ ---
    void rebuildBands();
    void transform (const std::vector<float>& signal, std::vector<float>& buf);
    void computeSpectrum();
    float bandValue (const std::vector<float>& v, double lo, double hi) const;
    void updateScope();
    void updateMeters();
    void saveData();
    void graphToMem();
    void clearMem();
    void clearAll();

    // --- การวาด ---
    void paintHeader (juce::Graphics&);
    void paintSpectrum (juce::Graphics&);
    void paintScope (juce::Graphics&);
    void paintMeters (juce::Graphics&);
    void paintControlLabels (juce::Graphics&);

    float xOfFreq (double f) const;
    double freqOfX (float x) const;
    float yOfDb (float db) const;
    int bandIndexAtX (float x) const;
    void applySelection (int bandA, int bandB);

    // --- ตัวช่วยพารามิเตอร์ ---
    int  choiceIndex (const char* id) const;
    bool boolValue (const char* id) const;
    void cycleChoice (const char* id);
    void toggleBool (const char* id);

    SpectrumScopeProcessor& proc;

    // พื้นที่
    juce::Rectangle<float> headerArea, specArea, plotArea, stripArea, scopeArea, controlArea, meterArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> controlLabels;
    float uiScale = 1.0f;

    // แกนความถี่/ความดังของกราฟ (คงที่เพื่อให้อ่านง่าย)
    static constexpr double viewFMin = 17.5, viewFMax = 22700.0;
    static constexpr float viewDbTop = 0.0f, viewDbBot = -80.0f;

    // FFT (คงที่ 16384 จุด)
    static constexpr int fftOrder = 14;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> sigL, sigR, fftL, fftR;
    std::vector<float> spPow, spL, spR, spX; // กำลังเฉลี่ยตามเวลาต่อ bin
    double fs = 44100.0;

    // แถบความถี่แบบอ็อกเทฟ
    std::vector<Band> bands;
    int bandsCached = -1, weightCached = -1;
    double bandsFs = 0.0;

    // หน่วยความจำกราฟอ้างอิง
    std::vector<float> memDb;
    bool memActive = false;
    std::unique_ptr<juce::FileChooser> chooser;

    // ช่วงความถี่ที่เลือก (ผูกกราฟบนกับสโคปล่าง)
    bool selActive = false;
    double selLo = 0.0, selHi = 0.0;
    int dragStartBand = -1;

    // สโคปเฟส
    std::vector<juce::Point<float>> scopePts;
    float scopeCorr = 0.0f;
    Biquad hpL, hpR, lpL, lpR;

    // มิเตอร์
    std::array<float, 3> meterLevel { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> meterHoldDb { -200.0f, -200.0f, -200.0f };
    std::array<bool, 2> clipShown { false, false };

    // ปุ่ม
    PanelButton btnWeight { "weight" }, btnShow { "show" }, btnBands { "bands" }, btnDetect { "detect" },
                btnResponse { "response" }, btnFreeze { "freeze" }, btnHold { "hold" }, btnClear { "clear" };
    PanelButton btnSaveData { "savedata" }, btnGraphMem { "graphmem" }, btnClearMem { "clearmem" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumScopeEditor)
};
