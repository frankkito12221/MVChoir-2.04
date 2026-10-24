#pragma once
#include "MapProcessor.h"
#include "LabLook.h"

class MapEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MapEditor (MapProcessor&);
    ~MapEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Dot
    {
        int slot = -1;
        juce::String label;
        float x = 0.f, y = 0.f, depth = 0.f, peak = 0.f;
        bool mute = false;
    };

    void timerCallback() override;
    void applyMode();
    bool isMapMode() const { return proc.pval (mpMode) > 0.5f; }
    juce::Rectangle<float> mapArea() const { return juce::Rectangle<float> (24.0f, 90.0f, 600.0f, 560.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    int dotAt (juce::Point<float> p) const;
    void writeCommand (int slot, float x, float y, float depth, bool mute);
    juce::Colour dotColour (int slot) const;
    void paintMap (juce::Graphics& g);
    void paintRelay (juce::Graphics& g);

    MapProcessor& proc;
    LabLook look;
    juce::Image logo;

    juce::ComboBox modeBox;
    std::unique_ptr<ComboAtt> modeAtt;
    juce::TextEditor nameEditor;
    juce::ToggleButton muteBtn { "MUTE" };
    std::unique_ptr<ButtonAtt> muteAtt;
    juce::TextButton resetBtn { "RESET POSITION" };

    std::vector<Dot> dots;
    int dragSlot = -1;
    float dragX = 0.f, dragY = 0.f;
    int lastMode = -1;
    float meterSmooth[2] = { 0.f, 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MapEditor)
};
