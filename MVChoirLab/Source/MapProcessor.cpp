#include "MapProcessor.h"
#include "MapEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MapProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kMapIds[mpMode], 1 }, "Mode",
                                                               juce::StringArray { "RELAY (on a channel)", "MAP (on the bus)" }, 0));
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMapIds[mpX], 1 }, "Map X (Pan)",
                                                              juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMapIds[mpY], 1 }, "Map Y (Level)",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), mvmap::kDefaultY));
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMapIds[mpDepth], 1 }, "Depth",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kMapIds[mpMute], 1 }, "Mute", false));
    return { v.begin(), v.end() };
}

MapProcessor::MapProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MVMAP", createLayout())
{
    for (int i = 0; i < mpCount; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (kMapIds[i]);
    startTimerHz (30);
}

MapProcessor::~MapProcessor()
{
    stopTimer();
    unregisterSlot();
}

bool MapProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MapProcessor::prepareToPlay (double sampleRate, int)
{
    dsp.prepare (sampleRate);
}

void MapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    float pl = 0.0f, pr = 0.0f;
    if (isRelay())
    {
        dsp.process (l, r, n, pval (mpX), pval (mpY), pval (mpDepth), pval (mpMute) > 0.5f, pl, pr);
        const int s = slot.load();
        if (s >= 0)
        {
            auto& sl = mvmap::Registry::get().slots[s];
            sl.peakL.store (pl);
            sl.peakR.store (pr);
        }
    }
    else
    {
        for (int i = 0; i < n; ++i) // en modo MAP el audio pasa sin tocar
        {
            pl = std::max (pl, std::fabs (l[i]));
            pr = std::max (pr, std::fabs (r[i]));
        }
    }
    peakL.store (pl);
    peakR.store (pr);
}

void MapProcessor::setPlain (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void MapProcessor::resetPosition()
{
    setPlain (kMapIds[mpX], 0.0f);
    setPlain (kMapIds[mpY], mvmap::kDefaultY);
    setPlain (kMapIds[mpDepth], 0.0f);
    setPlain (kMapIds[mpMute], 0.0f);
}

void MapProcessor::registerSlot()
{
    auto& R = mvmap::Registry::get();
    uint64_t uid = (uint64_t) apvts.state.getProperty ("uid").toString().getHexValue64();
    const int s = R.claim (uid);
    if (s < 0)
        return;
    apvts.state.setProperty ("uid", juce::String::toHexString ((juce::int64) uid), nullptr);
    juce::String lab = getLabel();
    if (lab.isEmpty())
    {
        lab = "TRACK " + juce::String (s + 1);
        setLabel (lab);
    }
    R.setLabel (s, lab.toRawUTF8());
    labelCache = lab;
    registeredUid = uid;
    auto& sl = R.slots[s];
    sl.x.store (pval (mpX));
    sl.y.store (pval (mpY));
    sl.depth.store (pval (mpDepth));
    sl.mute.store (pval (mpMute) > 0.5f);
    lastCmdSeq = sl.editSeq.load();
    slot.store (s);
}

void MapProcessor::unregisterSlot()
{
    const int s = slot.exchange (-1);
    if (s >= 0)
        mvmap::Registry::get().release (s);
    registeredUid = 0;
}

void MapProcessor::timerCallback()
{
    const bool relayMode = isRelay();
    if (! relayMode)
    {
        if (slot.load() >= 0)
            unregisterSlot();
        return;
    }
    if (slot.load() < 0)
    {
        registerSlot();
        return;
    }

    auto& R = mvmap::Registry::get();
    const int s = slot.load();
    auto& sl = R.slots[s];

    // si se cargo un proyecto con otro identificador, nos volvemos a registrar
    const auto uidState = (uint64_t) apvts.state.getProperty ("uid").toString().getHexValue64();
    if (uidState != 0 && uidState != registeredUid)
    {
        unregisterSlot();
        return;
    }
    const juce::String lab = getLabel();
    if (lab != labelCache)
    {
        labelCache = lab;
        R.setLabel (s, lab.toRawUTF8());
    }

    const int seq = sl.editSeq.load();
    if (seq != lastCmdSeq)
    {
        // el MAP movio este canal: lo llevamos a los parametros (queda guardado y automatizable)
        lastCmdSeq = seq;
        setPlain (kMapIds[mpX], sl.cmdX.load());
        setPlain (kMapIds[mpY], sl.cmdY.load());
        setPlain (kMapIds[mpDepth], sl.cmdDepth.load());
        setPlain (kMapIds[mpMute], sl.cmdMute.load() ? 1.0f : 0.0f);
    }
    sl.x.store (pval (mpX));
    sl.y.store (pval (mpY));
    sl.depth.store (pval (mpDepth));
    sl.mute.store (pval (mpMute) > 0.5f);
}

juce::AudioProcessorEditor* MapProcessor::createEditor()
{
    return new MapEditor (*this);
}

void MapProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MapProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MapProcessor();
}
