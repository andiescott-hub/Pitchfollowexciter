#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0c0f16), panel (0xff151a25), raised (0xff1e2533), line (0xff2a3345), ink (0xffe9edf5), mute (0xff8d98ad), hi (0xfff4efe4);
    const float fLo = 20.0f, fHi = 20000.0f, top = 30.0f, bottom = 24.0f;
    const float tau = 6.2831853f;

    juce::String noteName (float hz)
    {
        static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const int m = (int) std::lround (69.0f + 12.0f * std::log2 (hz / 440.0f));
        return juce::String (n[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    }
}

//==============================================================================
PfxLook::PfxLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::arrowColourId, mute);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, line);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, ink);
}

void PfxLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f, cx = area.getCentreX(), cy = area.getCentreY();
    const float angle = a0 + pos * (a1 - a0);
    juce::Path track, value;
    track.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
    value.addCentredArc (cx, cy, r, r, 0.0f, a0, angle, true);
    const juce::PathStrokeType stroke (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (line);
    g.strokePath (track, stroke);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, stroke);
    g.setColour (raised);
    g.fillEllipse (cx - r * 0.7f, cy - r * 0.7f, r * 1.4f, r * 1.4f);
    g.setColour (ink);
    g.drawLine (cx + std::sin (angle) * r * 0.25f, cy - std::cos (angle) * r * 0.25f,
                cx + std::sin (angle) * r * 0.62f, cy - std::cos (angle) * r * 0.62f, 2.2f);
}

