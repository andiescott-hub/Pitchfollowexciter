#pragma once
#include "PluginProcessor.h"

// The frequency display: band windows, draggable handles, and the note each band is following.
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour colour (int band);

private:
    void timerCallback() override { repaint(); }
    float xOf (float hz) const, hzOf (float x) const, yOf (float db) const, dbOf (float y) const;
    float get (int band, const char* name) const;
    void set (int band, const char* name, float value);
    void gesture (int band, const char* name, bool begin);
    int hitNode (juce::Point<float>) const, hitRegion (juce::Point<float>) const;

    PfxProcessor& proc;
    int dragging = -1;
    juce::Point<float> grab;
};

class PfxEditor : public juce::AudioProcessorEditor
{
public:
    explicit PfxEditor (PfxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    void selectBand (int);

    PfxProcessor& proc;
    PfxDisplay display;

    juce::Label bandName;
    juce::Slider knob[4];
    juce::Label knobLabel[4];
    juce::ComboBox type, mode, oversampling, listen, key;
    juce::Label modeLabel, osLabel, listenLabel, keyLabel;
    juce::TextButton solo { "Solo" }, bypass { "Bypass" }, link { "Bass link" };

    std::unique_ptr<APVTS::SliderAttachment> knobAtt[4];
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt, modeAtt, osAtt, listenAtt, keyAtt;
    std::unique_ptr<APVTS::ButtonAttachment> soloAtt, bypassAtt, linkAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PfxEditor)
};
