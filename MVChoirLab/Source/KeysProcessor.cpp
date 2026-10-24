#include "KeysProcessor.h"
#include "KeysEditor.h"

namespace
{
juce::String fmtPct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String fmtMs (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
juce::String fmtDb (float v, int) { return juce::String (v, 1) + " dB"; }
juce::String fmtSt (float v, int) { return juce::String (v, 1) + " st"; }
juce::String fmtCents (float v, int) { return juce::String (juce::roundToInt (v)) + " c"; }
juce::String fmtBright (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
constexpr int kChunk = 64; // resolucion del strum: ~1.5 ms
}

juce::AudioProcessorValueTreeState::ParameterLayout KeysProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto pid = [] (int i) { return juce::ParameterID { kKeysIds[i], 1 }; };
    auto addF = [&] (int i, const juce::String& name, float lo, float hi, float def, float interval, float skew,
                     std::function<juce::String (float, int)> fmt)
    {
        juce::NormalisableRange<float> range (lo, hi, interval);
        range.setSkewForCentre (skew > 0.0f ? lo + (hi - lo) * skew : lo + (hi - lo) * 0.5f);
        v.push_back (std::make_unique<juce::AudioParameterFloat> (
            pid (i), name, range, def, juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };

    v.push_back (std::make_unique<juce::AudioParameterChoice> (pid (kpMode), "Mode", juce::StringArray { "DIRECT", "CHORD" }, 1));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (pid (kpChord), "Chord Type",
                                                               juce::StringArray { "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS2", "SUS4", "DIM", "AUG", "QRT", "ADD9", "CLST" }, 0));
    v.push_back (std::make_unique<juce::AudioParameterInt> (pid (kpChordOct), "Octave", -1, 1, 0));
    v.push_back (std::make_unique<juce::AudioParameterInt> (pid (kpUnison), "Unison", 1, 3, 2));
    v.push_back (std::make_unique<juce::AudioParameterInt> (pid (kpRoot), "Sample Root", 24, 96, 60,
                                                            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int x, int) { return midiNoteName (x); })));
    addF (kpRootFine, "Sample Fine", -50.0f, 50.0f, 0.0f, 0.1f, 0.0f, fmtCents);
    addF (kpStrum, "Strum", 0.0f, 400.0f, 90.0f, 1.0f, 0.0f, fmtMs);
    v.push_back (std::make_unique<juce::AudioParameterChoice> (pid (kpStrumDir), "Strum Direction", juce::StringArray { "UP", "DOWN", "RANDOM" }, 0));
    addF (kpAttack, "Attack", 5.0f, 3000.0f, 220.0f, 1.0f, 0.15f, fmtMs);
    addF (kpRelease, "Release", 20.0f, 6000.0f, 900.0f, 1.0f, 0.15f, fmtMs);
    addF (kpDetune, "Detune", 0.0f, 1.0f, 0.15f, 0.001f, 0.0f, fmtPct);
    addF (kpHum, "Humanize", 0.0f, 1.0f, 0.3f, 0.001f, 0.0f, fmtPct);
    addF (kpMove, "Movement", 0.0f, 1.0f, 0.2f, 0.001f, 0.0f, fmtPct);
    addF (kpWidth, "Stereo Width", 0.0f, 1.5f, 1.0f, 0.001f, 0.0f, fmtPct);
    addF (kpBright, "Brightness", -1.0f, 1.0f, 0.0f, 0.001f, 0.0f, fmtBright);
    addF (kpFormant, "Formant", -12.0f, 12.0f, 0.0f, 0.01f, 0.0f, fmtSt);
    v.push_back (std::make_unique<juce::AudioParameterChoice> (pid (kpMutMode), "Mutation", juce::StringArray { "OFF", "WHISPER", "BLUR", "MORPH" }, 0));
    addF (kpMutAmt, "Mutation Amount", 0.0f, 1.0f, 0.4f, 0.001f, 0.0f, fmtPct);
    addF (kpRevSize, "Reverb Size", 0.0f, 1.0f, 0.6f, 0.001f, 0.0f, fmtPct);
    addF (kpRevLevel, "Reverb Level", 0.0f, 1.0f, 0.3f, 0.001f, 0.0f, fmtPct);
    addF (kpRevSend, "Reverb Send", 0.0f, 1.0f, 0.35f, 0.001f, 0.0f, fmtPct);
    addF (kpOut, "Output", -30.0f, 12.0f, -6.0f, 0.1f, 0.0f, fmtDb);
    addF (kpVelSens, "Velocity", 0.0f, 1.0f, 0.5f, 0.001f, 0.0f, fmtPct);
    return { v.begin(), v.end() };
}

