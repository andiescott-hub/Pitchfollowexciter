#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0c0f16), panel (0xff151a25), line (0xff2a3345), ink (0xffe9edf5), mute (0xff8d98ad);
    const float fLo = 20.0f, fHi = 20000.0f, top = 26.0f, bottom = 22.0f;

    juce::String noteName (float hz)
    {
        static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const int m = (int) std::lround (69.0f + 12.0f * std::log2 (hz / 440.0f));
        return juce::String (n[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    }
}

//==============================================================================
PfxDisplay::PfxDisplay (PfxProcessor& p) : proc (p) { startTimerHz (30); }

juce::Colour PfxDisplay::colour (int b)
{
    static const juce::Colour c[4] = { juce::Colour (0xfff5a742), juce::Colour (0xfff0648c), juce::Colour (0xff38cfc0), juce::Colour (0xff9a8cff) };
    return c[b & 3];
}

float PfxDisplay::xOf (float hz) const { return (float) getWidth() * std::log (hz / fLo) / std::log (fHi / fLo); }
float PfxDisplay::hzOf (float x) const { return fLo * std::pow (fHi / fLo, x / (float) getWidth()); }
float PfxDisplay::yOf (float db) const { return top + ((float) getHeight() - top - bottom) * (1.0f - (db + 24.0f) / 48.0f); }
float PfxDisplay::dbOf (float y) const { return 48.0f * (1.0f - (y - top) / ((float) getHeight() - top - bottom)) - 24.0f; }

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
    for (int b = 0; b < PfxProcessor::kBands; ++b)
        if (get (b, "on") > 0.5f && pt.getDistanceFrom ({ xOf (get (b, "freq")), yOf (get (b, "amount")) }) < 14.0f) return b;
    return -1;
}

int PfxDisplay::hitRegion (juce::Point<float> pt) const
{
    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        float lo, hi;
        pfx::bandEdges (get (b, "freq"), get (b, "focus"), lo, hi);
        if (pt.x >= xOf (lo) && pt.x <= xOf (hi)) return b;
    }
    return -1;
}

void PfxDisplay::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight(), base = h - bottom;
    g.fillAll (juce::Colour (0xff0e121b));
    g.setFont (11.0f);
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = xOf (f);
        g.setColour (line.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, top - 6.0f, base);
        g.setColour (mute);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f), (int) x - 20, (int) h - 18, 40, 14, juce::Justification::centred);
    }
    for (float db : { -12.0f, 0.0f, 12.0f }) {
        g.setColour (line.withAlpha (std::abs (db) < 0.5f ? 0.9f : 0.4f));
        g.drawHorizontalLine ((int) yOf (db), 0.0f, w);
        g.setColour (mute);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", (int) w - 60, (int) yOf (db) - 15, 54, 14, juce::Justification::right);
    }

    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        const auto col = colour (b);
        const bool dim = get (b, "bypass") > 0.5f;
        float lo, hi;
        pfx::bandEdges (get (b, "freq"), get (b, "focus"), lo, hi);
        const float x0 = xOf (lo), x1 = xOf (hi);
        g.setGradientFill (juce::ColourGradient (col.withAlpha (b == selected ? 0.24f : 0.13f), 0, 0, col.withAlpha (0.03f), 0, base, false));
        g.fillRect (x0, 0.0f, x1 - x0, base);
        g.setColour (col.withAlpha (b == selected ? 0.7f : 0.35f));
        g.drawVerticalLine ((int) x0, 0.0f, base);
        g.drawVerticalLine ((int) x1, 0.0f, base);

        // the note being followed
        if (! dim && proc.gateOpen (b)) {
            const float hz = proc.trackedHz (b), xc = xOf (hz), q = pfx::autoQ (get (b, "freq")), amt = get (b, "amount");
            juce::Path spike;
            const float yb = yOf (-24.0f);
            spike.startNewSubPath (xc - 70.0f, yb);
            for (float dx = -70.0f; dx <= 70.0f; dx += 2.0f) {
                const float r = hzOf (xc + dx) / hz;
                const float db = -10.0f * std::log10 (1.0f + q * q * (r - 1.0f / r) * (r - 1.0f / r)) - 40.0f * std::pow (std::abs (dx) / 70.0f, 4.0f);
                spike.lineTo (xc + dx, juce::jmin (yb, yOf (amt + db)));
            }
            spike.lineTo (xc + 70.0f, yb);
            spike.closeSubPath();
            g.setColour (col.withAlpha (0.3f));
            g.fillPath (spike);
            g.setColour (col);
            g.strokePath (spike, juce::PathStrokeType (1.5f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (noteName (hz), (int) xc - 24, (int) juce::jmax (top, yOf (amt) - 26.0f), 48, 14, juce::Justification::centred);
        }
    }

    for (int b = 0; b < PfxProcessor::kBands; ++b) {
        if (get (b, "on") < 0.5f) continue;
        const float x = xOf (get (b, "freq")), y = yOf (get (b, "amount"));
        const auto col = colour (b);
        if (b == selected) { g.setColour (col); g.drawEllipse (x - 13.0f, y - 13.0f, 26.0f, 26.0f, 1.5f); }
        g.setColour (get (b, "bypass") > 0.5f ? line : col);
        g.fillEllipse (x - 9.0f, y - 9.0f, 18.0f, 18.0f);
        g.setColour (bg);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), (int) x - 9, (int) y - 8, 18, 16, juce::Justification::centred);
    }
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
    set (dragging, "freq", juce::jlimit (30.0f, 8000.0f, hzOf (e.position.x - grab.x)));
    if (grab.y > -1.0e5f) set (dragging, "amount", juce::jlimit (-24.0f, 18.0f, dbOf (e.position.y - grab.y)));
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
    addAndMakeVisible (display);
    display.onSelect = [this] (int b) { selectBand (b); };

    static const char* names[4] = { "Amount", "Drive", "Focus", "Glide" };
    for (int i = 0; i < 4; ++i) {
        knob[i].setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        addAndMakeVisible (knob[i]);
        knobLabel[i].setText (names[i], juce::dontSendNotification);
        knobLabel[i].setJustificationType (juce::Justification::centred);
        knobLabel[i].setColour (juce::Label::textColourId, mute);
        addAndMakeVisible (knobLabel[i]);
    }
    bandName.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (bandName);

    auto combo = [this] (juce::ComboBox& c, juce::Label* label, const char* text, const char* paramId) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
            c.addItemList (choice->choices, 1);
        addAndMakeVisible (c);
        if (label != nullptr) {
            label->setText (text, juce::dontSendNotification);
            label->setColour (juce::Label::textColourId, mute);
            addAndMakeVisible (*label);
        }
    };
    type.addItemList ({ "Tube", "Tape", "Transistor" }, 1);
    addAndMakeVisible (type);
    combo (mode, &modeLabel, "Mode", "mode");
    combo (oversampling, &osLabel, "Oversampling", "oversampling");
    combo (listen, &listenLabel, "Listen", "listen");
    combo (key, &keyLabel, "Key", "key");
    modeAtt   = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mode", mode);
    osAtt     = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "oversampling", oversampling);
    listenAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "listen", listen);
    keyAtt    = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "key", key);

    for (auto* b : { &solo, &bypass, &link }) { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
    linkAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "link", link);

    selectBand (0);
    setResizable (true, true);
    setResizeLimits (640, 420, 1800, 1200);
    setSize (920, 580);
}

