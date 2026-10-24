#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MapCore.h"

enum MapParam { mpMode, mpX, mpY, mpDepth, mpMute, mpCount };
inline const char* const kMapIds[mpCount] = { "mode", "x", "y", "depth", "mute" };

// MV MAP: el mismo plugin hace de RELAY (en cada canal) o de MAP (en el bus).
class MapProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MapProcessor();
    ~MapProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MV MAP"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float pval (int idx) const { return raw[(size_t) idx]->load(); }
    bool isRelay() const { return pval (mpMode) < 0.5f; }
    int getSlot() const { return slot.load(); }
    juce::String getLabel() const { return apvts.state.getProperty ("label").toString(); }
    void setLabel (const juce::String& text) { apvts.state.setProperty ("label", text, nullptr); }
    void setPlain (const juce::String& id, float plainValue);
    void resetPosition();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> peakL { 0.f }, peakR { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void registerSlot();
    void unregisterSlot();

    std::array<std::atomic<float>*, (size_t) mpCount> raw {};
    mvmap::RelayDsp dsp;
    std::atomic<int> slot { -1 };
    uint64_t registeredUid = 0;
    int lastCmdSeq = 0;
    juce::String labelCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MapProcessor)
};
