#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kTimerHz = 30.0f;
    constexpr float kMeterRangeDb = 39.0f;
    constexpr float kPi = 3.14159265358979f;

    const float kResponseMs[] = { 40.0f, 80.0f, 160.0f, 320.0f, 640.0f, 1000.0f };
    const int   kBandsPerOctave[] = { 3, 6, 12 };

    // ชุดสี: ดำกราไฟต์ + ทองแชมเปญ
    const juce::Colour kGold     (0xffe6c27a);
    const juce::Colour kText     (0xffc9d1de);
    const juce::Colour kTextDim  (0xff6f7889);
    const juce::Colour kBorder   (0xff2a303c);
    const juce::Colour kRed      (0xfff43f5e);
    const juce::Colour kGreen    (0xff34d399);
    const juce::Colour kAmber    (0xfffbbf24);

    juce::String choiceText (SpectrumScopeProcessor& p, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
            return c->getCurrentChoiceName();
        return {};
    }

    float dbOf (float lin) { return 20.0f * std::log10 (juce::jmax (lin, 1.0e-9f)); }

    // สีตามความถี่: เบส = ส้มอุ่น -> กลาง = ทอง/เขียวมิ้นต์ -> แหลม = ฟ้า/ม่วง
    juce::Colour freqColour (double f)
    {
        static const juce::Colour stops[] = { juce::Colour (0xffff7a59), juce::Colour (0xfff5c26b), juce::Colour (0xff5eead4),
                                              juce::Colour (0xff60a5fa), juce::Colour (0xffa78bfa) };
        const double t = juce::jlimit (0.0, 1.0, (std::log (f) - std::log (20.0)) / (std::log (20000.0) - std::log (20.0))) * 4.0;
        const int i = juce::jmin (3, (int) t);
        return stops[i].interpolatedWith (stops[i + 1], (float) (t - (double) i));
    }

    // สีตามความสัมพันธ์เฟส: +1 เขียว, 0 เหลืองอำพัน, -1 แดง
    juce::Colour corrColour (float c)
    {
        return c >= 0.0f ? kAmber.interpolatedWith (kGreen, c) : kAmber.interpolatedWith (kRed, -c);
    }

    juce::String hzText (double f)
    {
        if (f >= 1000.0)
        {
            auto s = juce::String (f / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s + " kHz";
        }
        return juce::String ((int) std::lround (f)) + " Hz";
    }

    double weightingDb (int mode, double f)
    {
        if (mode == 0)
            return 0.0;

        const double f2 = f * f, c1 = 20.6 * 20.6, c2 = 107.7 * 107.7, c3 = 737.9 * 737.9, c4 = 12194.0 * 12194.0;
        if (mode == 1)
        {
            const double ra = (c4 * f2 * f2) / ((f2 + c1) * std::sqrt ((f2 + c2) * (f2 + c3)) * (f2 + c4));
            return 20.0 * std::log10 (ra) + 2.0;
        }
        const double rc = (c4 * f2) / ((f2 + c1) * (f2 + c4));
        return 20.0 * std::log10 (rc) + 0.06;
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141923), r.getX(), r.getY(),
                                                 juce::Colour (0xff0a0d13), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (kBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        // เส้นแสงบาง ๆ ด้านบนให้ดูเป็นกระจก
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) (r.getY() + 1.5f), r.getX() + 16.0f, r.getRight() - 16.0f);
    }
}