KeysProcessor::KeysProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Master", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Bass", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Tenor", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Alto", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Soprano", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "MVCHOIRLABKEYS", createLayout())
{
    engine = std::make_unique<mvc::Engine>();
    for (int i = 0; i < kpCount; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (kKeysIds[i]);
    outPeak[0].store (0.0f);
    outPeak[1].store (0.0f);
    for (auto& m : voiceMeter)
        m.store (0.0f);
}

bool KeysProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (const auto& bus : layouts.outputBuses)
        if (! bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void KeysProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSampleRate) > 1.0;
    currentSampleRate = sampleRate;
    engine->prepare (sampleRate);
    engine->setMainSpecial (false); // aca ninguna voz es "MAIN": todas son notas
    setLatencySamples (engine->latencySamples());
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    scratch.setSize (kScratchCh, juce::jmax (1, samplesPerBlock));
    scratch.clear();
    for (auto& s : slots)
        s = Slot{};
    if (! srcMono.empty() && (rateChanged || current == nullptr))
        rebuildLoop (sampleRate, false);
}

// ---------------------------------------------------------------- grabacion

bool KeysProcessor::loadSample (const juce::File& file, bool detectRoot)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return false;
    const juce::int64 maxLen = (juce::int64) (reader->sampleRate * 30.0);
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    if (len < 4096 || reader->numChannels < 1)
        return false;

    juce::AudioBuffer<float> tmp ((int) reader->numChannels, len);
    reader->read (&tmp, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
    {
        const float* d = tmp.getReadPointer (ch);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] += d[i];
    }
    const float inv = 1.0f / (float) tmp.getNumChannels();
    for (auto& x : mono)
        x *= inv;

    srcMono = std::move (mono);
    srcRate = reader->sampleRate;
    samplePath = file.getFullPathName();
    sampleName = file.getFileName();
    apvts.state.setProperty ("samplePath", samplePath, nullptr);
    rebuildLoop (currentSampleRate > 0.0 ? currentSampleRate : 44100.0, detectRoot);
    return true;
}

void KeysProcessor::rebuildLoop (double hostRate, bool detectRoot)
{
    if (srcMono.empty())
        return;
    const double ratio = srcRate / hostRate;
    const int outLen = (int) ((double) srcMono.size() / ratio);
    std::vector<float> res ((size_t) juce::jmax (1, outLen));
    for (int i = 0; i < outLen; ++i)
    {
        const double p = i * ratio;
        const int i0 = (int) p;
        const int i1 = juce::jmin (i0 + 1, (int) srcMono.size() - 1);
        const float f = (float) (p - i0);
        res[(size_t) i] = srcMono[(size_t) i0] * (1.0f - f) + srcMono[(size_t) i1] * f;
    }
    auto region = mvc::prepareLoopRegion (res, hostRate);
    if (region.size() < 2048)
        return;

    if (detectRoot)
    {
        const double f0 = mvc::detectPitchHz (region, hostRate);
        if (f0 > 40.0)
        {
            const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
            const int root = juce::jlimit (24, 96, (int) std::lround (midi));
            setPlain (kKeysIds[kpRoot], (float) root);
            setPlain (kKeysIds[kpRootFine], (float) juce::jlimit (-50.0, 50.0, (midi - root) * 100.0));
        }
    }

    auto s = std::make_shared<LoopData>();
    s->data = std::move (region);
    s->xfade = (int) (0.08 * hostRate);

    waveform.assign (300, 0.0f);
    const size_t per = juce::jmax<size_t> (1, s->data.size() / 300);
    for (size_t b = 0; b < 300; ++b)
    {
        float pk = 0.0f;
        for (size_t i = b * per; i < juce::jmin (s->data.size(), (b + 1) * per); ++i)
            pk = std::max (pk, std::fabs (s->data[i]));
        waveform[b] = pk;
    }

    if (keepAlive.size() >= 16)
        keepAlive.erase (keepAlive.begin());
    keepAlive.push_back (s);
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        pending = s;
    }
    ++sampleVersion;
}

