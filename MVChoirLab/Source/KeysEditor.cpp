#include "KeysEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff070a0f);
const juce::Colour kPanel (0xff0c1118);
const juce::Colour kEdge (0xff1a2a3d);
const juce::Colour kText (0xffb9c8da);
const juce::Colour kDim (0xff5f7189);
const juce::Colour kAccent (0xff4aa8ff);
constexpr int kCell = 62;
constexpr int kGroupH = 128;
}

KeysEditor::KeysEditor (KeysProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    art = juce::ImageCache::getFromMemory (BinaryData::art_jpg, BinaryData::art_jpgSize);

    const juce::Colour c1 (0xff4aa8ff), c2 (0xff3de8d0), c3 (0xff9b7bff), c4 (0xff8fd0ff);

    rootKnob = makeKnob (kpRoot, "ROOT NOTE", c2);
    fineKnob = makeKnob (kpRootFine, "ROOT FINE", c2);

    addGroup ("ENVELOPE", 0, { makeKnob (kpAttack, "ATTACK", c1), makeKnob (kpRelease, "RELEASE", c1), makeKnob (kpStrum, "STRUM", c1) });
    addGroup ("VOICES", 0, { makeKnob (kpUnison, "UNISON", c3), makeKnob (kpChordOct, "OCTAVE", c3), makeKnob (kpDetune, "DETUNE", c3),
                             makeKnob (kpHum, "HUMANIZE", c3), makeKnob (kpMove, "MOVEMENT", c3), makeKnob (kpWidth, "WIDTH", c3) });
    addGroup ("OUTPUT", 0, { makeKnob (kpOut, "OUTPUT", c4), makeKnob (kpVelSens, "VELOCITY", c4) });
    addGroup ("TONE / MUTATION", 1, { makeKnob (kpBright, "BRIGHT", c2), makeKnob (kpFormant, "FORMANT", c2), makeKnob (kpMutAmt, "MUT AMT", c2) });
    addGroup ("SPACE", 1, { makeKnob (kpRevSize, "REV SIZE", c4), makeKnob (kpRevLevel, "REV LEVEL", c4), makeKnob (kpRevSend, "REV SEND", c4) });

    modeBox.addItemList (juce::StringArray { "DIRECT", "CHORD" }, 1);
    dirBox.addItemList (juce::StringArray { "STRUM UP", "STRUM DOWN", "STRUM RANDOM" }, 1);
    mutBox.addItemList (juce::StringArray { "OFF", "WHISPER", "BLUR", "MORPH" }, 1);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (dirBox);
    addAndMakeVisible (mutBox);
    modeAtt = std::make_unique<ComboAtt> (proc.apvts, kKeysIds[kpMode], modeBox);
    dirAtt = std::make_unique<ComboAtt> (proc.apvts, kKeysIds[kpStrumDir], dirBox);
    mutAtt = std::make_unique<ComboAtt> (proc.apvts, kKeysIds[kpMutMode], mutBox);

    for (int i = 0; i < 12; ++i)
    {
        chordBtns[i].setButtonText (kChordLabels[i]);
        chordBtns[i].setClickingTogglesState (false);
        chordBtns[i].onClick = [this, i] { setChoice (kpChord, i); };
        addAndMakeVisible (chordBtns[i]);
    }

    loadBtn.onClick = [this] { loadFromChooser(); };
    recBtn.onClick = [this]
    {
        if (proc.isRecording())
            proc.stopRecording();
        else
            proc.startRecording();
    };
    folderBtn.onClick = []
    {
        auto dir = StemRecorder::getFolder();
        dir.createDirectory();
        dir.revealToUser();
    };
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (recBtn);
    addAndMakeVisible (folderBtn);

    keyboard.setAvailableRange (36, 96);
    addAndMakeVisible (keyboard);

    setSize (1100, 660);
    startTimerHz (30);
}

KeysEditor::~KeysEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

KeysEditor::Knob* KeysEditor::makeKnob (int paramIdx, const juce::String& label, juce::Colour colour)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, colour);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->att = std::make_unique<SliderAtt> (proc.apvts, kKeysIds[paramIdx], s);
    addAndMakeVisible (s);
    Knob* raw = k.get();
    knobStore.push_back (std::move (k));
    return raw;
}

void KeysEditor::addGroup (const juce::String& title, int row, std::vector<Knob*> knobs)
{
    Group g;
    g.title = title;
    g.row = row;
    g.knobs = std::move (knobs);
    groups.push_back (std::move (g));
}

void KeysEditor::setChoice (int paramIdx, int index)
{
    if (auto* prm = proc.apvts.getParameter (kKeysIds[paramIdx]))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

void KeysEditor::loadFromChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Elegi una grabacion de voz (una vocal sostenida, tipo aaah)",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            proc.loadSample (f);
    });
}

void KeysEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
        proc.loadSample (juce::File (files[0]));
}