//==============================================================================
PfxDisplay::PfxDisplay (PfxProcessor& p) : proc (p)
{
    fft.init (kFft);
    scopeIn.assign (kFft, 0.0f); scopeOut.assign (kFft, 0.0f);
    dbIn.assign (kFft / 2, -140.0f); dbOut.assign (kFft / 2, -140.0f);
    window.resize (kFft); re.resize (kFft); im.resize (kFft);
    for (int i = 0; i < kFft; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (6.283185307179586 * i / kFft);
    startTimerHz (30);
}

juce::Colour PfxDisplay::colour (int b)
{
    static const juce::Colour c[4] = { juce::Colour (0xfff5a742), juce::Colour (0xfff0648c), juce::Colour (0xff38cfc0), juce::Colour (0xff9a8cff) };
    return c[b & 3];
}

float PfxDisplay::xOf (float hz) const { return (float) getWidth() * std::log (hz / fLo) / std::log (fHi / fLo); }
float PfxDisplay::hzOf (float x) const { return fLo * std::pow (fHi / fLo, x / (float) getWidth()); }
float PfxDisplay::yOf (float db) const { return top + ((float) getHeight() - top - bottom) * (1.0f - (db + 24.0f) / 48.0f); }
float PfxDisplay::dbOf (float y) const { return 48.0f * (1.0f - (y - top) / ((float) getHeight() - top - bottom)) - 24.0f; }
float PfxDisplay::ySpec (float db) const { return top + ((float) getHeight() - top - bottom) * (-10.0f - db) / 80.0f; }

float PfxDisplay::get (int b, const char* name) const { return proc.apvts.getRawParameterValue (PfxProcessor::bandId (b, name))->load(); }

void PfxDisplay::set (int b, const char* name, float value)
{
    if (auto* p = proc.apvts.getParameter (PfxProcessor::bandId (b, name)))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PfxDisplay::gesture (int b, const char* name, bool begin)
{
    if (auto* p = proc.apvts.getParameter (PfxProcessor::bandId (b, name)))
        begin ? p->beginChangeGesture() : p->endChangeGesture();
}

int PfxDisplay::hitNode (juce::Point<float> pt) const
{
    for (int i = 0; i <= PfxProcessor::kBands; ++i) {            // selected band first
        const int b = i == 0 ? selected : i - 1;
        if (get (b, "on") > 0.5f && pt.getDistanceFrom ({ xOf (get (b, "freq")), yOf (get (b, "amount")) }) < 14.0f) return b;
    }
    return -1;
}

int PfxDisplay::hitRegion (juce::Point<float> pt) const
{
    int best = -1;
    float width = 1.0e9f;
    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        float lo, hi;
        pfx::bandEdges (get (b, "freq"), get (b, "focus"), lo, hi);
        const float x0 = xOf (lo), x1 = xOf (hi);
        if (pt.x >= x0 && pt.x <= x1 && x1 - x0 < width) { width = x1 - x0; best = b; }
    }
    return best;
}

// Level of harmonics 2..8 relative to the fundamental, for the current type and drive.
const float* PfxDisplay::harmonics (int b)
{
    auto& c = harm[b];
    const int type = (int) get (b, "type");
    const float drive = get (b, "drive");
    if (c.type == type && std::abs (c.drive - drive) < 1.0e-4f) return c.rel;
    c.type = type; c.drive = drive;
    pfx::Shaper sh;
    sh.set (type, drive);
    const int K = 256;
    float m1 = 1.0f;
    for (int k = 1; k <= 8; ++k) {
        float a = 0, bq = 0;
        for (int i = 0; i < K; ++i) {
            const float ph = tau * (float) i / (float) K, v = sh.curve (sh.d * 0.5f * std::sin (ph)) / sh.den;
            a += v * std::cos ((float) k * ph); bq += v * std::sin ((float) k * ph);
        }
        float mag = std::sqrt (a * a + bq * bq);
        if (k == 1) { m1 = juce::jmax (mag, 1.0e-9f); continue; }
        if (k == 2) mag = std::sqrt (mag * mag + (float) (K * K) * 0.0625f * sh.even * sh.even);   // the octave signal
        c.rel[k - 2] = 20.0f * std::log10 (mag / m1 + 1.0e-9f);
    }
    return c.rel;
}

void PfxDisplay::timerCallback()
{
    proc.readScope (scopeIn.data(), scopeOut.data(), kFft);
    const float bin = (float) proc.sampleRate() / (float) kFft;
    for (int pass = 0; pass < 2; ++pass) {
        const auto& src = pass == 0 ? scopeIn : scopeOut;
        auto& dst = pass == 0 ? dbIn : dbOut;
        for (int i = 0; i < kFft; ++i) { re[(size_t) i] = src[(size_t) i] * window[(size_t) i]; im[(size_t) i] = 0.0; }
        fft.run (re.data(), im.data());
        for (int k = 1; k < kFft / 2; ++k) {
            const float mag = (float) std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * 4.0f / (float) kFft;
            const float db = 20.0f * std::log10 (mag + 1.0e-9f) + 4.5f * std::log2 ((float) k * bin / 1000.0f);   // tilt so a mix reads flat
            dst[(size_t) k] = juce::jmax (db, dst[(size_t) k] - 2.5f);
        }
    }
    repaint();
}

void PfxDisplay::spectrumPath (juce::Path& p, const std::vector<float>& db) const
{
    const float bin = (float) proc.sampleRate() / (float) kFft, base = (float) getHeight() - bottom;
    const int last = kFft / 2 - 2;
    for (float px = 0.0f; px <= (float) getWidth(); px += 2.0f) {
        const float b0 = hzOf (px) / bin, b1 = hzOf (px + 2.0f) / bin;
        float v;
        if (b1 - b0 < 1.0f) {
            const int i = juce::jlimit (1, last, (int) b0);
            const float u = juce::jlimit (0.0f, 1.0f, b0 - (float) i);
            v = db[(size_t) i] * (1.0f - u) + db[(size_t) i + 1] * u;
        } else {
            v = -140.0f;
            for (int i = juce::jmax (1, (int) b0); (float) i < b1 && i <= last; ++i) v = juce::jmax (v, db[(size_t) i]);
        }
        const float y = juce::jmin (base, ySpec (v));
        if (px < 1.0f) p.startNewSubPath (px, y); else p.lineTo (px, y);
    }
}

void PfxDisplay::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight(), base = h - bottom;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff111622), 0, 0, juce::Colour (0xff0a0d14), 0, h, false));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // grid: decades, octave C markers along the top, Amount scale on the right
    g.setFont (11.0f);
    for (float f : { 30.0f, 40.0f, 60.0f, 70.0f, 80.0f, 90.0f, 300.0f, 400.0f, 600.0f, 700.0f, 800.0f, 900.0f, 3000.0f, 4000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f }) {
        g.setColour (line.withAlpha (0.25f));
        g.drawVerticalLine ((int) xOf (f), top - 8.0f, base);
    }
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = xOf (f);
        g.setColour (line.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, top - 8.0f, base);
        g.setColour (mute);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x - 20, (int) h - 19, 40, 14, juce::Justification::centred);
    }
    for (int o = 1; o <= 9; ++o) {
        const float f = 440.0f * std::pow (2.0f, (12.0f * (float) (o + 1) - 69.0f) / 12.0f), x = xOf (f);
        g.setColour (mute.withAlpha (0.6f));
        g.fillRect (x, top - 8.0f, 1.0f, 5.0f);
        g.drawText ("C" + juce::String (o), (int) x - 15, (int) top - 24, 30, 14, juce::Justification::centred);
    }
    for (float db : { -12.0f, 0.0f, 12.0f }) {
        g.setColour (line.withAlpha (std::abs (db) < 0.5f ? 0.9f : 0.35f));
        g.drawHorizontalLine ((int) yOf (db), 0.0f, w);
        g.setColour (mute);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", (int) w - 62, (int) yOf (db) - 15, 54, 14, juce::Justification::right);
    }

    // input spectrum
    {
        juce::Path p;
        spectrumPath (p, dbIn);
        p.lineTo (w, base); p.lineTo (0.0f, base); p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x80788cb4), 0, top, juce::Colour (0x1f788cb4), 0, base, false));
        g.fillPath (p);
    }

    bool anySolo = false;
    for (int b = 0; b < PfxProcessor::kBands; ++b) anySolo = anySolo || (get (b, "on") > 0.5f && get (b, "solo") > 0.5f);

    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        const auto col = colour (b);
        const bool dim = get (b, "bypass") > 0.5f || (anySolo && get (b, "solo") < 0.5f);
        const float fade = dim ? 0.35f : 1.0f;
        float lo, hi;
        pfx::bandEdges (get (b, "freq"), get (b, "focus"), lo, hi);
        const float x0 = xOf (lo), x1 = xOf (hi);
        g.setGradientFill (juce::ColourGradient (col.withAlpha ((b == selected ? 0.23f : 0.13f) * fade), 0, 0, col.withAlpha (0.03f), 0, base, false));
        g.fillRect (x0, 0.0f, x1 - x0, base);
        g.setColour (col.withAlpha ((b == selected ? 0.67f : 0.33f) * fade));
        g.fillRect (x0, 0.0f, 1.0f, base);
        g.fillRect (x1, 0.0f, 1.0f, base);

        // the note being followed, and the harmonics the saturator adds above it
        if (proc.gateOpen (b) && proc.trackedHz (b) > 0.0f) {
            const float hz = proc.trackedHz (b), xc = xOf (hz), q = pfx::autoQ (get (b, "freq")), amt = get (b, "amount"), yb = yOf (-24.0f);
            float shape[71];
            for (int i = 0; i <= 70; ++i) {
                const float dx = (float) (i * 2 - 70), r = hzOf (xc + dx) / hz;
                shape[i] = -10.0f * std::log10 (1.0f + q * q * (r - 1.0f / r) * (r - 1.0f / r)) - 40.0f * std::pow (std::abs (dx) / 70.0f, 4.0f);
            }
            auto spike = [&] (float centre, float level, float alpha) {
                juce::Path s;
                s.startNewSubPath (centre - 70.0f, yb);
                for (int i = 0; i <= 70; ++i) s.lineTo (centre - 70.0f + (float) (i * 2), juce::jmin (yb, yOf (level + shape[i])));
                s.lineTo (centre + 70.0f, yb);
                s.closeSubPath();
                g.setColour (col.withAlpha (0.33f * alpha * fade));
                g.fillPath (s);
                g.setColour (col.withAlpha (alpha * fade));
                g.strokePath (s, juce::PathStrokeType (1.5f));
            };
            spike (xc, amt, 1.0f);
            if (get (b, "bypass") < 0.5f) {
                const float* rel = harmonics (b);
                for (int k = 2; k <= 8; ++k)
                    if (rel[k - 2] > -60.0f && (float) k * hz < fHi && amt + rel[k - 2] > -24.0f) spike (xOf ((float) k * hz), amt + rel[k - 2], 0.6f);
            }
            g.setColour (col.withAlpha (fade));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (noteName (hz), (int) xc - 24, (int) juce::jmax (top + 2.0f, yOf (amt) - 24.0f), 48, 14, juce::Justification::centred);
        }
    }

    // output spectrum
    {
        juce::Path p;
        spectrumPath (p, dbOut);
        g.setColour (hi.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        const float x = xOf (get (b, "freq")), y = yOf (get (b, "amount"));
        const auto col = colour (b);
        if (b == selected) { g.setColour (col); g.drawEllipse (x - 13.0f, y - 13.0f, 26.0f, 26.0f, 1.5f); }
        g.setColour (get (b, "bypass") > 0.5f ? juce::Colour (0xff3a4356) : col);
        g.fillEllipse (x - 9.0f, y - 9.0f, 18.0f, 18.0f);
        g.setColour (bg);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), (int) x - 9, (int) y - 8, 18, 16, juce::Justification::centred);
    }
}

void PfxDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (hitNode (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor
                    : hitRegion (e.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
}

void PfxDisplay::mouseDown (const juce::MouseEvent& e)
{
    int b = hitNode (e.position);
    const bool node = b >= 0;
    if (b < 0) b = hitRegion (e.position);
    if (b < 0) return;
    selected = b;
    if (onSelect) onSelect (b);
    dragging = b;
    grab = { e.position.x - xOf (get (b, "freq")), node ? e.position.y - yOf (get (b, "amount")) : -1.0e6f };
    gesture (b, "freq", true);
    gesture (b, "amount", true);
}

void PfxDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const int b = dragging;
    const float focus = get (b, "focus");
    float fc = juce::jlimit (30.0f, 8000.0f, hzOf (e.position.x - grab.x));
    if (! e.mods.isAltDown())                       // snap: band edges lock together when they come close
        for (int o = 0; o < PfxProcessor::kBands; ++o) {
            if (o == b || get (o, "on") < 0.5f) continue;
            float ol, oh, l, h;
            pfx::bandEdges (get (o, "freq"), get (o, "focus"), ol, oh);
            pfx::bandEdges (fc, focus, l, h);
            const bool below = std::abs (xOf (h) - xOf (ol)) < 9.0f, above = std::abs (xOf (l) - xOf (oh)) < 9.0f;
            if (! below && ! above) continue;
            for (int i = 0; i < 3; ++i) {           // the window width depends on the centre, so settle in a few steps
                const float st = pfx::autoWindowSt (fc) * pfx::focusScale (focus);
                fc = juce::jlimit (30.0f, 8000.0f, below ? ol * std::pow (2.0f, -st / 12.0f) : oh * std::pow (2.0f, st / 12.0f));
            }
            break;
        }
    set (b, "freq", fc);
    if (grab.y > -1.0e5f) set (b, "amount", juce::jlimit (-24.0f, 18.0f, dbOf (e.position.y - grab.y)));
}

void PfxDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    gesture (dragging, "freq", false);
    gesture (dragging, "amount", false);
    dragging = -1;
}

void PfxDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int b = hitNode (e.position); b >= 0) { set (b, "on", 0.0f); return; }   // double-click a handle to remove it
    if (hitRegion (e.position) >= 0) return;
    for (int b = 0; b < PfxProcessor::kBands; ++b)
        if (get (b, "on") < 0.5f) {                                                    // double-click empty space to add a band
            set (b, "freq", juce::jlimit (30.0f, 8000.0f, hzOf (e.position.x)));
            set (b, "amount", juce::jlimit (-24.0f, 18.0f, dbOf (e.position.y)));
            set (b, "on", 1.0f);
            selected = b;
            if (onSelect) onSelect (b);
            return;
        }
}

void PfxDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int b = hitNode (e.position);
    if (b < 0) b = hitRegion (e.position);
    if (b >= 0) set (b, "focus", juce::jlimit (0.0f, 1.0f, get (b, "focus") + (wheel.deltaY > 0 ? 0.04f : -0.04f)));
}

//==============================================================================
PfxEditor::PfxEditor (PfxProcessor& p) : AudioProcessorEditor (p), proc (p), display (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (display);
    display.onSelect = [this] (int b) { selectBand (b); };

    static const char* names[4] = { "AMOUNT", "DRIVE", "FOCUS", "GLIDE" };
    for (int i = 0; i < 4; ++i) {
        knob[i].setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob[i].setTextBoxStyle (juce::Slider::TextBoxBelow, true, 96, 18);
        addAndMakeVisible (knob[i]);
        knobLabel[i].setText (names[i], juce::dontSendNotification);
        knobLabel[i].setFont (juce::FontOptions (11.0f));
        knobLabel[i].setJustificationType (juce::Justification::centred);
        knobLabel[i].setColour (juce::Label::textColourId, mute);
        addAndMakeVisible (knobLabel[i]);
    }
    bandName.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (bandName);
    hint.setText ("Double-click the display to add a band, double-click a handle to remove it. Drag to move, drag the handle up or down for Amount, "
                  "scroll for Focus. Edges snap together (hold Alt to place freely).", juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, mute);
    hint.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (hint);

    auto combo = [this] (juce::ComboBox& c, juce::Label& label, const char* text, const char* paramId) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
            c.addItemList (choice->choices, 1);
        addAndMakeVisible (c);
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, mute);
        addAndMakeVisible (label);
    };
    type.addItemList ({ "Tube", "Tape", "Transistor" }, 1);
    addAndMakeVisible (type);
    combo (mode, modeLabel, "Mode", "mode");
    combo (oversampling, osLabel, "Oversampling", "oversampling");
    combo (listen, listenLabel, "Listen", "listen");
    combo (key, keyLabel, "Key", "key");
    modeAtt   = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mode", mode);
    osAtt     = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "oversampling", oversampling);
    listenAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "listen", listen);
    keyAtt    = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "key", key);

    for (auto* b : { &solo, &bypass, &link }) { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
    linkAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "link", link);

    selectBand (0);
    setResizable (true, true);
    setResizeLimits (760, 460, 2000, 1300);
    setSize (980, 620);
    startTimerHz (10);
}