//==============================================================================
void SpectrumScopeEditor::Biquad::setLowPass (double fs, double f, double q)
{
    const double w0 = 2.0 * (double) kPi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha;
    b0 = (float) (((1.0 - cw) * 0.5) / a0);
    b1 = (float) ((1.0 - cw) / a0);
    b2 = b0;
    a1 = (float) ((-2.0 * cw) / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void SpectrumScopeEditor::Biquad::setHighPass (double fs, double f, double q)
{
    const double w0 = 2.0 * (double) kPi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha;
    b0 = (float) (((1.0 + cw) * 0.5) / a0);
    b1 = (float) (-(1.0 + cw) / a0);
    b2 = b0;
    a1 = (float) ((-2.0 * cw) / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

//==============================================================================
void PanelButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = isLit && isLit();
    const float corner = juce::jmin (b.getHeight() * 0.5f, 14.0f);

    g.setGradientFill (juce::ColourGradient (lit ? kGold.withAlpha (0.26f) : juce::Colour (0xff1b212d), b.getX(), b.getY(),
                                             lit ? kGold.withAlpha (0.08f) : juce::Colour (0xff10141c), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    g.setColour (lit ? kGold : (isDown ? juce::Colour (0xff7a849a) : (isOver ? juce::Colour (0xff4f586c) : kBorder)));
    g.drawRoundedRectangle (b, corner, 1.2f);

    g.setColour (lit ? kGold : kText);
    g.setFont (juce::jmax (9.0f, b.getHeight() * 0.42f));
    g.drawText (getText ? getText() : getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
SpectrumScopeEditor::SpectrumScopeEditor (SpectrumScopeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    sigL.assign ((size_t) fftSize, 0.0f);
    sigR.assign ((size_t) fftSize, 0.0f);
    fftL.assign ((size_t) fftSize * 2, 0.0f);
    fftR.assign ((size_t) fftSize * 2, 0.0f);

    const size_t nb = (size_t) fftSize / 2 + 1;
    spPow.assign (nb, 0.0f);
    spL.assign (nb, 0.0f);
    spR.assign (nb, 0.0f);
    spX.assign (nb, 0.0f);

    setResizable (true, true);
    setResizeLimits (840, 532, 2400, 1520);
    getConstrainer()->setFixedAspectRatio (1200.0 / 760.0);
    setSize (1200, 760);

    for (auto* b : { &btnWeight, &btnShow, &btnBands, &btnDetect, &btnResponse, &btnFreeze, &btnHold, &btnClear,
                     &btnSaveData, &btnGraphMem, &btnClearMem })
        addAndMakeVisible (*b);

    btnWeight.getText   = [this] { return choiceText (proc, ParamIDs::weight); };
    btnShow.getText     = [this] { return choiceText (proc, ParamIDs::show); };
    btnBands.getText    = [this] { return choiceText (proc, ParamIDs::bands); };
    btnDetect.getText   = [this] { return choiceText (proc, ParamIDs::detect); };
    btnResponse.getText = [this] { return choiceText (proc, ParamIDs::response) + " ms"; };
    btnFreeze.getText   = [] { return juce::String ("FREEZE"); };
    btnHold.getText     = [] { return juce::String ("PEAK HOLD"); };
    btnClear.getText    = [] { return juce::String ("CLEAR"); };
    btnSaveData.getText = [] { return juce::String ("Save Data"); };
    btnGraphMem.getText = [] { return juce::String ("Graph -> Mem"); };
    btnClearMem.getText = [] { return juce::String ("Clear Mem"); };

    btnFreeze.isLit = [this] { return boolValue (ParamIDs::freeze); };
    btnHold.isLit   = [this] { return boolValue (ParamIDs::peakHold); };

    btnWeight.onClick   = [this] { cycleChoice (ParamIDs::weight); };
    btnShow.onClick     = [this] { cycleChoice (ParamIDs::show); };
    btnBands.onClick    = [this] { cycleChoice (ParamIDs::bands); };
    btnDetect.onClick   = [this] { cycleChoice (ParamIDs::detect); };
    btnResponse.onClick = [this] { cycleChoice (ParamIDs::response); };
    btnFreeze.onClick   = [this] { toggleBool (ParamIDs::freeze); };
    btnHold.onClick     = [this] { toggleBool (ParamIDs::peakHold); };
    btnClear.onClick    = [this] { clearAll(); };
    btnSaveData.onClick = [this] { saveData(); };
    btnGraphMem.onClick = [this] { graphToMem(); };
    btnClearMem.onClick = [this] { clearMem(); };

    rebuildBands();
    startTimerHz ((int) kTimerHz);
}

SpectrumScopeEditor::~SpectrumScopeEditor()
{
    stopTimer();
}

//==============================================================================
// ตัวช่วยพารามิเตอร์
int SpectrumScopeEditor::choiceIndex (const char* id) const
{
    return (int) proc.apvts.getRawParameterValue (id)->load();
}

bool SpectrumScopeEditor::boolValue (const char* id) const
{
    return proc.apvts.getRawParameterValue (id)->load() > 0.5f;
}

void SpectrumScopeEditor::cycleChoice (const char* id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        const int next = (p->getIndex() + 1) % p->choices.size();
        p->beginChangeGesture();
        *p = next;
        p->endChangeGesture();
    }
}

void SpectrumScopeEditor::toggleBool (const char* id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (proc.apvts.getParameter (id)))
    {
        const bool next = ! p->get();
        p->beginChangeGesture();
        *p = next;
        p->endChangeGesture();
    }
}

void SpectrumScopeEditor::clearAll()
{
    for (auto& b : bands)
        b.hold = -120.0f;

    meterHoldDb.fill (-200.0f);
    proc.clipped[0].store (false);
    proc.clipped[1].store (false);
}

void SpectrumScopeEditor::graphToMem()
{
    memDb.clear();
    for (auto& b : bands)
        memDb.push_back (b.db);
    memActive = true;
}

void SpectrumScopeEditor::clearMem()
{
    memActive = false;
}

//==============================================================================
// แถบความถี่
void SpectrumScopeEditor::rebuildBands()
{
    bandsCached  = choiceIndex (ParamIDs::bands);
    weightCached = choiceIndex (ParamIDs::weight);
    bandsFs      = fs;

    const int n = kBandsPerOctave[juce::jlimit (0, 2, bandsCached)];
    const int i0 = (int) std::floor ((double) n * std::log2 (20.0 / 1000.0));
    const int i1 = (int) std::ceil  ((double) n * std::log2 (20000.0 / 1000.0));

    bands.clear();
    for (int i = i0; i <= i1; ++i)
    {
        Band b;
        b.fc = 1000.0 * std::pow (2.0, (double) i / (double) n);
        b.lo = b.fc * std::pow (2.0, -0.5 / (double) n);
        b.hi = b.fc * std::pow (2.0,  0.5 / (double) n);
        b.weight = (float) weightingDb (weightCached, b.fc);
        bands.push_back (b);
    }

    memActive = false;
}

float SpectrumScopeEditor::bandValue (const std::vector<float>& v, double lo, double hi) const
{
    const int nb = fftSize / 2;
    const double binHz = fs / (double) fftSize;
    const double kLo = lo / binHz, kHi = hi / binHz;
    if (kLo >= (double) nb)
        return 0.0f;

    const double nbins = kHi - kLo;
    const int iLo = juce::jmax (1, (int) std::ceil (kLo));
    const int iHi = juce::jmin (nb, (int) std::floor (kHi));

    if (iHi >= iLo)
    {
        double s = 0.0;
        for (int i = iLo; i <= iHi; ++i)
            s += (double) v[(size_t) i];
        return (float) (s * (nbins / (double) (iHi - iLo + 1)) / 1.5); // 1.5 = ENBW ของหน้าต่าง Hann
    }

    // แถบแคบกว่า 1 bin: ประมาณค่าจาก bin ที่อยู่ใกล้กลางแถบ
    const double kc = juce::jlimit (1.0, (double) nb - 1.0, 0.5 * (kLo + kHi));
    const int i = (int) kc;
    const double fr = kc - (double) i;
    const double p = (double) v[(size_t) i] + ((double) v[(size_t) i + 1] - (double) v[(size_t) i]) * fr;
    return (float) (p * nbins / 1.5);
}

void SpectrumScopeEditor::transform (const std::vector<float>& signal, std::vector<float>& buf)
{
    std::copy (signal.begin(), signal.end(), buf.begin());
    std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
    window.multiplyWithWindowingTable (buf.data(), (size_t) fftSize);
    fft.performRealOnlyForwardTransform (buf.data(), true);
}

void SpectrumScopeEditor::computeSpectrum()
{
    proc.copyLatest (0, sigL.data(), fftSize);
    proc.copyLatest (1, sigR.data(), fftSize);
    transform (sigL, fftL);
    transform (sigR, fftR);

    const int mode = choiceIndex (ParamIDs::show);
    const bool useRms = choiceIndex (ParamIDs::detect) == 0;
    const float tau = kResponseMs[juce::jlimit (0, 5, choiceIndex (ParamIDs::response))] / 1000.0f;
    const float alpha = std::exp (-1.0f / (kTimerHz * tau));
    const float norm = 4.0f / (float) fftSize;
    const float n2 = norm * norm;
    const int nb = fftSize / 2;

    for (int k = 0; k <= nb; ++k)
    {
        const float lr = fftL[(size_t) (2 * k)], li = fftL[(size_t) (2 * k + 1)];
        const float rr = fftR[(size_t) (2 * k)], ri = fftR[(size_t) (2 * k + 1)];
        const float pl = (lr * lr + li * li) * n2;
        const float pr = (rr * rr + ri * ri) * n2;
        const float px = (lr * rr + li * ri) * n2; // Re(L * conj(R))

        float pw;
        switch (mode)
        {
            case 0:  pw = 0.5f * (pl + pr); break;                                  // L+R
            case 1:  pw = pl; break;                                                // L
            case 2:  pw = pr; break;                                                // R
            case 3:  pw = 0.25f * (pl + pr + 2.0f * px); break;                     // MID
            default: pw = 0.25f * (pl + pr - 2.0f * px); break;                     // SIDE
        }
        pw = juce::jmax (pw, 0.0f);

        const size_t kk = (size_t) k;
        spL[kk] = alpha * spL[kk] + (1.0f - alpha) * pl;
        spR[kk] = alpha * spR[kk] + (1.0f - alpha) * pr;
        spX[kk] = alpha * spX[kk] + (1.0f - alpha) * px;
        spPow[kk] = useRms ? (alpha * spPow[kk] + (1.0f - alpha) * pw)
                           : juce::jmax (pw, spPow[kk] * alpha);
    }

    const bool holdOn = boolValue (ParamIDs::peakHold);
    for (auto& b : bands)
    {
        const float p = bandValue (spPow, b.lo, b.hi);
        const float db = juce::jmax (10.0f * std::log10 (juce::jmax (p, 1.0e-12f)) + b.weight, -120.0f);
        b.db = db;
        b.hold = holdOn ? juce::jmax (b.hold, db) : -120.0f;

        const float bl = bandValue (spL, b.lo, b.hi);
        const float br = bandValue (spR, b.lo, b.hi);
        const float bx = bandValue (spX, b.lo, b.hi);
        b.corr = (bl + br > 1.0e-9f) ? juce::jlimit (-1.0f, 1.0f, bx / (std::sqrt (bl * br) + 1.0e-12f)) : 0.0f;
    }
}

//==============================================================================
// สโคปเฟส (ถ้าเลือกช่วงความถี่ จะกรองให้เหลือเฉพาะช่วงนั้น)
void SpectrumScopeEditor::updateScope()
{
    constexpr int total = 4096, use = 2048;
    std::array<float, total> l {}, r {};
    proc.copyLatest (0, l.data(), total);
    proc.copyLatest (1, r.data(), total);

    if (selActive)
    {
        const double nyq = fs * 0.5;
        const bool useHp = selLo > 25.0;
        const bool useLp = selHi < nyq * 0.9;

        if (useHp) { hpL.setHighPass (fs, selLo, 0.7071); hpR = hpL; }
        if (useLp) { lpL.setLowPass  (fs, selHi, 0.7071); lpR = lpL; }
        hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();

        for (int i = 0; i < total; ++i)
        {
            float a = l[(size_t) i], b = r[(size_t) i];
            if (useHp) { a = hpL.process (a); b = hpR.process (b); }
            if (useLp) { a = lpL.process (a); b = lpR.process (b); }
            l[(size_t) i] = a;
            r[(size_t) i] = b;
        }
    }

    scopePts.clear();
    double sll = 0.0, srr = 0.0, slr = 0.0;

    for (int i = total - use; i < total; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        sll += (double) a * a;
        srr += (double) b * b;
        slr += (double) a * b;

        if ((i & 1) == 0)
        {
            const float m = 0.5f * (a + b);
            const float s = 0.5f * (b - a);
            const float amp = std::sqrt (m * m + s * s) * 1.41421356f;
            const float rn = juce::jlimit (0.0f, 1.0f, (dbOf (amp) + 80.0f) / 80.0f);
            const float ang = std::atan2 (s, std::abs (m));
            scopePts.push_back ({ rn * std::sin (ang), rn * std::cos (ang) });
        }
    }

    const float c = (sll + srr > 1.0e-9) ? (float) juce::jlimit (-1.0, 1.0, slr / (std::sqrt (sll * srr) + 1.0e-12)) : 0.0f;
    scopeCorr += (c - scopeCorr) * 0.2f;
}

void SpectrumScopeEditor::updateMeters()
{
    float pk[3], rm[3];
    for (size_t i = 0; i < 3; ++i)
    {
        pk[i] = proc.peak[i].exchange (0.0f);
        rm[i] = proc.rms[i].load();
    }

    const bool useRms = choiceIndex (ParamIDs::detect) == 0;
    for (size_t i = 0; i < 3; ++i)
    {
        const float target = useRms ? rm[i] : pk[i];
        if (target > meterLevel[i])
            meterLevel[i] = target;
        else
            meterLevel[i] += (target - meterLevel[i]) * (useRms ? 0.12f : 0.25f);

        if (pk[i] > 0.0f)
            meterHoldDb[i] = juce::jmax (meterHoldDb[i], dbOf (pk[i]));
    }

    clipShown[0] = proc.clipped[0].load();
    clipShown[1] = proc.clipped[1].load();
}

void SpectrumScopeEditor::timerCallback()
{
    fs = proc.getCurrentSampleRate();

    if (bandsCached != choiceIndex (ParamIDs::bands) || weightCached != choiceIndex (ParamIDs::weight) || bandsFs != fs)
        rebuildBands();

    if (! boolValue (ParamIDs::freeze))
        computeSpectrum();

    updateScope();
    updateMeters();
    repaint();
}

void SpectrumScopeEditor::saveData()
{
    juce::String csv;
    csv << "Band center (Hz),Low (Hz),High (Hz),Level (dB),Max (dB),Mem (dB),Phase correlation\n";
    const bool memOk = memActive && memDb.size() == bands.size();

    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto& b = bands[i];
        csv << juce::String (b.fc, 1) << "," << juce::String (b.lo, 1) << "," << juce::String (b.hi, 1) << ","
            << juce::String (b.db, 2) << ","
            << (b.hold > -119.0f ? juce::String (b.hold, 2) : juce::String()) << ","
            << (memOk ? juce::String (memDb[i], 2) : juce::String()) << ","
            << juce::String (b.corr, 3) << "\n";
    }

    chooser = std::make_unique<juce::FileChooser> ("Save spectrum data (CSV)",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SpectrumScope_data.csv"),
        "*.csv");

    const int flags = juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (flags, [csv] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
            f.withFileExtension ("csv").replaceWithText (csv);
    });
}

//==============================================================================
// เมาส์: ลากเลือกช่วงความถี่
int SpectrumScopeEditor::bandIndexAtX (float x) const
{
    if (bands.empty())
        return -1;

    const double f = freqOfX (x);
    for (size_t i = 0; i < bands.size(); ++i)
        if (f < bands[i].hi)
            return (int) i;

    return (int) bands.size() - 1;
}

void SpectrumScopeEditor::applySelection (int a, int b)
{
    if (a < 0 || b < 0 || bands.empty())
        return;

    const int lo = juce::jmin (a, b), hi = juce::jmax (a, b);
    selLo = bands[(size_t) lo].lo;
    selHi = bands[(size_t) hi].hi;
    selActive = true;
}

void SpectrumScopeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (plotArea.expanded (0.0f, 6.0f).contains (e.position))
    {
        dragStartBand = bandIndexAtX (e.position.x);
        applySelection (dragStartBand, dragStartBand);
    }
}

void SpectrumScopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStartBand >= 0)
        applySelection (dragStartBand, bandIndexAtX (e.position.x));
}

