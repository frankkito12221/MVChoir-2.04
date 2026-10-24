#pragma once
#include "KeysProcessor.h"
#include "LabLook.h"

class KeysEditor : public juce::AudioProcessorEditor,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit KeysEditor (KeysProcessor&);
    ~KeysEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() > 0; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::String label;
        juce::Slider slider;
        std::unique_ptr<SliderAtt> att;
    };

    struct Group
    {
        juce::String title;
        int row = 0;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    Knob* makeKnob (int paramIdx, const juce::String& label, juce::Colour colour);
    void addGroup (const juce::String& title, int row, std::vector<Knob*> knobs);
    void setChoice (int paramIdx, int index);
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title);
    void paintStage (juce::Graphics& g);
    void loadFromChooser();

    KeysProcessor& proc;
    LabLook look;
    juce::Image logo, art;

    std::vector<std::unique_ptr<Knob>> knobStore;
    std::vector<Group> groups;
    Knob* rootKnob = nullptr;
    Knob* fineKnob = nullptr;

    juce::ComboBox modeBox, dirBox, mutBox;
    std::unique_ptr<ComboAtt> modeAtt, dirAtt, mutAtt;
    juce::TextButton chordBtns[12];
    juce::TextButton loadBtn { "LOAD VOICE" }, recBtn { "REC STEMS" }, folderBtn { "OPEN FOLDER" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::MidiKeyboardComponent keyboard;

    juce::Rectangle<int> samplePanel, platePanel, stagePanel, recPanel;
    juce::Rectangle<int> waveArea;
    std::vector<float> wave;
    juce::String shownName;
    int shownVersion = -1;
    float voiceSmooth[mvc::kVoices] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysEditor)
};