void KeysEditor::timerCallback()
{
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        const float db = juce::Decibels::gainToDecibels (proc.voiceMeter[v].load(), -60.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        voiceSmooth[v] = lvl > voiceSmooth[v] ? lvl : voiceSmooth[v] * 0.9f;
    }
    const int chord = juce::roundToInt (proc.pval (kpChord));
    for (int i = 0; i < 12; ++i)
        chordBtns[i].setToggleState (i == chord, juce::dontSendNotification);
    if (shownVersion != proc.sampleVersion.load())
    {
        shownVersion = proc.sampleVersion.load();
        wave = proc.waveform;
        shownName = proc.sampleName;
    }
    recBtn.setButtonText (proc.isRecording() ? "STOP REC" : "REC STEMS");
    recBtn.setColour (juce::TextButton::buttonColourId, proc.isRecording() ? juce::Colour (0xffb3261e) : juce::Colour (0xff111b28));
    repaint();
}

void KeysEditor::resized()
{
    samplePanel = { 12, 76, 300, 300 };
    platePanel = { 12, 384, 300, 160 };
    stagePanel = { 322, 76, 766, 174 };
    recPanel = { 0, 0, 0, 0 };

    loadBtn.setBounds (samplePanel.getX() + 14, samplePanel.getY() + 258, 130, 30);
    waveArea = { samplePanel.getX() + 14, samplePanel.getY() + 34, samplePanel.getWidth() - 28, 84 };
    rootKnob->slider.setBounds (samplePanel.getX() + 150, samplePanel.getY() + 172, 70, 86);
    fineKnob->slider.setBounds (samplePanel.getX() + 222, samplePanel.getY() + 172, 70, 86);

    auto pb = platePanel.reduced (10);
    pb.removeFromTop (18);
    auto line = pb.removeFromTop (26);
    modeBox.setBounds (line.removeFromLeft (110));
    line.removeFromLeft (8);
    dirBox.setBounds (line);
    pb.removeFromTop (6);
    const int cw = pb.getWidth() / 4;
    for (int i = 0; i < 12; ++i)
        chordBtns[i].setBounds (pb.getX() + (i % 4) * cw, pb.getY() + (i / 4) * 31, cw - 4, 27);

    const int baseY = stagePanel.getBottom() + 8;
    int cursorX[2] = { stagePanel.getX(), stagePanel.getX() };
    for (auto& g : groups)
    {
        const int extra = (g.row == 1 && g.title.startsWith ("TONE")) ? 1 : 0;
        const int w = ((int) g.knobs.size() + extra) * kCell + 16;
        g.bounds = { cursorX[g.row], baseY + g.row * (kGroupH + 8), w, kGroupH };
        cursorX[g.row] += w + 10;
        for (size_t i = 0; i < g.knobs.size(); ++i)
            g.knobs[i]->slider.setBounds (g.bounds.getX() + 8 + (int) i * kCell, g.bounds.getY() + 30, kCell, 90);
        if (extra == 1)
            mutBox.setBounds (g.bounds.getX() + 8 + (int) g.knobs.size() * kCell, g.bounds.getY() + 62, kCell, 26);
    }
    recPanel = { cursorX[1], baseY + kGroupH + 8, stagePanel.getRight() - cursorX[1], kGroupH };
    recBtn.setBounds (recPanel.getX() + 14, recPanel.getY() + 40, 120, 30);
    folderBtn.setBounds (recPanel.getX() + 14, recPanel.getY() + 82, 120, 30);

    keyboard.setBounds (12, getHeight() - 112, getWidth() - 24, 100);
    keyboard.setKeyWidth ((float) (getWidth() - 24) / 36.0f);
}

void KeysEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (kAccent);
        g.setFont (12.0f);
        g.drawText (title, r.getX() + 14, r.getY() + 8, r.getWidth() - 28, 16, juce::Justification::left);
    }
}