void SpectrumScopeEditor::mouseUp (const juce::MouseEvent&)
{
    dragStartBand = -1;
}

void SpectrumScopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (plotArea.expanded (0.0f, 6.0f).contains (e.position))
    {
        selActive = false;
        dragStartBand = -1;
    }
}

//==============================================================================
// การวาด
float SpectrumScopeEditor::xOfFreq (double f) const
{
    const double lmin = std::log (viewFMin), lmax = std::log (viewFMax);
    return plotArea.getX() + (float) ((std::log (juce::jmax (f, 1.0)) - lmin) / (lmax - lmin)) * plotArea.getWidth();
}

double SpectrumScopeEditor::freqOfX (float x) const
{
    const double lmin = std::log (viewFMin), lmax = std::log (viewFMax);
    return std::exp (lmin + (double) ((x - plotArea.getX()) / plotArea.getWidth()) * (lmax - lmin));
}

float SpectrumScopeEditor::yOfDb (float db) const
{
    return plotArea.getY() + (viewDbTop - db) / (viewDbTop - viewDbBot) * plotArea.getHeight();
}

void SpectrumScopeEditor::paintHeader (juce::Graphics& g)
{
    const float s = uiScale;
    g.setColour (kGold);
    g.setFont (juce::jmax (11.0f, 20.0f * s));
    g.drawText ("S P E C T R U M   S C O P E", juce::Rectangle<float> (headerArea.getX() + 6.0f * s, headerArea.getY(), 420.0f * s, headerArea.getHeight()),
                juce::Justification::centredLeft, false);

    g.setColour (kGold.withAlpha (0.35f));
    g.drawHorizontalLine ((int) headerArea.getBottom(), headerArea.getX(), headerArea.getRight());
}

