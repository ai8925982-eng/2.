#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kTimerHz = 30.0f;
    constexpr float kMeterRangeDb = 39.0f;

    const float kResponseMs[] = { 40.0f, 80.0f, 160.0f, 320.0f, 640.0f, 1000.0f };

    const juce::Colour kCyan   (0xff35c8ff);
    const juce::Colour kOrange (0xffffa24d);
    const juce::Colour kGrid   (0xff1b6fe0);
    const juce::Colour kLabel  (0xffd8e6f5);

    juce::String choiceText (SpectrumScopeProcessor& p, const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
            return c->getCurrentChoiceName();
        return {};
    }

    float dbOf (float lin) { return 20.0f * std::log10 (juce::jmax (lin, 1.0e-9f)); }

    void drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff3a424b));
        g.fillRoundedRectangle (r.expanded (4.0f), 3.0f);
        g.setColour (juce::Colour (0xff7d8a97));
        g.drawRoundedRectangle (r.expanded (4.0f), 3.0f, 1.5f);
        g.setColour (juce::Colour (0xff040c1e));
        g.fillRect (r);
    }
}

//==============================================================================
void PanelButton::paintButton (juce::Graphics& g, [[maybe_unused]] bool isOver, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = isLit && isLit();

    g.setGradientFill (juce::ColourGradient (juce::Colour (lit ? 0xff14597f : 0xff0e3a5a), b.getX(), b.getY(),
                                             juce::Colour (0xff06182c), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (isDown ? juce::Colour (0xff5ff2ff) : juce::Colour (0xff6a7d8c));
    g.drawRoundedRectangle (b, 3.0f, 1.5f);

    g.setColour (lit ? juce::Colour (0xff7ff6ff) : juce::Colour (0xff17c7e6));
    g.setFont (juce::jmax (9.0f, b.getHeight() * 0.48f));
    g.drawText (getText ? getText() : getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
SpectrumScopeEditor::SpectrumScopeEditor (SpectrumScopeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (700, 462, 2200, 1453);
    getConstrainer()->setFixedAspectRatio (1393.0 / 920.0);
    setSize (1100, 727);

    for (auto* b : { &btnLf, &btnWeight, &btnFreeze, &btnShow, &btnHold, &btnClear, &btnDetect, &btnResponse,
                     &navLeft, &navRight, &navUp, &navDown, &zoomIn, &zoomOut, &btnResetZoom })
        addAndMakeVisible (*b);

    btnLf.getText       = [this] { return choiceText (proc, ParamIDs::lfres); };
    btnWeight.getText   = [this] { return choiceText (proc, ParamIDs::weight); };
    btnShow.getText     = [this] { return choiceText (proc, ParamIDs::show); };
    btnDetect.getText   = [this] { return choiceText (proc, ParamIDs::detect); };
    btnResponse.getText = [this] { return choiceText (proc, ParamIDs::response); };
    btnFreeze.getText   = [] { return juce::String ("FREEZE"); };
    btnHold.getText     = [] { return juce::String ("HOLD"); };
    btnClear.getText    = [] { return juce::String ("CLEAR"); };
    navLeft.getText     = [] { return juce::String ("<"); };
    navRight.getText    = [] { return juce::String (">"); };
    navUp.getText       = [] { return juce::String ("^"); };
    navDown.getText     = [] { return juce::String ("v"); };
    zoomIn.getText      = [] { return juce::String ("+"); };
    zoomOut.getText     = [] { return juce::String ("-"); };
    btnResetZoom.getText = [] { return juce::String ("RESET ZOOM"); };

    btnFreeze.isLit = [this] { return boolValue (ParamIDs::freeze); };
    btnHold.isLit   = [this] { return boolValue (ParamIDs::peakHold); };

    btnLf.onClick       = [this] { cycleChoice (ParamIDs::lfres); };
    btnWeight.onClick   = [this] { cycleChoice (ParamIDs::weight); };
    btnShow.onClick     = [this] { cycleChoice (ParamIDs::show); };
    btnDetect.onClick   = [this] { cycleChoice (ParamIDs::detect); };
    btnResponse.onClick = [this] { cycleChoice (ParamIDs::response); };
    btnFreeze.onClick   = [this] { toggleBool (ParamIDs::freeze); };
    btnHold.onClick     = [this] { toggleBool (ParamIDs::peakHold); };
    btnClear.onClick    = [this] { clearAll(); };

    navLeft.onClick  = [this] { pan (-0.15f, 0.0f); };
    navRight.onClick = [this] { pan (0.15f, 0.0f); };
    navUp.onClick    = [this] { pan (0.0f, 0.15f); };
    navDown.onClick  = [this] { pan (0.0f, -0.15f); };
    zoomIn.onClick   = [this] { zoom (0.75f); };
    zoomOut.onClick  = [this] { zoom (1.0f / 0.75f); };
    btnResetZoom.onClick = [this] { resetZoom(); };

    rebuildFFT (14);
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
    for (auto& h : holdDb)
        std::fill (h.begin(), h.end(), -200.0f);

    meterHoldDb.fill (-200.0f);
    proc.clipped[0].store (false);
    proc.clipped[1].store (false);
}

//==============================================================================
// Navigate / Zoom
void SpectrumScopeEditor::pan (float dxFrac, float dyFrac)
{
    {
        const double lmin = std::log ((double) viewFMin), lmax = std::log ((double) viewFMax);
        double shift = (double) dxFrac * (lmax - lmin);
        shift = juce::jlimit (std::log (1.0) - lmin, std::log (24000.0) - lmax, shift);
        viewFMin = (float) std::exp (lmin + shift);
        viewFMax = (float) std::exp (lmax + shift);
    }
    {
        float shift = dyFrac * (viewDbTop - viewDbBot);
        shift = juce::jlimit (-140.0f - viewDbBot, 24.0f - viewDbTop, shift);
        viewDbTop += shift;
        viewDbBot += shift;
    }
}

void SpectrumScopeEditor::zoom (float factor)
{
    {
        const double lmin = std::log ((double) viewFMin), lmax = std::log ((double) viewFMax);
        const double c = 0.5 * (lmin + lmax);
        const double maxHalf = 0.5 * (std::log (24000.0) - std::log (1.0));
        const double half = juce::jlimit (0.5 * std::log (1.3), maxHalf, 0.5 * (lmax - lmin) * (double) factor);
        viewFMin = (float) std::exp (c - half);
        viewFMax = (float) std::exp (c + half);
    }
    {
        const float c = 0.5f * (viewDbTop + viewDbBot);
        const float half = juce::jlimit (5.0f, 82.0f, 0.5f * (viewDbTop - viewDbBot) * factor);
        viewDbTop = c + half;
        viewDbBot = c - half;
    }
    pan (0.0f, 0.0f); // บีบให้อยู่ในขอบเขต
}

void SpectrumScopeEditor::resetZoom()
{
    viewFMin = defFMin;
    viewFMax = defFMax;
    viewDbTop = defDbTop;
    viewDbBot = defDbBot;
}

//==============================================================================
// FFT
void SpectrumScopeEditor::rebuildFFT (int order)
{
    fftOrder = order;
    fftSize  = 1 << order;
    fft      = std::make_unique<juce::dsp::FFT> (order);
    window   = std::make_unique<juce::dsp::WindowingFunction<float>> ((size_t) fftSize,
                                                                       juce::dsp::WindowingFunction<float>::hann);
    fftBuf.assign ((size_t) fftSize * 2, 0.0f);
    sigL.assign ((size_t) fftSize, 0.0f);
    sigR.assign ((size_t) fftSize, 0.0f);
    sigA.assign ((size_t) fftSize, 0.0f);
    sigB.assign ((size_t) fftSize, 0.0f);

    const size_t nb = (size_t) fftSize / 2 + 1;
    for (int c = 0; c < 2; ++c)
    {
        smoothPow[(size_t) c].assign (nb, 0.0f);
        holdDb[(size_t) c].assign (nb, -200.0f);
        curveDb[(size_t) c].assign (nb, -200.0f);
    }
    weightMode = -1; // บังคับคำนวณน้ำหนักใหม่
}

void SpectrumScopeEditor::rebuildWeighting()
{
    weightMode = choiceIndex (ParamIDs::weight);
    weightFs   = fs;

    const size_t nb = (size_t) fftSize / 2 + 1;
    weightDb.assign (nb, 0.0f);
    if (weightMode == 0)
        return;

    const double binHz = fs / (double) fftSize;
    const double c1 = 20.6 * 20.6, c2 = 107.7 * 107.7, c3 = 737.9 * 737.9, c4 = 12194.0 * 12194.0;

    for (size_t k = 1; k < nb; ++k)
    {
        const double f2 = (double) k * binHz * (double) k * binHz;
        double db;
        if (weightMode == 1) // A-weighting
        {
            const double ra = (c4 * f2 * f2) / ((f2 + c1) * std::sqrt ((f2 + c2) * (f2 + c3)) * (f2 + c4));
            db = 20.0 * std::log10 (ra) + 2.0;
        }
        else // C-weighting
        {
            const double rc = (c4 * f2) / ((f2 + c1) * (f2 + c4));
            db = 20.0 * std::log10 (rc) + 0.06;
        }
        weightDb[k] = (float) db;
    }
}

void SpectrumScopeEditor::analyse (const std::vector<float>& signal, int curve, bool useRms, float alpha, bool holdOn)
{
    std::copy (signal.begin(), signal.end(), fftBuf.begin());
    std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);

    window->multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
    fft->performRealOnlyForwardTransform (fftBuf.data(), true);

    const float norm = 4.0f / (float) fftSize; // ไซน์เต็มสเกลแอมพลิจูด 1.0 => 0 dB
    const int nb = fftSize / 2;

    auto& sp = smoothPow[(size_t) curve];
    auto& hd = holdDb[(size_t) curve];
    auto& out = curveDb[(size_t) curve];

    for (int k = 0; k <= nb; ++k)
    {
        const float re = fftBuf[(size_t) (2 * k)];
        const float im = fftBuf[(size_t) (2 * k + 1)];
        const float amp = std::sqrt (re * re + im * im) * norm;
        const float pw = amp * amp;

        auto& s = sp[(size_t) k];
        s = useRms ? (alpha * s + (1.0f - alpha) * pw)
                   : juce::jmax (pw, s * alpha);

        const float db = 10.0f * std::log10 (juce::jmax (s, 1.0e-12f)) + weightDb[(size_t) k];
        out[(size_t) k] = db;
        hd[(size_t) k] = holdOn ? juce::jmax (hd[(size_t) k], db) : -200.0f;
    }
}

void SpectrumScopeEditor::computeSpectrum()
{
    const int n = fftSize;
    proc.copyLatest (0, sigL.data(), n);
    proc.copyLatest (1, sigR.data(), n);

    const int mode = choiceIndex (ParamIDs::show);
    const bool useRms = choiceIndex (ParamIDs::detect) == 0;
    const float tau = kResponseMs[juce::jlimit (0, 5, choiceIndex (ParamIDs::response))] / 1000.0f;
    const float alpha = std::exp (-1.0f / (kTimerHz * tau));
    const bool holdOn = boolValue (ParamIDs::peakHold);

    switch (mode)
    {
        case 0:
            analyse (sigL, 0, useRms, alpha, holdOn);
            analyse (sigR, 1, useRms, alpha, holdOn);
            nCurves = 2;
            break;
        case 1:
            analyse (sigL, 0, useRms, alpha, holdOn);
            nCurves = 1;
            break;
        case 2:
            analyse (sigR, 0, useRms, alpha, holdOn);
            nCurves = 1;
            break;
        default:
            for (int i = 0; i < n; ++i)
                sigA[(size_t) i] = (mode == 3) ? 0.5f * (sigL[(size_t) i] + sigR[(size_t) i])
                                               : 0.5f * (sigL[(size_t) i] - sigR[(size_t) i]);
            analyse (sigA, 0, useRms, alpha, holdOn);
            nCurves = 1;
            break;
    }
}

void SpectrumScopeEditor::updateScope()
{
    constexpr int n = 2048;
    std::array<float, n> l {}, r {};
    proc.copyLatest (0, l.data(), n);
    proc.copyLatest (1, r.data(), n);

    scopePts.clear();
    for (int i = 0; i < n; i += 2)
    {
        const float m = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        const float s = 0.5f * (r[(size_t) i] - l[(size_t) i]);
        const float amp = std::sqrt (m * m + s * s) * 1.41421356f;
        const float rn = juce::jlimit (0.0f, 1.0f, (dbOf (amp) + 80.0f) / 80.0f);
        const float a = std::atan2 (s, std::abs (m));
        scopePts.push_back ({ rn * std::sin (a), rn * std::cos (a) });
    }
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

    const int order = 15 - choiceIndex (ParamIDs::lfres);
    if (order != fftOrder)
        rebuildFFT (order);

    if (weightMode != choiceIndex (ParamIDs::weight) || weightFs != fs)
        rebuildWeighting();

    if (! boolValue (ParamIDs::freeze))
        computeSpectrum();

    updateScope();
    updateMeters();
    repaint();
}

//==============================================================================
// การวาด
float SpectrumScopeEditor::xOfFreq (double f) const
{
    const double lmin = std::log ((double) viewFMin), lmax = std::log ((double) viewFMax);
    return plotArea.getX() + (float) ((std::log (juce::jmax (f, 1.0e-3)) - lmin) / (lmax - lmin)) * plotArea.getWidth();
}

double SpectrumScopeEditor::freqOfX (float x) const
{
    const double lmin = std::log ((double) viewFMin), lmax = std::log ((double) viewFMax);
    return std::exp (lmin + (double) ((x - plotArea.getX()) / plotArea.getWidth()) * (lmax - lmin));
}

float SpectrumScopeEditor::yOfDb (float db) const
{
    return plotArea.getY() + (viewDbTop - db) / (viewDbTop - viewDbBot) * plotArea.getHeight();
}

juce::Path SpectrumScopeEditor::makeCurve (const std::vector<float>& db, bool& ok, float& firstX, float& lastX) const
{
    juce::Path p;
    ok = false;

    const int nb = fftSize / 2;
    const double binHz = fs / (double) fftSize;
    const float x0 = plotArea.getX();
    const int w = (int) plotArea.getWidth();

    for (int px = 0; px <= w; ++px)
    {
        const double kLo = freqOfX (x0 + (float) px) / binHz;
        const double kHi = freqOfX (x0 + (float) px + 1.0f) / binHz;
        float v;

        if (kHi - kLo < 1.0)
        {
            const double kc = 0.5 * (kLo + kHi);
            if (kc < 1.0 || kc >= (double) nb)
                continue;
            const int i = (int) kc;
            const float fr = (float) (kc - (double) i);
            v = db[(size_t) i] + (db[(size_t) i + 1] - db[(size_t) i]) * fr;
        }
        else
        {
            const int iLo = juce::jmax (1, (int) std::ceil (kLo));
            const int iHi = juce::jmin (nb, (int) std::floor (kHi));
            if (iLo > iHi)
                continue;
            v = -200.0f;
            for (int i = iLo; i <= iHi; ++i)
                v = juce::jmax (v, db[(size_t) i]);
        }

        const float x = x0 + (float) px;
        const float y = juce::jlimit (plotArea.getY(), plotArea.getBottom(), yOfDb (v));
        if (! ok)
        {
            p.startNewSubPath (x, y);
            firstX = x;
            ok = true;
        }
        else
        {
            p.lineTo (x, y);
        }
        lastX = x;
    }
    return p;
}

void SpectrumScopeEditor::paintSpectrum (juce::Graphics& g)
{
    drawPanelFrame (g, specArea);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff000a24), plotArea.getX(), plotArea.getY(),
                                             juce::Colour (0xff04124a), plotArea.getX(), plotArea.getBottom(), false));
    g.fillRect (plotArea);

    const float sc = (float) getHeight() / 727.0f;
    g.setFont (juce::jmax (9.0f, 17.0f * sc));

    // เส้นกริด dB
    const float range = viewDbTop - viewDbBot;
    const int step = range >= 60.0f ? 20 : range >= 30.0f ? 10 : range >= 15.0f ? 5 : 2;
    for (int d = (int) std::ceil (viewDbBot / (float) step) * step; d <= (int) viewDbTop; d += step)
    {
        const float y = yOfDb ((float) d);
        g.setColour (kGrid.withAlpha (0.75f));
        g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());
        g.setColour (kLabel);
        g.drawText (juce::String (d), juce::Rectangle<float> (specArea.getX(), y - 10.0f, plotArea.getX() - specArea.getX() - 6.0f, 20.0f),
                    juce::Justification::centredRight, false);
    }
    g.setColour (kLabel);
    g.drawText ("dB", juce::Rectangle<float> (plotArea.getX() + 4.0f, plotArea.getY() + 2.0f, 40.0f, 18.0f),
                juce::Justification::centredLeft, false);

    // เส้นกริดความถี่
    static const double freqs[] = { 7.8125, 15.625, 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    static const char* names[]  = { "8", "16", "31", "62", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    for (int i = 0; i < 12; ++i)
    {
        const float x = xOfFreq (freqs[i]);
        if (x < plotArea.getX() || x > plotArea.getRight())
            continue;
        g.setColour (kGrid.withAlpha (0.75f));
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        g.setColour (kLabel);
        g.drawText (names[i], juce::Rectangle<float> (x - 24.0f, plotArea.getBottom() + 4.0f, 48.0f, 20.0f),
                    juce::Justification::centred, false);
    }
    g.drawText ("Hz", juce::Rectangle<float> (plotArea.getRight() - 40.0f, plotArea.getBottom() + 4.0f, 40.0f, 20.0f),
                juce::Justification::centredRight, false);

    // กราฟสเปกตรัม
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plotArea.toNearestInt());

    const bool holdOn = boolValue (ParamIDs::peakHold);
    for (int c = 0; c < nCurves; ++c)
    {
        const juce::Colour col = (nCurves == 2 && c == 1) ? kOrange : kCyan;

        bool ok = false;
        float fx = 0.0f, lx = 0.0f;

        if (holdOn)
        {
            bool okH = false;
            float hf = 0.0f, hl = 0.0f;
            auto hp = makeCurve (holdDb[(size_t) c], okH, hf, hl);
            if (okH)
            {
                g.setColour (juce::Colour (0xffffe14d).withAlpha (0.85f));
                g.strokePath (hp, juce::PathStrokeType (1.0f));
            }
        }

        auto path = makeCurve (curveDb[(size_t) c], ok, fx, lx);
        if (! ok)
            continue;

        juce::Path fill (path);
        fill.lineTo (lx, plotArea.getBottom());
        fill.lineTo (fx, plotArea.getBottom());
        fill.closeSubPath();
        g.setColour (col.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }
}

void SpectrumScopeEditor::paintScope (juce::Graphics& g)
{
    drawPanelFrame (g, scopeArea);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff04124a), scopeArea.getCentreX(), scopeArea.getBottom(),
                                             juce::Colour (0xff000a24), scopeArea.getCentreX(), scopeArea.getY(), false));
    g.fillRect (scopeArea);

    const float sc = (float) getHeight() / 727.0f;
    const float labelH = 30.0f * sc;
    const float cx = scopeArea.getCentreX();
    const float cy = scopeArea.getBottom() - labelH - 4.0f;
    const float R = juce::jmin (scopeArea.getWidth() * 0.5f - 10.0f, cy - scopeArea.getY() - 8.0f);

    // วงแหวน (0, -20, -40, -60 dB)
    for (int i = 1; i <= 4; ++i)
    {
        const float rr = R * (float) i / 4.0f;
        juce::Path arc;
        arc.addCentredArc (cx, cy, rr, rr, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (juce::Colour (0xff1f5fff));
        g.strokePath (arc, juce::PathStrokeType (1.4f));
    }

    g.setColour (juce::Colour (0xff1f5fff));
    g.drawVerticalLine ((int) cx, cy - R, cy);
    g.drawHorizontalLine ((int) cy, cx - R, cx + R);

    // เส้นประ 45 องศา (L / R)
    const float dash[] = { 6.0f, 5.0f };
    const float d = R * 0.7071f;
    g.drawDashedLine (juce::Line<float> (cx, cy, cx - d, cy - d), dash, 2, 1.2f);
    g.drawDashedLine (juce::Line<float> (cx, cy, cx + d, cy - d), dash, 2, 1.2f);

    // จุดสัญญาณ
    for (auto& p : scopePts)
    {
        const float x = cx + p.x * R;
        const float y = cy - p.y * R;
        g.setColour (kCyan.withAlpha (0.55f));
        g.fillRect (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
    }

    // ป้ายกำกับ
    g.setColour (kLabel);
    g.setFont (juce::jmax (9.0f, 19.0f * sc));
    g.drawText ("Left",  juce::Rectangle<float> (scopeArea.getX() + 10.0f, scopeArea.getY() + 6.0f, 100.0f, 22.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("Right", juce::Rectangle<float> (scopeArea.getRight() - 110.0f, scopeArea.getY() + 6.0f, 100.0f, 22.0f),
                juce::Justification::centredRight, false);

    g.setColour (juce::Colour (0xff3d8bff));
    g.setFont (juce::jmax (9.0f, 17.0f * sc));
    g.drawText ("AntiPhase", juce::Rectangle<float> (scopeArea.getX() + 8.0f, cy - 24.0f, 120.0f, 20.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("AntiPhase", juce::Rectangle<float> (scopeArea.getRight() - 128.0f, cy - 24.0f, 120.0f, 20.0f),
                juce::Justification::centredRight, false);

    g.setColour (kLabel);
    static const char* axis[] = { "0", "-20", "-40", "-60", "-80", "-60", "-40", "-20", "0" };
    for (int i = 0; i < 9; ++i)
    {
        const float x = cx + R * (float) (i - 4) / 4.0f;
        g.drawText (axis[i], juce::Rectangle<float> (x - 22.0f, cy + 4.0f, 44.0f, 20.0f), juce::Justification::centred, false);
    }
}

void SpectrumScopeEditor::paintMeters (juce::Graphics& g)
{
    const float sc = (float) getHeight() / 727.0f;
    const float labelW = meterArea.getWidth() * 0.17f;
    const float barsW = meterArea.getWidth() - 2.0f * labelW;
    const float barW = barsW / 3.0f * 0.78f;
    const float gap = (barsW - 3.0f * barW) / 4.0f;
    const float top = meterArea.getY() + 30.0f * sc;
    const float bot = meterArea.getBottom() - 44.0f * sc;
    const float h = bot - top;

    auto yOfMeterDb = [&] (float db) { return top + juce::jlimit (0.0f, 1.0f, -db / kMeterRangeDb) * h; };

    // สเกลซ้าย/ขวา
    g.setFont (juce::jmax (9.0f, 15.0f * sc));
    for (int d = 0; d >= -39; d -= 3)
    {
        const float y = yOfMeterDb ((float) d);
        g.setColour (kLabel);
        g.drawText (juce::String (d), juce::Rectangle<float> (meterArea.getX(), y - 9.0f, labelW - 4.0f, 18.0f),
                    juce::Justification::centredRight, false);
        g.drawText (juce::String (d), juce::Rectangle<float> (meterArea.getRight() - labelW + 4.0f, y - 9.0f, labelW - 4.0f, 18.0f),
                    juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff6a7d8c));
        g.drawHorizontalLine ((int) y, meterArea.getX() + labelW - 3.0f, meterArea.getX() + labelW);
        g.drawHorizontalLine ((int) y, meterArea.getRight() - labelW, meterArea.getRight() - labelW + 3.0f);
    }

    static const char* names[] = { "L", "M", "R" };
    for (int i = 0; i < 3; ++i)
    {
        const float bx = meterArea.getX() + labelW + gap + (float) i * (barW + gap);
        juce::Rectangle<float> bar (bx, top, barW, h);

        // ชื่อช่อง
        g.setColour (kLabel);
        g.drawText (names[i], juce::Rectangle<float> (bx - 10.0f, top - 22.0f * sc, barW + 20.0f, 14.0f * sc),
                    juce::Justification::centred, false);

        // ตัวบอก clip (L และ R เท่านั้น)
        const bool clipOn = (i != 1) && clipShown[(size_t) (i == 0 ? 0 : 1)];
        g.setColour (clipOn ? juce::Colour (0xffff2d2d) : juce::Colour (0xff6b1010));
        g.fillRect (juce::Rectangle<float> (bx, top - 8.0f * sc, barW, 4.0f * sc));

        g.setColour (juce::Colour (0xff0a0f18));
        g.fillRect (bar);

        const float lvDb = dbOf (meterLevel[(size_t) i]);
        const float yLv = yOfMeterDb (lvDb);
        if (yLv < bot)
        {
            juce::ColourGradient grad (juce::Colour (0xffff3b30), 0.0f, top, juce::Colour (0xff30d158), 0.0f, bot, false);
            grad.addColour (0.18, juce::Colour (0xffffd60a));
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (bx + 2.0f, yLv, barW - 4.0f, bot - yLv));
        }

        g.setColour (juce::Colour (0xff6a7d8c));
        g.drawRect (bar, 1.5f);

        // ช่องแสดงค่าพีคสูงสุด
        juce::Rectangle<float> box (bx - 4.0f, bot + 8.0f * sc, barW + 8.0f, 26.0f * sc);
        g.setColour (juce::Colour (0xff04070c));
        g.fillRect (box);
        g.setColour (juce::Colour (0xff6a7d8c));
        g.drawRect (box, 1.5f);
        g.setColour (kLabel);
        g.setFont (juce::jmax (9.0f, 15.0f * sc));
        const float hv = meterHoldDb[(size_t) i];
        g.drawText (hv <= -150.0f ? juce::String::fromUTF8 ("-\xE2\x88\x9E") : juce::String (hv, 1), box,
                    juce::Justification::centred, false);
    }
}

void SpectrumScopeEditor::paintControlLabels (juce::Graphics& g)
{
    const float sc = (float) getHeight() / 727.0f;
    g.setColour (kLabel);
    g.setFont (juce::jmax (10.0f, 19.0f * sc));
    for (auto& [text, r] : controlLabels)
        g.drawText (text, r, juce::Justification::centred, false);
}

void SpectrumScopeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3138), 0.0f, 0.0f,
                                             juce::Colour (0xff14181d), 0.0f, (float) getHeight(), false));
    g.fillAll();

    paintSpectrum (g);
    paintScope (g);

    // แผงควบคุมด้านขวาบนและกลางล่าง
    g.setColour (juce::Colour (0xff0b1424));
    g.fillRoundedRectangle (navArea, 3.0f);
    g.setColour (juce::Colour (0xff6a7d8c));
    g.drawRoundedRectangle (navArea, 3.0f, 1.5f);

    paintControlLabels (g);
    paintMeters (g);
}

