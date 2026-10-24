#include "MapEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff070a0f);
const juce::Colour kPanel (0xff0c1118);
const juce::Colour kEdge (0xff1a2a3d);
const juce::Colour kText (0xffb9c8da);
const juce::Colour kDim (0xff5f7189);
const juce::Colour kAccent (0xff4aa8ff);

float dbToY (float db)
{
    if (db >= 0.0f) return mvmap::kDefaultY + db / 6.0f * (1.0f - mvmap::kDefaultY);
    return mvmap::kDefaultY * (1.0f - std::pow (-db / 50.0f, 1.0f / 1.5f));
}
}

MapEditor::MapEditor (MapProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    modeBox.addItemList (juce::StringArray { "RELAY (on a channel)", "MAP (on the bus)" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<ComboAtt> (proc.apvts, kMapIds[mpMode], modeBox);

    nameEditor.setInputRestrictions (24);
    nameEditor.setText (proc.getLabel(), juce::dontSendNotification);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111b28));
    nameEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8e4f2));
    nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff223550));
    nameEditor.onTextChange = [this] { proc.setLabel (nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    addAndMakeVisible (muteBtn);
    muteAtt = std::make_unique<ButtonAtt> (proc.apvts, kMapIds[mpMute], muteBtn);

    resetBtn.onClick = [this] { proc.resetPosition(); };
    addAndMakeVisible (resetBtn);

    applyMode();
    startTimerHz (30);
}

MapEditor::~MapEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MapEditor::applyMode()
{
    const bool map = isMapMode();
    lastMode = map ? 1 : 0;
    nameEditor.setVisible (! map);
    muteBtn.setVisible (! map);
    resetBtn.setVisible (! map);
    setSize (map ? 920 : 460, map ? 680 : 300);
}

void MapEditor::resized()
{
    modeBox.setBounds (getWidth() - 220, 22, 200, 28);
    if (! isMapMode())
    {
        nameEditor.setBounds (24, 128, 230, 28);
        muteBtn.setBounds (270, 128, 100, 28);
        resetBtn.setBounds (24, 232, 150, 30);
    }
}

juce::Colour MapEditor::dotColour (int slot) const
{
    return juce::Colour::fromHSV (std::fmod ((float) slot * 0.137f, 1.0f), 0.65f, 1.0f, 1.0f);
}

juce::Point<float> MapEditor::toScreen (float x, float y) const
{
    const auto a = mapArea();
    return { a.getX() + (x * 0.5f + 0.5f) * a.getWidth(), a.getY() + (1.0f - y) * a.getHeight() };
}

int MapEditor::dotAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 26.0f;
    for (const auto& d : dots)
    {
        const auto c = toScreen (d.x, d.y);
        const float dist = c.getDistanceFrom (p);
        if (dist < bestD) { bestD = dist; best = d.slot; }
    }
    return best;
}

void MapEditor::writeCommand (int slot, float x, float y, float depth, bool mute)
{
    auto& sl = mvmap::Registry::get().slots[slot];
    sl.cmdX.store (x);
    sl.cmdY.store (y);
    sl.cmdDepth.store (depth);
    sl.cmdMute.store (mute);
    sl.editSeq.fetch_add (1);
}

void MapEditor::timerCallback()
{
    const int mode = isMapMode() ? 1 : 0;
    if (mode != lastMode)
    {
        applyMode();
        resized();
    }
    if (mode == 1)
    {
        auto& R = mvmap::Registry::get();
        dots.clear();
        for (int i = 0; i < mvmap::kMaxSlots; ++i)
        {
            auto& s = R.slots[i];
            if (s.id.load() == 0)
                continue;
            Dot d;
            d.slot = i;
            char lab[32];
            R.getLabel (i, lab);
            d.label = juce::String::fromUTF8 (lab);
            d.x = s.x.load();
            d.y = s.y.load();
            d.depth = s.depth.load();
            d.mute = s.mute.load();
            const float db = juce::Decibels::gainToDecibels (std::max (s.peakL.load(), s.peakR.load()), -60.0f);
            d.peak = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (i == dragSlot)
            {
                d.x = dragX;
                d.y = dragY;
            }
            dots.push_back (d);
        }
    }
    else
    {
        for (int c = 0; c < 2; ++c)
        {
            const float db = juce::Decibels::gainToDecibels (c == 0 ? proc.peakL.load() : proc.peakR.load(), -60.0f);
            const float lvl = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            meterSmooth[c] = lvl > meterSmooth[c] ? lvl : meterSmooth[c] * 0.9f;
        }
        if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != proc.getLabel())
            nameEditor.setText (proc.getLabel(), juce::dontSendNotification);
    }
    repaint();
}

void MapEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! isMapMode())
        return;
    dragSlot = dotAt (e.position);
    for (const auto& d : dots)
        if (d.slot == dragSlot) { dragX = d.x; dragY = d.y; }
}

void MapEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! isMapMode() || dragSlot < 0)
        return;
    const auto a = mapArea();
    dragX = juce::jlimit (-1.0f, 1.0f, ((e.position.x - a.getX()) / a.getWidth()) * 2.0f - 1.0f);
    dragY = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - a.getY()) / a.getHeight());
    auto& sl = mvmap::Registry::get().slots[dragSlot];
    writeCommand (dragSlot, dragX, dragY, sl.depth.load(), sl.mute.load());
}

void MapEditor::mouseUp (const juce::MouseEvent&)
{
    dragSlot = -1;
}

void MapEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isMapMode())
        return;
    const int s = dotAt (e.position);
    if (s < 0)
        return;
    auto& sl = mvmap::Registry::get().slots[s];
    writeCommand (s, sl.x.load(), sl.y.load(), sl.depth.load(), ! sl.mute.load()); // doble click: mute
}

void MapEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isMapMode())
        return;
    const int s = dotAt (e.position);
    if (s < 0)
        return;
    auto& sl = mvmap::Registry::get().slots[s];
    const float depth = juce::jlimit (0.0f, 1.0f, sl.depth.load() + wheel.deltaY * 0.25f);
    writeCommand (s, sl.x.load(), sl.y.load(), depth, sl.mute.load()); // rueda: profundidad
}

void MapEditor::paintMap (juce::Graphics& g)
{
    const auto a = mapArea();
    g.setColour (kPanel);
    g.fillRoundedRectangle (a, 10.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (a.reduced (0.5f), 10.0f, 1.0f);

    // lineas de nivel
    g.setFont (10.0f);
    static const float marks[6] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f };
    for (float db : marks)
    {
        const float y = a.getY() + (1.0f - dbToY (db)) * a.getHeight();
        g.setColour (db == 0.0f ? juce::Colour (0xff2b4a73) : kEdge);
        g.drawLine (a.getX() + 40.0f, y, a.getRight() - 6.0f, y, db == 0.0f ? 1.5f : 1.0f);
        g.setColour (kDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", (int) a.getX() + 4, (int) y - 6, 40, 12, juce::Justification::left);
    }
    g.setColour (kEdge);
    g.drawLine (a.getCentreX(), a.getY() + 6.0f, a.getCentreX(), a.getBottom() - 6.0f, 1.0f);
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText ("L", (int) a.getX() + 46, (int) a.getBottom() - 20, 20, 14, juce::Justification::left);
    g.drawText ("R", (int) a.getRight() - 24, (int) a.getBottom() - 20, 20, 14, juce::Justification::right);
    g.drawText ("PAN", (int) a.getCentreX() - 20, (int) a.getBottom() - 20, 40, 14, juce::Justification::centred);

    if (dots.empty())
    {
        g.setColour (kText);
        g.setFont (14.0f);
        g.drawFittedText ("No channels yet.\nPut MV MAP on each channel (mode RELAY),\nthen come back here.",
                          a.reduced (60.0f).toNearestInt(), juce::Justification::centred, 4);
    }

    for (const auto& d : dots)
    {
        const auto c = toScreen (d.x, d.y);
        const float rad = 9.0f + d.peak * 13.0f;
        auto col = dotColour (d.slot);
        if (d.mute)
            col = col.withAlpha (0.25f);
        if (d.depth > 0.01f) // profundidad: aro difuso alrededor del punto
        {
            g.setColour (col.withAlpha (0.35f));
            const float rr = rad * (1.0f + d.depth * 2.2f);
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 1.5f);
        }
        juce::ColourGradient glow (col.withAlpha (0.5f), c.x, c.y, col.withAlpha (0.0f), c.x + rad * 2.4f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - rad * 2.4f, c.y - rad * 2.4f, rad * 4.8f, rad * 4.8f);
        g.setColour (col);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (d.mute ? kDim : juce::Colours::white);
        g.setFont (11.0f);
        g.drawText (d.label, (int) c.x - 50, (int) (c.y + rad + 3.0f), 100, 14, juce::Justification::centred);
    }

    // lista lateral
    const juce::Rectangle<int> list (644, 90, 252, 560);
    g.setColour (kPanel);
    g.fillRoundedRectangle (list.toFloat(), 10.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (list.toFloat().reduced (0.5f), 10.0f, 1.0f);
    g.setColour (kAccent);
    g.setFont (12.0f);
    g.drawText ("CHANNELS", list.getX() + 14, list.getY() + 8, 200, 16, juce::Justification::left);
    int row = 0;
    for (const auto& d : dots)
    {
        if (row >= 18)
            break;
        const int y = list.getY() + 34 + row * 28;
        g.setColour (dotColour (d.slot).withAlpha (d.mute ? 0.3f : 1.0f));
        g.fillEllipse ((float) list.getX() + 14.0f, (float) y + 6.0f, 10.0f, 10.0f);
        g.setColour (d.mute ? kDim : kText);
        g.setFont (12.0f);
        g.drawText (d.label, list.getX() + 32, y, 110, 22, juce::Justification::left);
        g.setColour (kDim);
        const float db = mvmap::yToDb (d.y);
        g.drawText (db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1) + " dB", list.getX() + 140, y, 56, 22, juce::Justification::right);
        const int pan = juce::roundToInt (d.x * 100.0f);
        g.drawText (pan == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (std::abs (pan)), list.getX() + 198, y, 40, 22, juce::Justification::right);
        ++row;
    }
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawFittedText ("Drag a dot: left/right = pan, up/down = level.\nMouse wheel on a dot = depth.\nDouble click = mute.",
                      juce::Rectangle<int> (list.getX() + 14, list.getBottom() - 62, list.getWidth() - 28, 54), juce::Justification::topLeft, 4);
}