void SpectrumScopeEditor::paintSpectrum (juce::Graphics& g)
{
    const float s = uiScale;
    drawPanel (g, specArea);

    // หัวข้อ + คำอธิบาย
    g.setColour (kGold);
    g.setFont (juce::jmax (9.0f, 14.0f * s));
    g.drawText ("SPECTRUM", juce::Rectangle<float> (specArea.getX() + 20.0f * s, specArea.getY() + 11.0f * s, 120.0f * s, 20.0f * s),
                juce::Justification::centredLeft, false);

    const bool holdOn = boolValue (ParamIDs::peakHold);
    const bool memShown = memActive && memDb.size() == bands.size();

    {
        float lx = specArea.getX() + 140.0f * s;
        const float ly = specArea.getY() + 11.0f * s;
        g.setFont (juce::jmax (9.0f, 13.0f * s));

        auto chip = [&] (juce::Colour col, const juce::String& text)
        {
            g.setColour (col);
            g.fillEllipse (lx, ly + 10.0f * s - 3.0f, 6.0f, 6.0f);
            g.setColour (kText);
            g.drawText (text, juce::Rectangle<float> (lx + 12.0f, ly, 120.0f * s, 20.0f * s), juce::Justification::centredLeft, false);
            lx += 100.0f * s;
        };

        chip (freqColour (400.0), choiceText (proc, ParamIDs::show));
        if (holdOn)  chip (kGold, "Max");
        if (memShown) chip (juce::Colours::white, "Mem");
    }

    g.setColour (kTextDim);
    g.setFont (juce::jmax (9.0f, 12.0f * s));
    g.drawText ("Drag to focus the phase scope on a band  |  Double-click to clear",
                juce::Rectangle<float> (specArea.getRight() - 470.0f * s, specArea.getY() + 11.0f * s, 450.0f * s, 20.0f * s),
                juce::Justification::centredRight, false);

    // เส้นกริด dB
    g.setFont (juce::jmax (9.0f, 12.0f * s));
    for (int d = 0; d >= -80; d -= 20)
    {
        const float y = yOfDb ((float) d);
        g.setColour (juce::Colour (d == 0 ? 0x30ffffff : 0x16ffffff));
        g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());
        g.setColour (kTextDim);
        g.drawText (juce::String (d), juce::Rectangle<float> (specArea.getX(), y - 9.0f, plotArea.getX() - specArea.getX() - 8.0f * s, 18.0f),
                    juce::Justification::centredRight, false);
    }

    // เส้นกริดความถี่ (ห่างกันหนึ่งอ็อกเทฟ)
    static const double gridF[] = { 31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
    static const char* gridN[]  = { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    for (int i = 0; i < 10; ++i)
    {
        const float x = xOfFreq (gridF[i]);
        g.setColour (juce::Colour (0x12ffffff));
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        g.setColour (kTextDim);
        g.drawText (gridN[i], juce::Rectangle<float> (x - 24.0f, stripArea.getBottom() + 2.0f * s, 48.0f, 18.0f * s),
                    juce::Justification::centred, false);
    }
    g.drawText ("Hz", juce::Rectangle<float> (plotArea.getRight() - 30.0f, stripArea.getBottom() + 2.0f * s, 34.0f, 18.0f * s),
                juce::Justification::centredRight, false);
    g.drawText ("dB", juce::Rectangle<float> (specArea.getX() + 4.0f * s, plotArea.getY() - 22.0f * s, 40.0f * s, 16.0f * s),
                juce::Justification::centredRight, false);

    // ไฮไลต์ช่วงที่เลือก
    juce::Colour selCol = kGold;
    if (selActive)
    {
        selCol = freqColour (std::sqrt (selLo * selHi));
        const float x1 = xOfFreq (selLo), x2 = xOfFreq (selHi);
        const juce::Rectangle<float> sel (x1, plotArea.getY(), x2 - x1, stripArea.getBottom() - plotArea.getY());
        g.setColour (selCol.withAlpha (0.10f));
        g.fillRect (sel);
        g.setColour (selCol.withAlpha (0.7f));
        g.drawVerticalLine ((int) x1, sel.getY(), sel.getBottom());
        g.drawVerticalLine ((int) x2, sel.getY(), sel.getBottom());
        g.setFont (juce::jmax (9.0f, 12.0f * s));
        g.drawText (hzText (selLo) + " - " + hzText (selHi),
                    juce::Rectangle<float> (x1 - 60.0f, plotArea.getY() + 2.0f, (x2 - x1) + 120.0f, 16.0f * s),
                    juce::Justification::centred, false);
    }

    // แท่งแถบความถี่
    for (const auto& b : bands)
    {
        if (b.db <= viewDbBot)
            continue;

        const float xl = xOfFreq (b.lo), xr = xOfFreq (b.hi);
        const float gap = juce::jmax (1.0f, (xr - xl) * 0.2f);
        const float top = juce::jlimit (plotArea.getY(), plotArea.getBottom(), yOfDb (b.db));
        const juce::Rectangle<float> bar (xl + gap * 0.5f, top, xr - xl - gap, plotArea.getBottom() - top);
        if (bar.getWidth() <= 0.5f || bar.getHeight() <= 0.5f)
            continue;

        const bool inSel = ! selActive || (b.fc >= selLo * 0.999 && b.fc <= selHi * 1.001);
        const float a = inSel ? 1.0f : 0.35f;
        const auto col = freqColour (b.fc);

        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.95f * a), 0.0f, bar.getY(),
                                                 col.withAlpha (0.10f * a), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (bar, juce::jmin (3.0f, bar.getWidth() * 0.4f));

        g.setColour (col.brighter (0.5f).withAlpha (a));
        g.fillRoundedRectangle (juce::Rectangle<float> (bar.getX(), bar.getY(), bar.getWidth(), juce::jmin (2.5f, bar.getHeight())), 1.0f);

        if (holdOn && b.hold > viewDbBot)
        {
            const float yh = juce::jlimit (plotArea.getY(), plotArea.getBottom(), yOfDb (b.hold));
            g.setColour (kGold.withAlpha (a));
            g.fillRect (juce::Rectangle<float> (bar.getX(), yh - 1.0f, bar.getWidth(), 2.0f));
        }
    }

    // เส้นอ้างอิง Mem
    if (memShown)
    {
        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            if (memDb[i] <= viewDbBot)
                continue;
            const float x = 0.5f * (xOfFreq (bands[i].lo) + xOfFreq (bands[i].hi));
            const float y = juce::jlimit (plotArea.getY(), plotArea.getBottom(), yOfDb (memDb[i]));
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else           { p.lineTo (x, y); }
        }
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // แถบความสัมพันธ์เฟสของแต่ละช่วง (เชื่อมกับ Phase Scope ด้านล่าง)
    g.setColour (kTextDim);
    g.setFont (juce::jmax (8.0f, 10.0f * s));
    g.drawText ("PHASE", juce::Rectangle<float> (specArea.getX() + 4.0f * s, stripArea.getY() - 3.0f * s, plotArea.getX() - specArea.getX() - 8.0f * s, stripArea.getHeight() + 6.0f * s),
                juce::Justification::centredRight, false);

    for (const auto& b : bands)
    {
        const float xl = xOfFreq (b.lo), xr = xOfFreq (b.hi);
        const float gap = juce::jmax (1.0f, (xr - xl) * 0.2f);
        const juce::Rectangle<float> cell (xl + gap * 0.5f, stripArea.getY(), xr - xl - gap, stripArea.getHeight());
        if (cell.getWidth() <= 0.5f)
            continue;

        const bool audible = b.db > -70.0f;
        g.setColour (audible ? corrColour (b.corr) : juce::Colour (0xff222836));
        g.fillRoundedRectangle (cell, 2.0f);
    }
}