PfxEditor::~PfxEditor() { setLookAndFeel (nullptr); }

void PfxEditor::selectBand (int b)
{
    band = b;
    static const char* ids[4] = { "amount", "drive", "focus", "glide" };
    for (int i = 0; i < 4; ++i) {
        knobAtt[i].reset();
        knobAtt[i] = std::make_unique<APVTS::SliderAttachment> (proc.apvts, PfxProcessor::bandId (b, ids[i]), knob[i]);
        knob[i].setColour (juce::Slider::rotarySliderFillColourId, PfxDisplay::colour (b));
    }
    // Focus and Glide read out in real units, which depend on where the band sits
    auto freq = [this, b] { return proc.apvts.getRawParameterValue (PfxProcessor::bandId (b, "freq"))->load(); };
    knob[2].textFromValueFunction = [freq] (double v) {
        const float st = pfx::autoWindowSt (freq()) * pfx::focusScale ((float) v);
        return juce::String (v < 0.4 ? "Tight" : v > 0.6 ? "Loose" : "Auto") + " \xc2\xb1" + juce::String (st, 1) + " st";
    };
    knob[3].textFromValueFunction = [freq] (double v) {
        if (pfx::glideFixed ((float) v)) return juce::String ("Fixed");
        const float ms = pfx::glideMs (freq(), (float) v);
        return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0f, 1) + " s";
    };
    knob[2].updateText(); knob[3].updateText();

    typeAtt.reset();   typeAtt   = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, PfxProcessor::bandId (b, "type"), type);
    soloAtt.reset();   soloAtt   = std::make_unique<APVTS::ButtonAttachment>   (proc.apvts, PfxProcessor::bandId (b, "solo"), solo);
    bypassAtt.reset(); bypassAtt = std::make_unique<APVTS::ButtonAttachment>   (proc.apvts, PfxProcessor::bandId (b, "bypass"), bypass);
    bandName.setText ("Band " + juce::String (b + 1), juce::dontSendNotification);
    bandName.setColour (juce::Label::textColourId, PfxDisplay::colour (b));
    display.selected = b;
}

void PfxEditor::timerCallback()
{
    const float f = proc.apvts.getRawParameterValue (PfxProcessor::bandId (band, "freq"))->load();
    if (std::abs (f - lastFreq) > 0.01f) { lastFreq = f; knob[2].updateText(); knob[3].updateText(); }
}

void PfxEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().removeFromBottom (176).withTrimmedBottom (26).reduced (10, 6).toFloat(), 10.0f);
}

void PfxEditor::resized()
{
    auto area = getLocalBounds();
    hint.setBounds (area.removeFromBottom (26).reduced (14, 2));
    auto strip = area.removeFromBottom (150).reduced (22, 14);
    display.setBounds (area.reduced (10, 10).withTrimmedBottom (-4));

    auto bandArea = strip.removeFromLeft (150);
    bandName.setBounds (bandArea.removeFromTop (24));
    bandArea.removeFromTop (4);
    type.setBounds (bandArea.removeFromTop (26));
    bandArea.removeFromTop (8);
    auto buttons = bandArea.removeFromTop (26);
    solo.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (8);
    bypass.setBounds (buttons);

    strip.removeFromLeft (14);
    for (int i = 0; i < 4; ++i) {
        auto k = strip.removeFromLeft (100);
        knobLabel[i].setBounds (k.removeFromBottom (16));
        knob[i].setBounds (k);
    }

    strip.removeFromLeft (18);
    auto row = [&strip] (juce::Label& l, juce::Component& c) {
        auto r = strip.removeFromTop (26);
        l.setBounds (r.removeFromLeft (96));
        c.setBounds (r.removeFromLeft (juce::jmin (160, r.getWidth())));
        strip.removeFromTop (4);
        return r;
    };
    row (modeLabel, mode);
    row (osLabel, oversampling);
    row (listenLabel, listen);
    auto rest = row (keyLabel, key);
    rest.removeFromLeft (10);
    link.setBounds (rest.removeFromLeft (juce::jmin (96, rest.getWidth())));
}
