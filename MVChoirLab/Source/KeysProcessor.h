#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <memory>
#include <vector>
#include "VocalEngine.h"
#include "KeysLogic.h"
#include "StemRecorder.h"

enum KeysParam
{
    kpMode, kpChord, kpChordOct, kpUnison, kpRoot, kpRootFine,
    kpStrum, kpStrumDir, kpAttack, kpRelease,
    kpDetune, kpHum, kpMove, kpWidth,
    kpBright, kpFormant, kpMutMode, kpMutAmt,
    kpRevSize, kpRevLevel, kpRevSend, kpOut, kpVelSens,
    kpCount
};

inline const char* const kKeysIds[kpCount] = {
    "mode", "chord", "chordoct", "unison", "root", "rootfine",
    "strum", "strumdir", "attack", "release",
    "detune", "hum", "move", "width",
    "bright", "formant", "mutmode", "mutamt",
    "revsize", "revlevel", "revsend", "out", "velsens"
};

inline const char* const kChordLabels[12] = { "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS2",
                                               "SUS4", "DIM", "AUG", "QRT", "ADD9", "CLST" };

inline juce::String midiNoteName (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

class KeysProcessor : public juce::AudioProcessor
{
public:
    KeysProcessor();
    ~KeysProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MV CHOIR LAB KEYS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // hilo de mensajes (editor)
    bool loadSample (const juce::File& file, bool detectRoot = true);
    void setPlain (const juce::String& id, float plainValue);
    bool startRecording();
    void stopRecording() { recorder.stop(); }
    bool isRecording() const { return recorder.isRecording(); }

    float pval (int idx) const { return raw[(size_t) idx]->load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> outPeak[2];
    std::atomic<float> voiceMeter[mvc::kVoices];

    // solo se tocan desde el hilo de mensajes
    std::vector<float> waveform;
    juce::String sampleName;
    std::atomic<int> sampleVersion { 0 };

private:
    struct LoopData
    {
        std::vector<float> data;
        int xfade = 0;
    };

    struct Slot
    {
        int key = -1;
        bool gate = false;
        bool pending = false;
        int startDelay = 0;
        double semis = 0.0;
        float vel = 1.0f;
        int group = 0;
        int copy = 0;
        int nCopies = 1;
        uint32_t age = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuildLoop (double hostRate, bool detectRoot);
    void noteOn (int key, float vel);
    void noteOff (int key);
    void allNotesOff();
    int allocateSlot() const;
    void advanceTimers (int n);
    void buildVoices();
    void renderRange (int start, int len);

    static constexpr int chIn = 0, chVoice = 2, chSend = 22, chDry = 24, chGroup = 26, chRev = 34, kScratchCh = 36;

    std::unique_ptr<mvc::Engine> engine;
    StemRecorder recorder;
    juce::Reverb reverb;
    juce::AudioBuffer<float> scratch;
    std::array<std::atomic<float>*, (size_t) kpCount> raw {};
    Slot slots[mvc::kVoices];
    uint32_t ageCounter = 0;
    juce::Random rng;
    double currentSampleRate = 44100.0;

    mvc::SampleLoop loop;
    std::shared_ptr<const LoopData> current, pending;
    std::vector<std::shared_ptr<const LoopData>> keepAlive;
    juce::SpinLock sampleLock;
    std::vector<float> srcMono;
    double srcRate = 44100.0;
    juce::String samplePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysProcessor)
};