void PfxEditor::selectBand (int b)
{
    static const char* ids[4] = { "amount", "drive", "focus", "glide" };
    for (int i = 0; i < 4; ++i) {
        knobAtt[i].reset();
        knobAtt[i] = std::make_unique<APVTS::SliderAttachment> (proc.apvts, PfxProcessor::bandId (b, ids[i]), knob[i]);
        knob[i].setColour (juce::Slider::rotarySliderFillColourId, PfxDisplay::colour (b));
        knob[i].setColour (juce::Slider::thumbColourId, ink);
    }
    typeAtt.reset();   typeAtt   = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, PfxProcessor::bandId (b, "type"), type);
    soloAtt.reset();   soloAtt   = std::make_unique<APVTS::ButtonAttachment>   (proc.apvts, PfxProcessor::bandId (b, "solo"), solo);
    bypassAtt.reset(); bypassAtt = std::make_unique<APVTS::ButtonAttachment>   (proc.apvts, PfxProcessor::bandId (b, "bypass"), bypass);
    bandName.setText ("Band " + juce::String (b + 1), juce::dontSendNotification);
    bandName.setColour (juce::Label::textColourId, PfxDisplay::colour (b));
    display.selected = b;
}

void PfxEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().removeFromBottom (150).reduced (10, 8).toFloat(), 10.0f);
}

void PfxEditor::resized()
{
    auto area = getLocalBounds();
    auto strip = area.removeFromBottom (150).reduced (20, 14);
    display.setBounds (area.reduced (10, 10).withTrimmedBottom (-6));

    auto bandArea = strip.removeFromLeft (150);
    bandName.setBounds (bandArea.removeFromTop (24));
    type.setBounds (bandArea.removeFromTop (26));
    bandArea.removeFromTop (8);
    auto buttons = bandArea.removeFromTop (26);
    solo.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (8);
    bypass.setBounds (buttons);

    strip.removeFromLeft (10);
    for (int i = 0; i < 4; ++i) {
        auto k = strip.removeFromLeft (86);
        knobLabel[i].setBounds (k.removeFromBottom (18));
        knob[i].setBounds (k);
    }

    strip.removeFromLeft (16);
    auto row = [&strip] (juce::Label& l, juce::Component& c) {
        auto r = strip.removeFromTop (26);
        l.setBounds (r.removeFromLeft (96));
        c.setBounds (r.removeFromLeft (juce::jmin (170, r.getWidth())));
        strip.removeFromTop (4);
    };
    row (modeLabel, mode);
    row (osLabel, oversampling);
    row (listenLabel, listen);
    auto last = strip.removeFromTop (26);
    keyLabel.setBounds (last.removeFromLeft (96));
    key.setBounds (last.removeFromLeft (juce::jmin (170, last.getWidth())));
    last.removeFromLeft (10);
    link.setBounds (last.removeFromLeft (juce::jmin (90, last.getWidth())));
}