void SpectrumScopeEditor::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();

    specArea  = { 0.009f * W, 0.009f * H, 0.786f * W, 0.502f * H };
    navArea   = { 0.805f * W, 0.009f * H, 0.186f * W, 0.502f * H };
    scopeArea = { 0.009f * W, 0.535f * H, 0.565f * W, 0.455f * H };
    meterArea = { 0.800f * W, 0.535f * H, 0.192f * W, 0.455f * H };

    plotArea = { specArea.getX() + 0.065f * specArea.getWidth(), specArea.getY() + 0.035f * specArea.getHeight(),
                 specArea.getWidth() * 0.935f, specArea.getHeight() * 0.88f };

    auto setB = [] (juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt());
    };

    controlLabels.clear();
    auto addLabel = [this] (const juce::String& t, float x, float y, float w, float h)
    {
        controlLabels.push_back ({ t, juce::Rectangle<float> (x, y, w, h).toNearestInt() });
    };

    // --- Navigate / Zoom ---
    {
        const float bs = juce::jmin (navArea.getWidth() * 0.22f, navArea.getHeight() * 0.12f);
        const float cx = navArea.getCentreX();
        const float y0 = navArea.getY();
        const float cy = y0 + 2.2f * bs;

        addLabel ("Navigate", navArea.getX(), y0 + 0.05f * bs, navArea.getWidth(), 0.6f * bs);
        setB (navUp,    cx - bs * 0.5f, cy - 1.5f * bs, bs, bs);
        setB (navDown,  cx - bs * 0.5f, cy + 0.5f * bs, bs, bs);
        setB (navLeft,  cx - 1.5f * bs, cy - 0.5f * bs, bs, bs);
        setB (navRight, cx + 0.5f * bs, cy - 0.5f * bs, bs, bs);

        addLabel ("Zoom", navArea.getX(), y0 + 3.9f * bs, navArea.getWidth(), 0.6f * bs);
        setB (zoomOut, cx - 1.1f * bs, y0 + 4.6f * bs, bs, bs);
        setB (zoomIn,  cx + 0.1f * bs, y0 + 4.6f * bs, bs, bs);
        setB (btnResetZoom, cx - 1.5f * bs, y0 + 5.85f * bs, 3.0f * bs, 0.8f * bs);
    }

    // --- ปุ่มควบคุมกลางล่าง (2 คอลัมน์ x 4 แถว) ---
    {
        const float x = 0.603f * W, y = 0.535f * H, w = 0.177f * W, h = 0.455f * H;
        const float colW = w / 2.0f, rowH = h / 4.0f;

        struct Cell { const char* label; PanelButton* btn; };
        const Cell cells[] = { { "LF res", &btnLf },       { "Weight", &btnWeight },
                               { "Freeze", &btnFreeze },   { "Show", &btnShow },
                               { "Peak Hold", &btnHold },  { "Clear", &btnClear },
                               { "Detect", &btnDetect },   { "Response", &btnResponse } };

        for (int i = 0; i < 8; ++i)
        {
            const int r = i / 2, c = i % 2;
            const float cx0 = x + (float) c * colW, cy0 = y + (float) r * rowH;
            addLabel (cells[i].label, cx0, cy0, colW, 0.3f * rowH);
            setB (*cells[i].btn, cx0 + 0.06f * colW, cy0 + 0.32f * rowH, 0.88f * colW, 0.58f * rowH);
        }
    }

    repaint();
}
