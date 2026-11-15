#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout PfxProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    l.add (std::make_unique<juce::AudioParameterChoice> (id ("mode"), "Mode", juce::StringArray { "Low latency", "Lookahead" }, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("oversampling"), "Oversampling", juce::StringArray { "Off", "4x", "8x", "16x" }, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("listen"), "Listen", juce::StringArray { "Mix", "Added", "Dry" }, 0));

    juce::StringArray keys { "Off" };
    static const char* notes[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (auto* mode : { " major", " minor" })
        for (auto* n : notes) keys.add (juce::String (n) + mode);
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("key"), "Key", keys, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("link"), "Bass link", false));

    static const float defFreq[kBands] = { 62.0f, 262.0f, 560.0f, 2000.0f };
    for (int b = 0; b < kBands; ++b) {
        const auto name = "Band " + juce::String (b + 1) + " ";
        juce::NormalisableRange<float> freq (30.0f, 8000.0f, 0.0f);
        freq.setSkewForCentre (500.0f);
        l.add (std::make_unique<juce::AudioParameterBool>   (id (bandId (b, "on")),     name + "On", b < 3));
        l.add (std::make_unique<juce::AudioParameterFloat>  (id (bandId (b, "freq")),   name + "Frequency", freq, defFreq[b]));
        l.add (std::make_unique<juce::AudioParameterFloat>  (id (bandId (b, "amount")), name + "Amount", juce::NormalisableRange<float> (-24.0f, 18.0f, 0.1f), 0.0f));
        l.add (std::make_unique<juce::AudioParameterFloat>  (id (bandId (b, "drive")),  name + "Drive",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
        l.add (std::make_unique<juce::AudioParameterChoice> (id (bandId (b, "type")),   name + "Type",   juce::StringArray { "Tube", "Tape", "Transistor" }, b == 0 ? 0 : 1));
        l.add (std::make_unique<juce::AudioParameterFloat>  (id (bandId (b, "focus")),  name + "Focus",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        l.add (std::make_unique<juce::AudioParameterFloat>  (id (bandId (b, "glide")),  name + "Glide",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        l.add (std::make_unique<juce::AudioParameterBool>   (id (bandId (b, "solo")),   name + "Solo", false));
        l.add (std::make_unique<juce::AudioParameterBool>   (id (bandId (b, "bypass")), name + "Bypass", false));
    }
    return l;
}

PfxProcessor::PfxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    auto raw = [this] (const juce::String& s) { return apvts.getRawParameterValue (s); };
    pMode = raw ("mode"); pOs = raw ("oversampling"); pListen = raw ("listen"); pKey = raw ("key"); pLink = raw ("link");
    for (int b = 0; b < kBands; ++b)
        band[b] = { raw (bandId (b, "on")), raw (bandId (b, "freq")), raw (bandId (b, "amount")), raw (bandId (b, "drive")),
                    raw (bandId (b, "type")), raw (bandId (b, "focus")), raw (bandId (b, "glide")),
                    raw (bandId (b, "solo")), raw (bandId (b, "bypass")) };
}

bool PfxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && out == layouts.getMainInputChannelSet();
}

void PfxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    engine.setLookahead (pMode->load() > 0.5f);
    setLatencySamples (engine.latency());
    mono.assign ((size_t) maxBlock, 0.0f);
    wet.assign ((size_t) maxBlock, 0.0f);
    osBuf.setSize (2, maxBlock);
    for (int b = 0; b < kBands; ++b)
        for (int o = 0; o < 3; ++o) {
            // Polyphase IIR: almost no latency, so the generated harmonics stay aligned without delaying the dry path.
            os[b][o] = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) 2, (size_t) (o + 2), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            os[b][o]->initProcessing ((size_t) maxBlock);
        }
    for (auto& ch : dryRing) std::fill (std::begin (ch), std::end (ch), 0.0f);
    dryPos = 0;
    std::fill (std::begin (lastGain), std::end (lastGain), 0.0f);
    lastDry = 1.0f;
}

void PfxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples(), chans = juce::jmin (2, buffer.getNumChannels());
    if (chans == 0 || maxBlock == 0) return;

    engine.setLookahead (pMode->load() > 0.5f);
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());   // host re-aligns the track

    const int listen = (int) pListen->load(), osChoice = (int) pOs->load(), key = (int) pKey->load();
    engine.bassLink = pLink->load() > 0.5f;
    engine.keyMask = 0;
    if (key > 0) {
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int root = (key - 1) % 12;
        for (int d : (key <= 12 ? major : minor)) engine.keyMask |= 1 << ((root + d) % 12);
    }

    bool anySolo = false;
    for (int b = 0; b < kBands; ++b) {
        auto& p = engine.params[b];
        p.enabled  = band[b].on->load() > 0.5f;
        p.fc       = band[b].freq->load();
        p.amountDb = band[b].amount->load();
        p.drive    = band[b].drive->load();
        p.type     = (int) band[b].type->load();
        p.focus    = band[b].focus->load();
        p.glide    = band[b].glide->load();
        anySolo = anySolo || (p.enabled && band[b].solo->load() > 0.5f && band[b].byp->load() < 0.5f);   // a bypassed band drops out of solo
    }
    const float dryTarget = (listen == 1 || anySolo) ? 0.0f : 1.0f;
    const int delay = engine.latency();

    for (int offset = 0; offset < total; offset += maxBlock) {
        const int n = juce::jmin (maxBlock, total - offset);
        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = buffer.getReadPointer (chans - 1, offset);
        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (inL[i] + inR[i]);

        engine.analyse (mono.data(), n);
        std::fill (wet.begin(), wet.begin() + n, 0.0f);

        for (int b = 0; b < kBands; ++b) {
            if (! engine.params[b].enabled) { lastGain[b] = 0.0f; continue; }
            const bool muted = listen == 2 || band[b].byp->load() > 0.5f || (anySolo && band[b].solo->load() < 0.5f);
            const float target = muted ? 0.0f : juce::Decibels::decibelsToGain (engine.params[b].amountDb);
            const float* gen;
            if (osChoice == 0) {
                engine.generate (b, engine.s (b), engine.q (b), osBuf.getWritePointer (0), n);
                gen = osBuf.getReadPointer (0);
            } else {
                // only the nonlinear part runs oversampled; the clean copy stays at the base rate, phase intact
                osBuf.copyFrom (0, 0, engine.s (b), n);
                osBuf.copyFrom (1, 0, engine.q (b), n);
                juce::dsp::AudioBlock<float> block (osBuf.getArrayOfWritePointers(), 2, (size_t) n);
                auto& o = *os[b][osChoice - 1];
                auto up = o.processSamplesUp (block);
                engine.generate (b, up.getChannelPointer (0), up.getChannelPointer (1), up.getChannelPointer (0), (int) up.getNumSamples());
                o.processSamplesDown (block);
                gen = osBuf.getReadPointer (0);
            }
            engine.finish (b, gen, wet.data(), n, lastGain[b], target);
            lastGain[b] = target;
        }

        const float dryStep = (dryTarget - lastDry) / (float) n;
        for (int ch = 0; ch < chans; ++ch) {
            float* io = buffer.getWritePointer (ch, offset);
            float g = lastDry;
            int pos = dryPos;
            for (int i = 0; i < n; ++i) {
                dryRing[ch][pos] = io[i];
                const float d = dryRing[ch][(pos - delay) & (kDelaySize - 1)];
                pos = (pos + 1) & (kDelaySize - 1);
                g += dryStep;
                io[i] = d * g + wet[(size_t) i];
            }
        }
        dryPos = (dryPos + n) & (kDelaySize - 1);
        lastDry = dryTarget;
    }
}

void PfxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PfxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PfxProcessor::createEditor() { return new PfxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PfxProcessor(); }