void SpectrumScopeEditor::paintScope (juce::Graphics& g)
{
    const float s = uiScale;
    drawPanel (g, scopeArea);

    juce::Colour dotCol (0xfff2d9a0);
    if (selActive)
        dotCol = freqColour (std::sqrt (selLo * selHi));

    g.setColour (kGold);
    g.setFont (juce::jmax (9.0f, 14.0f * s));
    g.drawText ("PHASE SCOPE", juce::Rectangle<float> (scopeArea.getX() + 20.0f * s, scopeArea.getY() + 11.0f * s, 160.0f * s, 20.0f * s),
                juce::Justification::centredLeft, false);

    g.setColour (selActive ? dotCol : kTextDim);
    g.setFont (juce::jmax (9.0f, 12.0f * s));
    g.drawText (selActive ? hzText (selLo) + " - " + hzText (selHi) : juce::String ("Full range"),
                juce::Rectangle<float> (scopeArea.getRight() - 200.0f * s, scopeArea.getY() + 11.0f * s, 180.0f * s, 20.0f * s),
                juce::Justification::centredRight, false);

    const float corrH = 44.0f * s, labelH = 20.0f * s;
    const float cx = scopeArea.getCentreX();
    const float cy = scopeArea.getBottom() - corrH - labelH - 4.0f * s;
    const float R = juce::jmax (20.0f, juce::jmin (scopeArea.getWidth() * 0.5f - 22.0f * s, cy - (scopeArea.getY() + 46.0f * s)));

    // โซนแอนติเฟส (ผูกกับสีแดงของแถบเฟสด้านบน)
    {
        const juce::Rectangle<float> disc (cx - R, cy - R, 2.0f * R, 2.0f * R);
        juce::Path wl, wr;
        wl.addPieSegment (disc, -kPi * 0.5f, -kPi * 0.25f, 0.0f);
        wr.addPieSegment (disc, kPi * 0.25f, kPi * 0.5f, 0.0f);
        g.setColour (kRed.withAlpha (0.09f));
        g.fillPath (wl);
        g.fillPath (wr);
    }

    // วงแหวนและเส้นแกน
    for (int i = 1; i <= 4; ++i)
    {
        const float rr = R * (float) i / 4.0f;
        juce::Path arc;
        arc.addCentredArc (cx, cy, rr, rr, 0.0f, -kPi * 0.5f, kPi * 0.5f, true);
        g.setColour (kGold.withAlpha (i == 4 ? 0.40f : 0.18f));
        g.strokePath (arc, juce::PathStrokeType (1.0f));
    }

    g.setColour (kGold.withAlpha (0.18f));
    g.drawVerticalLine ((int) cx, cy - R, cy);
    g.drawHorizontalLine ((int) cy, cx - R, cx + R);

    const float dash[] = { 5.0f, 5.0f };
    const float d = R * 0.7071f;
    g.setColour (kGold.withAlpha (0.35f));
    g.drawDashedLine (juce::Line<float> (cx, cy, cx - d, cy - d), dash, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (cx, cy, cx + d, cy - d), dash, 2, 1.0f);

    // จุดสัญญาณ
    for (auto& p : scopePts)
    {
        const float x = cx + p.x * R;
        const float y = cy - p.y * R;
        g.setColour (dotCol.withAlpha (0.18f));
        g.fillEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f);
        g.setColour (dotCol.withAlpha (0.75f));
        g.fillRect (x - 0.9f, y - 0.9f, 1.8f, 1.8f);
    }

    // ป้ายกำกับ
    g.setFont (juce::jmax (9.0f, 12.0f * s));
    g.setColour (kText);
    g.drawText ("L", juce::Rectangle<float> (cx - d - 26.0f * s, cy - d - 8.0f * s, 22.0f * s, 16.0f * s), juce::Justification::centredRight, false);
    g.drawText ("R", juce::Rectangle<float> (cx + d + 4.0f * s, cy - d - 8.0f * s, 22.0f * s, 16.0f * s), juce::Justification::centredLeft, false);
    g.setColour (kRed.withAlpha (0.8f));
    g.drawText ("ANTI-PHASE", juce::Rectangle<float> (cx - R, cy - 20.0f * s, 90.0f * s, 16.0f * s), juce::Justification::centredLeft, false);
    g.drawText ("ANTI-PHASE", juce::Rectangle<float> (cx + R - 90.0f * s, cy - 20.0f * s, 90.0f * s, 16.0f * s), juce::Justification::centredRight, false);

    g.setColour (kTextDim);
    static const char* axis[] = { "0", "-20", "-40", "-60", "-80", "-60", "-40", "-20", "0" };
    for (int i = 0; i < 9; ++i)
    {
        const float x = cx + R * (float) (i - 4) / 4.0f;
        g.drawText (axis[i], juce::Rectangle<float> (x - 22.0f, cy + 3.0f * s, 44.0f, 16.0f * s), juce::Justification::centred, false);
    }

    // แถบ Correlation
    {
        const float trackW = juce::jmin (2.0f * R, scopeArea.getWidth() - 60.0f * s);
        const float tx = cx - trackW * 0.5f;
        const float ty = scopeArea.getBottom() - 22.0f * s;
        const float th = 6.0f * s;

        juce::ColourGradient grad (kRed, tx, ty, kGreen, tx + trackW, ty, false);
        grad.addColour (0.5, kAmber);
        g.setGradientFill (grad);
        g.setOpacity (0.85f);
        g.fillRoundedRectangle (tx, ty, trackW, th, th * 0.5f);

        const float mx = cx + scopeCorr * trackW * 0.5f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (mx - 5.0f * s, ty + th * 0.5f - 5.0f * s, 10.0f * s, 10.0f * s);
        g.setColour (corrColour (scopeCorr));
        g.drawEllipse (mx - 5.0f * s, ty + th * 0.5f - 5.0f * s, 10.0f * s, 10.0f * s, 2.0f);

        g.setFont (juce::jmax (9.0f, 12.0f * s));
        g.setColour (kText);
        g.drawText ("Correlation " + juce::String (scopeCorr, 2),
                    juce::Rectangle<float> (cx - 100.0f * s, ty - 20.0f * s, 200.0f * s, 16.0f * s), juce::Justification::centred, false);
        g.setColour (kTextDim);
        g.drawText ("-1", juce::Rectangle<float> (tx - 6.0f, ty + th + 2.0f, 30.0f, 14.0f * s), juce::Justification::centredLeft, false);
        g.drawText ("0",  juce::Rectangle<float> (cx - 15.0f, ty + th + 2.0f, 30.0f, 14.0f * s), juce::Justification::centred, false);
        g.drawText ("+1", juce::Rectangle<float> (tx + trackW - 24.0f, ty + th + 2.0f, 30.0f, 14.0f * s), juce::Justification::centredRight, false);
    }
}