void KeysProcessor::setPlain (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

bool KeysProcessor::startRecording()
{
    static const char* const names[StemRecorder::kNumStems] = { "BASS", "TENOR", "ALTO", "SOPRANO", "MIX" };
    return recorder.start (currentSampleRate, 31, names);
}

// ---------------------------------------------------------------- notas

int KeysProcessor::allocateSlot() const
{
    for (int i = 0; i < mvc::kVoices; ++i)
        if (slots[i].key < 0 && ! slots[i].gate && engine->getEnv (i) < 1e-3f)
            return i;
    int best = -1;
    uint32_t bestAge = 0xffffffffu;
    for (int i = 0; i < mvc::kVoices; ++i) // primero robamos una voz que ya se esta apagando
        if (! slots[i].gate && slots[i].age < bestAge) { bestAge = slots[i].age; best = i; }
    if (best >= 0)
        return best;
    for (int i = 0; i < mvc::kVoices; ++i)
        if (slots[i].age < bestAge) { bestAge = slots[i].age; best = i; }
    return juce::jmax (0, best);
}

void KeysProcessor::noteOn (int key, float vel)
{
    const int mode = juce::roundToInt (pval (kpMode));
    const int chordType = juce::roundToInt (pval (kpChord));
    const int oct = juce::roundToInt (pval (kpChordOct));
    const int unison = juce::jlimit (1, 3, juce::roundToInt (pval (kpUnison)));
    const double rootMidi = pval (kpRoot) + pval (kpRootFine) / 100.0;

    mvc::PlannedNote notes[4];
    const int nNotes = mvc::planFromKey (mode, chordType, oct, key, rootMidi, notes);

    int rank[4] = { 0, 1, 2, 3 };
    const int dir = juce::roundToInt (pval (kpStrumDir));
    if (dir == 1)
        for (int i = 0; i < 4; ++i) rank[i] = 3 - i;
    else if (dir == 2)
        for (int i = 3; i > 0; --i) std::swap (rank[i], rank[rng.nextInt (i + 1)]);
    const float strumMs = pval (kpStrum);

    for (int n = 0; n < nNotes; ++n)
        for (int c = 0; c < unison; ++c)
        {
            const int s = allocateSlot();
            engine->resetVoice (s);
            Slot& S = slots[s];
            S.key = key;
            S.semis = notes[n].semis;
            S.group = notes[n].group;
            S.copy = c;
            S.nCopies = unison;
            S.vel = vel;
            S.age = ++ageCounter;
            const int delay = nNotes > 1 ? (int) ((rank[n] / 3.0) * strumMs * 0.001 * currentSampleRate) : 0;
            S.startDelay = delay;
            S.pending = delay > 0;
            S.gate = delay <= 0;
        }
}

void KeysProcessor::noteOff (int key)
{
    for (auto& S : slots)
        if (S.key == key)
        {
            S.gate = false;
            S.pending = false;
            S.key = -1;
        }
}

void KeysProcessor::allNotesOff()
{
    for (auto& S : slots)
    {
        S.gate = false;
        S.pending = false;
        S.key = -1;
    }
}

void KeysProcessor::advanceTimers (int n)
{
    for (auto& S : slots)
        if (S.pending && S.key >= 0)
        {
            S.startDelay -= n;
            if (S.startDelay <= 0)
            {
                S.pending = false;
                S.gate = true;
            }
        }
}

void KeysProcessor::buildVoices()
{
    const float vs = pval (kpVelSens);
    const int mutMode = juce::jlimit (0, 3, juce::roundToInt (pval (kpMutMode)));
    for (int i = 0; i < mvc::kVoices; ++i)
    {
        const Slot& S = slots[i];
        mvc::VoiceParams p;
        p.active = true;
        p.useEnv = true;
        p.gate = S.gate;
        p.attackMs = pval (kpAttack);
        p.releaseMs = pval (kpRelease);
        const float spreadIdx = (float) S.copy - 0.5f * (float) (S.nCopies - 1);
        p.pitchSt = (float) S.semis;
        p.fineCents = spreadIdx * 10.0f;
        p.formantSt = pval (kpFormant);
        p.delayMs = (float) S.copy * 9.0f;
        p.humanize = 0.0f;
        const float g = (1.0f - vs) + vs * S.vel;
        p.level = g * 0.6f / std::sqrt ((float) S.nCopies);
        p.pan = juce::jlimit (-1.0f, 1.0f, ((float) S.group - 1.5f) * 0.3f + spreadIdx * 0.35f);
        p.bright = pval (kpBright);
        p.send = pval (kpRevSend);
        p.mutMode = mutMode;
        p.mutAmt = pval (kpMutAmt);
        engine->setVoice (i, p);
    }
}

// ---------------------------------------------------------------- audio

void KeysProcessor::renderRange (int start, int len)
{
    float* const* sc = scratch.getArrayOfWritePointers();
    while (len > 0)
    {
        const int c = juce::jmin (kChunk, len);
        advanceTimers (c);
        buildVoices();

        float* vL[mvc::kVoices];
        float* vR[mvc::kVoices];
        for (int v = 0; v < mvc::kVoices; ++v)
        {
            vL[v] = sc[chVoice + 2 * v] + start;
            vR[v] = sc[chVoice + 2 * v + 1] + start;
        }
        float* inL = sc[chIn] + start;
        float* inR = sc[chIn + 1] + start;
        for (int i = 0; i < c; ++i)
            inL[i] = inR[i] = loop.next();
        engine->process (inL, inR, c, vL, vR, sc[chSend] + start, sc[chSend + 1] + start, sc[chDry] + start, sc[chDry + 1] + start);
        start += c;
        len -= c;
    }
}

void KeysProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0)
        return;
    if (scratch.getNumSamples() < n)
        scratch.setSize (kScratchCh, n, false, false, true);

    {   // cambio de grabacion (sin asignar memoria en el hilo de audio)
        const juce::SpinLock::ScopedTryLockType tl (sampleLock);
        if (tl.isLocked() && pending != current)
        {
            current = pending;
            loop.view (current->data.data(), (int) current->data.size(), current->xfade);
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    mvc::Macros m;
    m.detune = pval (kpDetune);
    m.humanize = pval (kpHum);
    m.movement = pval (kpMove);
    m.width = pval (kpWidth);
    engine->setMacros (m);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit (pos, n, meta.samplePosition);
        if (evPos > pos)
        {
            renderRange (pos, evPos - pos);
            pos = evPos;
        }
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            allNotesOff();
    }
    renderRange (pos, n - pos);
    midi.clear();

    float* const* sc = scratch.getArrayOfWritePointers();

    // ---- grupos (bajo/tenor/alto/soprano) ----
    const float outGain = juce::Decibels::decibelsToGain (pval (kpOut));
    for (int g = 0; g < 4; ++g)
    {
        std::memset (sc[chGroup + 2 * g], 0, (size_t) n * sizeof (float));
        std::memset (sc[chGroup + 2 * g + 1], 0, (size_t) n * sizeof (float));
    }
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        voiceMeter[v].store (engine->takeVoicePeak (v));
        float* gl = sc[chGroup + 2 * slots[v].group];
        float* gr = sc[chGroup + 2 * slots[v].group + 1];
        const float* a = sc[chVoice + 2 * v];
        const float* b = sc[chVoice + 2 * v + 1];
        for (int i = 0; i < n; ++i)
        {
            gl[i] += a[i] * outGain;
            gr[i] += b[i] * outGain;
        }
    }

    // ---- reverb ----
    std::memcpy (sc[chRev], sc[chSend], (size_t) n * sizeof (float));
    std::memcpy (sc[chRev + 1], sc[chSend + 1], (size_t) n * sizeof (float));
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.3f + 0.69f * pval (kpRevSize);
    rp.damping = 0.5f;
    rp.wetLevel = 0.33f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    reverb.processStereo (sc[chRev], sc[chRev + 1], n);
    const float revGain = pval (kpRevLevel) * outGain;

    // ---- salida master ----
    auto mainBus = getBusBuffer (buffer, false, 0);
    if (mainBus.getNumChannels() < 2)
        return;
    float* mL = mainBus.getWritePointer (0);
    float* mR = mainBus.getWritePointer (1);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = sc[chRev][i] * revGain, r = sc[chRev + 1][i] * revGain;
        for (int g = 0; g < 4; ++g)
        {
            l += sc[chGroup + 2 * g][i];
            r += sc[chGroup + 2 * g + 1][i];
        }
        mL[i] = l;
        mR[i] = r;
        peakL = std::max (peakL, std::fabs (l));
        peakR = std::max (peakR, std::fabs (r));
    }
    outPeak[0].store (peakL);
    outPeak[1].store (peakR);

    // ---- salidas separadas (para rutear a pistas distintas) ----
    for (int g = 0; g < 4; ++g)
        if (auto* bus = getBus (false, g + 1))
            if (bus->isEnabled())
            {
                auto bb = getBusBuffer (buffer, false, g + 1);
                if (bb.getNumChannels() >= 2)
                {
                    bb.copyFrom (0, 0, scratch, chGroup + 2 * g, 0, n);
                    bb.copyFrom (1, 0, scratch, chGroup + 2 * g + 1, 0, n);
                }
            }

    // ---- stems ----
    if (recorder.isRecording())
    {
        for (int g = 0; g < 4; ++g)
            recorder.write (g, sc[chGroup + 2 * g], sc[chGroup + 2 * g + 1], n);
        recorder.write (4, mL, mR, n);
    }
}

juce::AudioProcessorEditor* KeysProcessor::createEditor()
{
    return new KeysEditor (*this);
}

void KeysProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KeysProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const juce::String path = apvts.state.getProperty ("samplePath").toString();
            if (path.isNotEmpty() && path != samplePath)
            {
                const juce::File f (path);
                if (f.existsAsFile())
                    loadSample (f, false); // conserva el tono raiz guardado en el proyecto
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeysProcessor();
}