void MapEditor::paintRelay (juce::Graphics& g)
{
    g.setColour (kText);
    g.setFont (13.0f);
    g.drawText ("RELAY  /  name of this channel:", 24, 104, 300, 18, juce::Justification::left);

    const float x = proc.pval (mpX), y = proc.pval (mpY), depth = proc.pval (mpDepth);
    g.setColour (kAccent);
    g.setFont (12.0f);
    const int pan = juce::roundToInt (x * 100.0f);
    const float db = mvmap::yToDb (y);
    g.drawText ("PAN " + (pan == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (std::abs (pan)))
                    + "     LEVEL " + (db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1) + " dB")
                    + "     DEPTH " + juce::String (juce::roundToInt (depth * 100.0f)) + " %",
                24, 172, 420, 18, juce::Justification::left);

    for (int c = 0; c < 2; ++c)
    {
        const juce::Rectangle<float> r (24.0f, 198.0f + (float) c * 12.0f, 330.0f, 8.0f);
        g.setColour (juce::Colour (0xff17212f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kAccent);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * meterSmooth[c]), 3.0f);
    }
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawFittedText (proc.getSlot() >= 0 ? "Registered. Open MV MAP in mode MAP on the bus to move me."
                                           : "Not registered yet (too many channels, or switching mode).",
                      juce::Rectangle<int> (190, 232, 250, 40), juce::Justification::topLeft, 3);
}

void MapEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    if (logo.isValid())
        g.drawImageWithin (logo, 14, 8, 62, 54, juce::RectanglePlacement::centred);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (28.0f).withStyle ("Bold")));
    g.drawText ("MV MAP", 86, 8, 300, 34, juce::Justification::left);
    g.setColour (kDim);
    g.setFont (11.0f);
    g.drawText (juce::String::fromUTF8 ("MA\xC3\x91" "ANA VEMOS  /  2D MIX MAP"), 88, 42, 320, 14, juce::Justification::left);

    if (isMapMode())
        paintMap (g);
    else
        paintRelay (g);
}