void KeysEditor::paintStage (juce::Graphics& g)
{
    auto r = stagePanel.toFloat();
    juce::Path clip;
    clip.addRoundedRectangle (r, 8.0f);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::black);
    g.fillRect (r);
    if (art.isValid())
    {
        g.setOpacity (0.8f);
        g.drawImage (art, r, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }
    juce::ColourGradient shade (juce::Colours::transparentBlack, r.getX(), r.getY() + r.getHeight() * 0.4f,
                                juce::Colour (0xe6070a0f), r.getX(), r.getBottom(), false);
    g.setGradientFill (shade);
    g.fillRect (r);
    for (int v = 0; v < mvc::kVoices; ++v)
    {
        const float cx = r.getX() + 50.0f + ((float) v + 0.5f) / (float) mvc::kVoices * (r.getWidth() - 100.0f);
        const float lvl = voiceSmooth[v];
        const float cy = r.getBottom() - 30.0f - lvl * (r.getHeight() - 80.0f);
        const float rad = 5.0f + lvl * 12.0f;
        const auto col = juce::Colour::fromHSV (0.55f + 0.04f * (float) v, 0.6f, 1.0f, 1.0f);
        juce::ColourGradient glow (col.withAlpha (0.5f), cx, cy, col.withAlpha (0.0f), cx + rad * 2.6f, cy, true);
        g.setGradientFill (glow);
        g.fillEllipse (cx - rad * 2.6f, cy - rad * 2.6f, rad * 5.2f, rad * 5.2f);
        g.setColour (col.withAlpha (0.35f + 0.65f * lvl));
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    }
    g.setColour (kAccent);
    g.setFont (11.0f);
    g.drawText ("CHOIR VOICES  /  PLAY THE KEYBOARD BELOW", stagePanel.getX() + 14, stagePanel.getY() + 8, 400, 14, juce::Justification::left);
    g.restoreState();
    g.setColour (kEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void KeysEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    if (logo.isValid())
        g.drawImageWithin (logo, 14, 8, 62, 54, juce::RectanglePlacement::centred);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f).withStyle ("Bold")));
    g.drawText ("CHOIR//LAB  KEYS", 86, 6, 420, 36, juce::Justification::left);
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText (juce::String::fromUTF8 ("MA\xC3\x91" "ANA VEMOS"), 88, 40, 280, 14, juce::Justification::left);
    g.drawText ("VOICE KEYBOARD  /  PLAY YOUR OWN VOICE", 88, 53, 360, 14, juce::Justification::left);
    const double lat = proc.getSampleRate() > 0 ? 1000.0 * proc.getLatencySamples() / proc.getSampleRate() : 0.0;
    g.drawText ("LATENCY " + juce::String (juce::roundToInt ((float) lat)) + " ms", getWidth() - 330, 30, 316, 14, juce::Justification::right);

    // ---- sample ----
    drawPanel (g, samplePanel, "VOICE SAMPLE");
    g.setColour (juce::Colour (0xff0f1722));
    g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
    if (wave.empty())
    {
        g.setColour (kDim);
        g.setFont (12.0f);
        g.drawText ("DROP A VOICE FILE HERE (WAV / AIFF / FLAC)", waveArea, juce::Justification::centred);
    }
    else
    {
        g.setColour (kAccent);
        const float midY = (float) waveArea.getCentreY();
        for (size_t i = 0; i < wave.size(); ++i)
        {
            const float x = (float) waveArea.getX() + 2.0f + (float) i / (float) wave.size() * (float) (waveArea.getWidth() - 4);
            const float h = wave[i] * (float) waveArea.getHeight() * 0.46f;
            g.drawLine (x, midY - h, x, midY + h, 1.0f);
        }
    }
    g.setColour (kText);
    g.setFont (12.0f);
    g.drawText (shownName.isEmpty() ? juce::String ("no sample loaded") : shownName, samplePanel.getX() + 14, waveArea.getBottom() + 6, samplePanel.getWidth() - 28, 16, juce::Justification::left);
    const int root = juce::roundToInt (proc.pval (kpRoot));
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText ("Detected pitch: " + midiNoteName (root) + " " + juce::String (juce::roundToInt (proc.pval (kpRootFine))) + " c",
                samplePanel.getX() + 14, waveArea.getBottom() + 24, samplePanel.getWidth() - 28, 14, juce::Justification::left);
    g.drawText ("Tip: one sustained vowel (aaah), dry, one voice.", samplePanel.getX() + 14, waveArea.getBottom() + 42, samplePanel.getWidth() - 28, 14, juce::Justification::left);
    g.drawText ("ROOT NOTE", rootKnob->slider.getX(), rootKnob->slider.getY() - 12, rootKnob->slider.getWidth(), 14, juce::Justification::centred);
    g.drawText ("ROOT FINE", fineKnob->slider.getX(), fineKnob->slider.getY() - 12, fineKnob->slider.getWidth(), 14, juce::Justification::centred);

    // ---- plate ----
    drawPanel (g, platePanel, "CHORD PLATE");
    paintStage (g);

    // ---- grupos de perillas ----
    for (const auto& grp : groups)
    {
        drawPanel (g, grp.bounds, grp.title);
        g.setColour (kDim);
        g.setFont (11.0f);
        for (auto* k : grp.knobs)
            g.drawText (k->label, k->slider.getX(), grp.bounds.getY() + 22, k->slider.getWidth(), 14, juce::Justification::centred);
    }
    g.setColour (kDim);
    g.drawText ("MUTATION", mutBox.getX(), mutBox.getY() - 16, mutBox.getWidth(), 14, juce::Justification::centred);

    drawPanel (g, recPanel, "EXPORT");
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText ("4 voices + mix as WAV", recPanel.getX() + 150, recPanel.getY() + 46, 150, 14, juce::Justification::left);
    g.drawText ("Documents / MV CHOIR LAB / Stems", recPanel.getX() + 150, recPanel.getY() + 88, 200, 14, juce::Justification::left);
}
