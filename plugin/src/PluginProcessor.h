#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"

class PfxProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kBands = pfx::Engine::kBands;

    PfxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }
    const juce::String getName() const override             { return "Pitch-follow Exciter"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.1; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::String bandId (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

    // read by the editor
    float trackedHz (int b) const { return engine.filterHz (b); }
    void readScope (float* in, float* out, int n) const;   // most recent n samples of input and output (mono)
    double sampleRate() const     { return getSampleRate() > 0 ? getSampleRate() : 44100.0; }
    bool  gateOpen (int b) const  { return engine.gateOpen (b); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct BandPtrs { std::atomic<float> *on, *freq, *amount, *drive, *type, *focus, *glide, *solo, *byp; };
    BandPtrs band[kBands] {};
    std::atomic<float> *pMode = nullptr, *pOs = nullptr, *pListen = nullptr, *pKey = nullptr, *pLink = nullptr;

    pfx::Engine engine;
    int maxBlock = 0;
    std::vector<float> mono, wet;
    juce::AudioBuffer<float> osBuf;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[kBands][3];   // 4x, 8x, 16x
    float lastGain[kBands] {}, lastGen[kBands] { 1.0f, 1.0f, 1.0f, 1.0f }, lastDry = 1.0f;

    // dry delay for lookahead mode
    static constexpr int kDelaySize = 32768;
    float dryRing[2][kDelaySize] {};
    int dryPos = 0;

    // analyser feed: written on the audio thread, read by the editor (display only)
    static constexpr int kScopeSize = 16384;
    float scopeIn[kScopeSize] {}, scopeOut[kScopeSize] {};
    std::atomic<int> scopePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PfxProcessor)
};