void SpectrumScopeEditor::paintMeters (juce::Graphics& g)
{
    const float s = uiScale;
    drawPanel (g, meterArea);

    g.setColour (kGold);
    g.setFont (juce::jmax (9.0f, 14.0f * s));
    g.drawText ("LEVEL", juce::Rectangle<float> (meterArea.getX() + 20.0f * s, meterArea.getY() + 11.0f * s, 120.0f * s, 20.0f * s),
                juce::Justification::centredLeft, false);

    const float labelW = 34.0f * s;
    const float barsX = meterArea.getX() + labelW + 8.0f * s;
    const float barsW = meterArea.getWidth() - labelW - 30.0f * s;
    const float barW = barsW / 3.0f * 0.66f;
    const float gap = (barsW - 3.0f * barW) / 2.0f;
    const float top = meterArea.getY() + 56.0f * s;
    const float bot = meterArea.getBottom() - 52.0f * s;
    const float h = bot - top;

    auto yOfMeterDb = [&] (float db) { return top + juce::jlimit (0.0f, 1.0f, -db / kMeterRangeDb) * h; };

    g.setFont (juce::jmax (8.0f, 11.0f * s));
    for (int d = 0; d >= -36; d -= 6)
    {
        const float y = yOfMeterDb ((float) d);
        g.setColour (kTextDim);
        g.drawText (juce::String (d), juce::Rectangle<float> (meterArea.getX() + 4.0f * s, y - 8.0f, labelW - 2.0f * s, 16.0f),
                    juce::Justification::centredRight, false);
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) y, barsX - 4.0f * s, barsX + barsW);
    }

    static const char* names[] = { "L", "M", "R" };
    for (int i = 0; i < 3; ++i)
    {
        const float bx = barsX + (float) i * (barW + gap);
        const juce::Rectangle<float> bar (bx, top, barW, h);

        g.setColour (kText);
        g.setFont (juce::jmax (9.0f, 13.0f * s));
        g.drawText (names[i], juce::Rectangle<float> (bx - 10.0f, top - 34.0f * s, barW + 20.0f, 16.0f * s), juce::Justification::centred, false);

        // ไฟ clip
        const bool clipOn = (i != 1) && clipShown[(size_t) (i == 0 ? 0 : 1)];
        g.setColour (clipOn ? kRed : juce::Colour (0xff3a1a22));
        g.fillEllipse (bx + barW * 0.5f - 4.0f * s, top - 16.0f * s, 8.0f * s, 8.0f * s);

        g.setColour (juce::Colour (0xff07090d));
        g.fillRoundedRectangle (bar, 4.0f);

        const float lvDb = dbOf (meterLevel[(size_t) i]);
        const float yLv = yOfMeterDb (lvDb);
        if (yLv < bot - 1.0f)
        {
            juce::ColourGradient grad (kRed, 0.0f, top, juce::Colour (0xff2dd4bf), 0.0f, bot, false);
            grad.addColour (0.16, kAmber);
            grad.addColour (0.42, juce::Colour (0xff5eead4));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (juce::Rectangle<float> (bx + 2.0f, yLv, barW - 4.0f, bot - yLv - 2.0f), 2.5f);
        }

        if (boolValue (ParamIDs::peakHold) && meterHoldDb[(size_t) i] > -kMeterRangeDb)
        {
            const float yh = yOfMeterDb (meterHoldDb[(size_t) i]);
            g.setColour (kGold);
            g.fillRect (juce::Rectangle<float> (bx + 1.0f, yh - 1.0f, barW - 2.0f, 2.0f));
        }

        g.setColour (kBorder);
        g.drawRoundedRectangle (bar, 4.0f, 1.0f);

        // ช่องแสดงค่าพีคสูงสุด
        const juce::Rectangle<float> box (bx - 6.0f * s, bot + 10.0f * s, barW + 12.0f * s, 24.0f * s);
        g.setColour (juce::Colour (0xff07090d));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (kBorder);
        g.drawRoundedRectangle (box, 6.0f, 1.0f);
        g.setColour (kGold);
        g.setFont (juce::jmax (9.0f, 12.0f * s));
        g.drawText (juce::String (juce::jmax (meterHoldDb[(size_t) i], -40.0f), 1), box, juce::Justification::centred, false);
    }
}

