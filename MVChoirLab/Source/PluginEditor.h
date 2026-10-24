#pragma once
#include "PluginProcessor.h"
#include "LabLook.h"

class MVEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MVEditor (MVProcessor&);
    ~MVEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::String label;
        juce::Slider slider;
        std::unique_ptr<SliderAtt> att;
    };

    void timerCallback() override;
    Knob* makeKnob (std::vector<std::unique_ptr<Knob>>& store, const juce::String& label, juce::Colour colour);
    void selectVoice (int v);
    juce::Colour groupColour (int voice) const;
    juce::String voiceInfo (int v) const;
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title);
    void paintStage (juce::Graphics& g);
    void paintOutputMeters (juce::Graphics& g);

    MVProcessor& proc;
    LabLook look;

    juce::Image logo, art;

    std::vector<std::unique_ptr<Knob>> macroKnobs, outKnobs, editKnobs;
    int selected = 1;

    juce::ToggleButton eye[mvc::kVoices];
    juce::TextButton solo[mvc::kVoices];
    std::unique_ptr<ButtonAtt> eyeAtt[mvc::kVoices], soloAtt[mvc::kVoices];

    juce::ToggleButton bypassBtn { "BYPASS" };
    juce::ToggleButton stemBtn[5];
    std::unique_ptr<ButtonAtt> bypassAtt, stemAtt[5];

    juce::ComboBox mutBox, presetBox, harmBox;
    std::unique_ptr<ComboAtt> mutAtt;

    juce::TextButton recBtn { "REC STEMS" }, folderBtn { "OPEN FOLDER" };
    juce::TextButton saveBtn { "SAVE" }, loadBtn { "LOAD" }, initBtn { "INIT" };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> leftPanel, stagePanel, macroPanel, editPanel, outPanel;
    float outSmooth[2] = { 0.f, 0.f };
    float voiceSmooth[mvc::kVoices] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MVEditor)
};
