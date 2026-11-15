#pragma once
#include "PluginProcessor.h"
#include "Fft.h"

class PfxLook : public juce::LookAndFeel_V4
{
public:
    PfxLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

// The frequency display: input and output spectrum, band windows, draggable handles,
// the note each band is following and the harmonics it adds above that note.
class PfxDisplay : public juce::Component, private juce::Timer
{
public:
    explicit PfxDisplay (PfxProcessor&);
    std::function<void (int)> onSelect;
    int selected = 0;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour colour (int band);

private:
    static constexpr int kFft = 8192;

    void timerCallback() override;
    void spectrumPath (juce::Path&, const std::vector<float>& db) const;
    float xOf (float hz) const, hzOf (float x) const, yOf (float db) const, dbOf (float y) const, ySpec (float db) const;
    float get (int band, const char* name) const;
    void set (int band, const char* name, float value);
    void gesture (int band, const char* name, bool begin);
    int hitNode (juce::Point<float>) const, hitRegion (juce::Point<float>) const;
    const float* harmonics (int band);

    PfxProcessor& proc;
    int dragging = -1;
    juce::Point<float> grab;

    pfx::Fft fft;
    std::vector<float> scopeIn, scopeOut, dbIn, dbOut;
    std::vector<double> window, re, im;

    struct HarmCache { int type = -1; float drive = -1.0f; float rel[7] {}; } harm[PfxProcessor::kBands];
};

class PfxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PfxEditor (PfxProcessor&);
    ~PfxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    void selectBand (int);
    void timerCallback() override;

    PfxProcessor& proc;
    PfxLook look;
    PfxDisplay display;

    juce::Label bandName, hint;
    juce::Slider knob[4];
    juce::Label knobLabel[4];
    juce::ComboBox type, mode, oversampling, listen, key;
    juce::Label modeLabel, osLabel, listenLabel, keyLabel;
    juce::TextButton solo { "Solo" }, bypass { "Bypass" }, link { "Bass link" };

    std::unique_ptr<APVTS::SliderAttachment> knobAtt[4];
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt, modeAtt, osAtt, listenAtt, keyAtt;
    std::unique_ptr<APVTS::ButtonAttachment> soloAtt, bypassAtt, linkAtt;
    int band = 0;
    float lastFreq = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PfxEditor)
};