void SpectrumScopeEditor::paintControlLabels (juce::Graphics& g)
{
    g.setColour (kTextDim);
    g.setFont (juce::jmax (9.0f, 12.0f * uiScale));
    for (auto& [text, r] : controlLabels)
        g.drawText (text, r, juce::Justification::centredLeft, false);
}

void SpectrumScopeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1118), 0.0f, 0.0f,
                                             juce::Colour (0xff050608), 0.0f, (float) getHeight(), false));
    g.fillAll();

    paintHeader (g);
    paintSpectrum (g);
    paintScope (g);

    drawPanel (g, controlArea);
    g.setColour (kGold);
    g.setFont (juce::jmax (9.0f, 14.0f * uiScale));
    g.drawText ("CONTROLS", juce::Rectangle<float> (controlArea.getX() + 20.0f * uiScale, controlArea.getY() + 11.0f * uiScale, 160.0f * uiScale, 20.0f * uiScale),
                juce::Justification::centredLeft, false);
    paintControlLabels (g);

    paintMeters (g);
}

void SpectrumScopeEditor::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float s = H / 760.0f;
    uiScale = s;

    headerArea  = { 0.015f * W, 0.012f * H, 0.970f * W, 0.062f * H };
    specArea    = { 0.015f * W, 0.090f * H, 0.970f * W, 0.465f * H };
    scopeArea   = { 0.015f * W, 0.570f * H, 0.385f * W, 0.415f * H };
    controlArea = { 0.415f * W, 0.570f * H, 0.265f * W, 0.415f * H };
    meterArea   = { 0.695f * W, 0.570f * H, 0.290f * W, 0.415f * H };

    plotArea  = { specArea.getX() + 58.0f * s, specArea.getY() + 48.0f * s,
                  specArea.getWidth() - 58.0f * s - 24.0f * s, specArea.getHeight() - 48.0f * s - 68.0f * s };
    stripArea = { plotArea.getX(), plotArea.getBottom() + 8.0f * s, plotArea.getWidth(), 8.0f * s };

    auto setB = [] (juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt());
    };

    // ปุ่มด้านขวาของแถบหัว
    {
        const float bw = 0.115f * W, bh = headerArea.getHeight() * 0.74f;
        const float by = headerArea.getY() + (headerArea.getHeight() - bh) * 0.5f;
        const float gp = 0.008f * W;
        setB (btnClearMem, headerArea.getRight() - bw, by, bw, bh);
        setB (btnGraphMem, headerArea.getRight() - 2.0f * bw - gp, by, bw, bh);
        setB (btnSaveData, headerArea.getRight() - 3.0f * bw - 2.0f * gp, by, bw, bh);
    }

    // ปุ่มควบคุม 2 คอลัมน์ x 4 แถว
    controlLabels.clear();
    {
        const float x = controlArea.getX() + 18.0f * s;
        const float y = controlArea.getY() + 44.0f * s;
        const float w = controlArea.getWidth() - 36.0f * s;
        const float h = controlArea.getHeight() - 44.0f * s - 12.0f * s;
        const float colW = w / 2.0f, rowH = h / 4.0f;

        struct Cell { const char* label; PanelButton* btn; };
        const Cell cells[] = { { "Show", &btnShow },        { "Bands", &btnBands },
                               { "Detect", &btnDetect },    { "Response", &btnResponse },
                               { "Weight", &btnWeight },    { "Freeze", &btnFreeze },
                               { "Peak Hold", &btnHold },   { "Clear", &btnClear } };

        for (int i = 0; i < 8; ++i)
        {
            const int r = i / 2, c = i % 2;
            const float cx0 = x + (float) c * colW, cy0 = y + (float) r * rowH;
            controlLabels.push_back ({ cells[i].label,
                                       juce::Rectangle<float> (cx0 + 6.0f * s, cy0, colW - 12.0f * s, 0.3f * rowH).toNearestInt() });
            setB (*cells[i].btn, cx0 + 4.0f * s, cy0 + 0.32f * rowH, colW - 8.0f * s, 0.56f * rowH);
        }
    }

    repaint();
}
