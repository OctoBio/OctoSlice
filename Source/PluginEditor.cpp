#include "PluginEditor.h"
#include <cmath>

using namespace octo;

static juce::String fmtMs (double v)   { return v < 100.0 ? juce::String (v, 1) + " ms" : v < 1000.0 ? juce::String ((int) v) + " ms" : juce::String (v / 1000.0, 2) + " s"; }
static juce::String fmtPct (double v)  { return juce::String ((int) std::round (v)) + " %"; }
static juce::String fmtSemi (double v) { return (v > 0.004 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.005 ? 0 : 2) + " st"; }
static juce::String fmtHz (double v)   { return v >= 19950.0 ? juce::String ("OPEN") : v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"; }
static juce::String fmtDb (double v)   { return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB"; }
static juce::String fmtNote (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& n,
            std::function<juce::String (double)> f)
    : name (n), format (std::move (f))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    slider.setDoubleClickReturnValue (true, state.getParameter (id)->convertFrom0to1 (state.getParameter (id)->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    slider.setBounds (r.removeFromTop (juce::jmin (r.getWidth(), r.getHeight() - 28)).reduced (2));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().withTrimmedTop (slider.getBottom() + 1);
    g.setFont (uiFont (10.0f, true, 0.2f));
    g.setColour (slider.isMouseOverOrDragging() ? colours::text : colours::textDim);
    g.drawText (name, r.removeFromTop (13), juce::Justification::centred);
    g.setFont (uiFont (11.0f, false, 0.05f));
    g.setColour (slider.isMouseOverOrDragging() ? colours::cyan : colours::text.withAlpha (0.85f));
    g.drawText (format (slider.getValue()), r.removeFromTop (14), juce::Justification::centred);
}

//==============================================================================
ChoiceSelector::ChoiceSelector (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    : attachment (*state.getParameter (id), [this] (float v) { index = (int) std::lround (v); repaint(); }),
      param (*state.getParameter (id))
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param))
        options = c->choices;
    attachment.sendInitialUpdate();
    animIndex = (float) index;
}

int ChoiceSelector::segmentAt (int x) const
{
    if (options.isEmpty()) return -1;
    return juce::jlimit (0, options.size() - 1, x * options.size() / juce::jmax (1, getWidth()));
}

void ChoiceSelector::mouseDown (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.x);
    if (s >= 0) attachment.setValueAsCompleteGesture ((float) s);
}

void ChoiceSelector::mouseMove (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.x);
    if (s != hover) { hover = s; repaint(); }
}

void ChoiceSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r, corner, 1.0f);

    const int n = juce::jmax (1, options.size());
    const float segW = r.getWidth() / (float) n;
    animIndex += ((float) index - animIndex) * 0.35f;
    if (std::abs (animIndex - (float) index) < 0.002f) animIndex = (float) index;

    auto pill = juce::Rectangle<float> (r.getX() + animIndex * segW, r.getY(), segW, r.getHeight()).reduced (2.0f);
    g.setColour (colours::cyan.withAlpha (0.12f));
    g.fillRoundedRectangle (pill.expanded (2.0f), pill.getHeight() * 0.5f + 2.0f);
    juce::ColourGradient grad (colours::cyan.withAlpha (0.55f), pill.getX(), pill.getY(),
                               colours::violet.withAlpha (0.55f), pill.getRight(), pill.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 0.8f);

    g.setFont (uiFont (fontSize, true, 0.14f));
    for (int i = 0; i < options.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + segW * (float) i, r.getY(), segW, r.getHeight());
        g.setColour (i == index ? juce::Colours::white : (i == hover ? colours::text : colours::textDim));
        g.drawText (options[i].toUpperCase(), cell, juce::Justification::centred);
    }
}

//==============================================================================
void MidiDragButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = isMouseOver (true);
    const bool enabled = proc.getSnapshot()->sample != nullptr;
    const float corner = r.getHeight() * 0.5f;
    g.setColour (colours::pink.withAlpha (enabled ? (hot ? 0.22f : 0.1f) : 0.03f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (colours::pink.withAlpha (enabled ? (hot ? 0.9f : 0.5f) : 0.15f));
    g.drawRoundedRectangle (r, corner, 1.0f);

    // little piano-roll icon
    auto icon = r.removeFromLeft (r.getHeight()).reduced (8.0f, 9.0f).translated (6.0f, 0.0f);
    g.setColour (enabled ? colours::pink : colours::textFaint);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (icon.getX() + (float) i * icon.getWidth() * 0.3f, icon.getY() + (float) (2 - i) * icon.getHeight() * 0.36f,
                                icon.getWidth() * 0.45f, icon.getHeight() * 0.26f, 1.0f);

    g.setColour (enabled ? colours::text : colours::textFaint);
    g.setFont (uiFont (11.0f, true, 0.18f));
    g.drawText ("DRAG MIDI", r.withTrimmedRight (8.0f), juce::Justification::centred);
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const auto f = proc.createMidiFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
OctoSliceEditor::OctoSliceEditor (OctoSliceProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p), midiButton (p),
      modeSel (p.apvts, "mode"), divSel (p.apvts, "division"), barsSel (p.apvts, "bars"), playModeSel (p.apvts, "playMode"),
      sens     (p.apvts, "sens", "SENSITIVITY", fmtPct),
      minLen   (p.apvts, "minLen", "MIN LENGTH", fmtMs),
      root     (p.apvts, "root", "ROOT", fmtNote),
      attack   (p.apvts, "attack", "ATTACK", fmtMs),
      release  (p.apvts, "release", "RELEASE", fmtMs),
      pitch    (p.apvts, "pitch", "PITCH", fmtSemi),
      tone     (p.apvts, "tone", "TONE", fmtHz),
      spaceKnob(p.apvts, "space", "SPACE", fmtPct),
      gain     (p.apvts, "gain", "GAIN", fmtDb)
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    for (size_t i = 0; i < lastTrig.size(); ++i)
        lastTrig[i] = proc.sliceTriggers[i].load();

    addAndMakeVisible (waveform);
    waveform.onRequestLoad = [this] { openFileChooser(); };

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { openFileChooser(); };
    loadButton.setTooltip ("Load a sample (or drag & drop it on the plugin)");
    addAndMakeVisible (midiButton);
    midiButton.setTooltip ("Drag onto a DAW track: one MIDI note per slice, at the original timing");

    for (auto* c : { &modeSel, &divSel, &barsSel, &playModeSel })
        addAndMakeVisible (*c);
    modeSel.fontSize = 11.5f;
    divSel.fontSize = barsSel.fontSize = 10.5f;

    for (auto* t : { &zeroX, &choke, &reverse })
        addAndMakeVisible (*t);
    zeroX.setTooltip ("Move grid/transient slices to the nearest zero crossing (avoids clicks)");
    choke.setTooltip ("Each new slice cuts the previous one (classic slicer behaviour)");
    reverse.setTooltip ("Play slices backwards");
    zeroXAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "zeroX", zeroX);
    chokeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "choke", choke);
    reverseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "reverse", reverse);

    pitch.slider.getProperties().set ("bipolar", true);
    for (auto* k : { &sens, &minLen, &root, &attack, &release, &pitch, &tone, &spaceKnob, &gain })
        addAndMakeVisible (*k);

    addAndMakeVisible (clearButton);
    addAndMakeVisible (gridButton);
    clearButton.onClick = [this] { proc.clearMarkers(); };
    gridButton.onClick  = [this] { proc.resetToGrid(); };

    setSize (1000, 660);

    lastFrameTime = juce::Time::getMillisecondCounterHiRes();
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) ((now - lastFrameTime) / 1000.0);
        lastFrameTime = now;
        advanceFrame (dt);
    });
    updateVisibility();
}

OctoSliceEditor::~OctoSliceEditor()
{
    vblank.reset();
    setLookAndFeel (nullptr);
}

void OctoSliceEditor::advanceFrame (float dt)
{
    dt = juce::jlimit (0.0f, 0.1f, dt);
    time += dt;
    waveform.tick (dt);

    auto snap = proc.getSnapshot();
    const int n = snap->numSlices();
    for (int i = 0; i < maxSlices; ++i)
    {
        const auto t = proc.sliceTriggers[(size_t) i].load();
        if (t != lastTrig[(size_t) i])
        {
            lastTrig[(size_t) i] = t;
            space.burst (waveform.getSliceCentre (i) + waveform.getPosition().toFloat(), sliceColour (i, n), 1.0f);
            space.kick (0.6f);
        }
    }

    SpaceBackground::Drive d;
    d.level = juce::jlimit (0.0f, 1.0f, proc.outputLevel.load() * 3.0f);
    d.pitch = proc.apvts.getRawParameterValue ("pitch")->load();
    d.space = proc.apvts.getRawParameterValue ("space")->load() / 100.0f;
    const float toneHz = proc.apvts.getRawParameterValue ("tone")->load();
    d.tone = juce::jlimit (0.0f, 1.0f, std::log (toneHz / 80.0f) / std::log (20000.0f / 80.0f));
    d.reverse = proc.apvts.getRawParameterValue ("reverse")->load() > 0.5f;
    d.numSlices = n;
    space.tick (dt, d);

    updateVisibility();
    repaint();
}

void OctoSliceEditor::updateVisibility()
{
    const int mode = modeSel.getIndex();
    divSel.setVisible (mode == 0);
    barsSel.setVisible (mode == 0);
    sens.setVisible (mode == 1);
    minLen.setVisible (mode == 1);
    zeroX.setVisible (mode != 2);
    clearButton.setVisible (mode == 2);
    gridButton.setVisible (mode == 2);
}

void OctoSliceEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadFile (f);
                          });
}

bool OctoSliceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void OctoSliceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    waveform.setDragHover (false);
    for (auto& f : files)
        if (proc.loadFile (juce::File (f)))
            break;
}

//==============================================================================
void OctoSliceEditor::resized()
{
    loadButton.setBounds (getWidth() - 24 - 86, 16, 86, 30);
    midiButton.setBounds (loadButton.getX() - 10 - 124, 16, 124, 30);

    waveform.setBounds (24, 64, getWidth() - 48, 300);

    slicePanel = { 24, 404, 400, 232 };
    playPanel  = { 436, 404, 196, 232 };
    soundPanel = { 644, 404, getWidth() - 644 - 24, 232 };

    // SLICE
    {
        auto r = slicePanel.reduced (16).withTrimmedTop (22);
        modeSel.setBounds (r.removeFromTop (30));
        r.removeFromTop (14);
        auto row1 = r.removeFromTop (26);
        divSel.setBounds (row1.withTrimmedLeft (60));
        r.removeFromTop (10);
        auto row2 = r.removeFromTop (26);
        barsSel.setBounds (row2.withTrimmedLeft (60));

        auto k = slicePanel.reduced (16).withTrimmedTop (22 + 30 + 8);
        sens.setBounds (k.getX() + 4, k.getY(), 92, 100);
        minLen.setBounds (k.getX() + 108, k.getY(), 92, 100);

        zeroX.setBounds (slicePanel.getRight() - 16 - 104, slicePanel.getBottom() - 16 - 26, 104, 26);
        clearButton.setBounds (slicePanel.getX() + 16, slicePanel.getBottom() - 16 - 28, 100, 28);
        gridButton.setBounds (clearButton.getRight() + 10, clearButton.getY(), 140, 28);
    }

    // PLAY
    {
        root.setBounds (playPanel.getX() + 10, playPanel.getY() + 34, 84, 102);
        choke.setBounds (playPanel.getX() + 96, playPanel.getY() + 52, 88, 26);
        reverse.setBounds (playPanel.getX() + 96, playPanel.getY() + 88, 88, 26);
        playModeSel.setBounds (playPanel.getX() + 14, playPanel.getBottom() - 16 - 28, playPanel.getWidth() - 28, 28);
    }

    // SOUND
    {
        auto r = soundPanel.reduced (10).withTrimmedTop (18);
        const int cw = r.getWidth() / 3, ch = r.getHeight() / 2;
        octo::Knob* knobs[] = { &attack, &release, &pitch, &tone, &spaceKnob, &gain };
        for (int i = 0; i < 6; ++i)
            knobs[i]->setBounds (r.getX() + (i % 3) * cw, r.getY() + (i / 3) * ch, cw, ch);
    }
}

void OctoSliceEditor::drawLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float lvl = juce::jlimit (0.0f, 1.0f, proc.outputLevel.load() * 3.0f);
    const auto c = juce::Point<float> (r.getX() + 16.0f, r.getCentreY());
    const float rad = 13.0f + 1.5f * lvl;

    juce::Path oct;
    for (int i = 0; i < 8; ++i)
    {
        const float a = time * 0.25f + juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / 8.0f;
        const auto pt = c.getPointOnCircumference (rad, a);
        if (i == 0) oct.startNewSubPath (pt); else oct.lineTo (pt);
    }
    oct.closeSubPath();

    g.setColour (colours::cyan.withAlpha (0.12f + 0.25f * lvl));
    g.strokePath (oct, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved));
    juce::ColourGradient grad (colours::cyan, c.x - rad, c.y - rad, colours::pink, c.x + rad, c.y + rad, false);
    g.setGradientFill (grad);
    g.strokePath (oct, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    // the "slice"
    const float a = -0.9f + 0.15f * std::sin (time * 1.3f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine ({ c.getPointOnCircumference (rad + 4.0f, a), c.getPointOnCircumference (rad + 4.0f, a + juce::MathConstants<float>::pi) }, 1.6f);

    auto textArea = r.withTrimmedLeft (42.0f);
    g.setFont (uiFont (22.0f, true, 0.32f));
    g.setColour (colours::text);
    const auto octoW = juce::GlyphArrangement::getStringWidth (uiFont (22.0f, true, 0.32f), "OCTO");
    g.drawText ("OCTO", textArea, juce::Justification::centredLeft);
    juce::ColourGradient tg (colours::cyan, textArea.getX() + octoW, 0.0f, colours::pink, textArea.getX() + octoW + 90.0f, 0.0f, false);
    g.setGradientFill (tg);
    g.drawText ("SLICE", textArea.withTrimmedLeft (octoW - 3.0f), juce::Justification::centredLeft);
}

void OctoSliceEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    drawGlassPanel (g, r.toFloat(), 14.0f);
    auto t = r.reduced (16, 12).removeFromTop (14).toFloat();
    g.setColour (colours::cyan);
    g.fillEllipse (t.getX(), t.getCentreY() - 2.5f, 5.0f, 5.0f);
    g.setColour (colours::cyan.withAlpha (0.3f));
    g.fillEllipse (t.getX() - 3.0f, t.getCentreY() - 5.5f, 11.0f, 11.0f);
    g.setFont (uiFont (11.0f, true, 0.4f));
    g.setColour (colours::textDim);
    g.drawText (title, t.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
}

void OctoSliceEditor::paint (juce::Graphics& g)
{
    space.draw (g, getLocalBounds().toFloat());

    // --- header ---------------------------------------------------------------
    drawLogo (g, { 26.0f, 12.0f, 260.0f, 38.0f });

    auto snap = proc.getSnapshot();
    auto info = juce::Rectangle<float> (300.0f, 14.0f, (float) midiButton.getX() - 316.0f, 34.0f);
    if (snap->sample)
    {
        const auto& s = *snap->sample;
        g.setFont (uiFont (13.0f, true, 0.06f));
        g.setColour (colours::text);
        g.drawText (s.name, info.removeFromTop (18.0f), juce::Justification::centredRight, true);
        g.setFont (uiFont (10.5f, false, 0.12f));
        g.setColour (colours::textDim);
        const juce::String details = juce::String (snap->numSlices()) + " SLICES   "
                                   + juce::String (s.audio.getNumSamples() / s.sampleRate, 2) + " S   "
                                   + juce::String (s.sampleRate / 1000.0, 1) + " KHZ   "
                                   + juce::String (proc.hostBpm.load(), 1) + " BPM";
        g.drawText (details, info, juce::Justification::centredRight, true);
    }
    else
    {
        g.setFont (uiFont (11.0f, false, 0.2f));
        g.setColour (colours::textFaint);
        g.drawText ("NO SAMPLE LOADED", info, juce::Justification::centredRight);
    }

    // --- hint row --------------------------------------------------------------
    {
        auto row = juce::Rectangle<float> (32.0f, 370.0f, (float) getWidth() - 64.0f, 24.0f);
        g.setFont (uiFont (10.5f, false, 0.12f));
        g.setColour (colours::textDim);
        const int sel = waveform.getSelectedSlice();
        if (snap->sample && sel >= 0 && sel < snap->numSlices())
        {
            const int note = (int) proc.apvts.getRawParameterValue ("root")->load() + sel;
            const double len = (snap->sliceEnd (sel) - snap->sliceStart (sel)) / snap->sample->sampleRate;
            g.setColour (sliceColour (sel, snap->numSlices()));
            g.drawText ("SLICE " + juce::String (sel + 1) + "  -  " + (note <= 127 ? fmtNote (note) : juce::String ("--"))
                        + "  -  " + juce::String (len * 1000.0, 0) + " MS", row, juce::Justification::centredLeft);
        }
        g.setColour (colours::textFaint);
        g.drawText (snap->sample ? "CLICK: PLAY   DOUBLE-CLICK: ADD   DRAG: MOVE   RIGHT-CLICK: DELETE   WHEEL: ZOOM"
                                 : "DRAG & DROP AUDIO ANYWHERE ON THE PLUGIN",
                    row, juce::Justification::centredRight);
    }

    // --- panels ------------------------------------------------------------------
    drawSection (g, slicePanel, "SLICE");
    drawSection (g, playPanel, "PLAY");
    drawSection (g, soundPanel, "SOUND");

    const int mode = modeSel.getIndex();
    g.setFont (uiFont (10.0f, true, 0.2f));
    g.setColour (colours::textDim);
    if (mode != 2 && snap->sample)
    {
        const auto readout = juce::Rectangle<float> ((float) slicePanel.getX() + 18.0f, (float) slicePanel.getBottom() - 50.0f, 200.0f, 36.0f);
        g.setFont (uiFont (30.0f, true, 0.02f));
        juce::ColourGradient rg (colours::cyan, readout.getX(), 0.0f, colours::violet, readout.getX() + 60.0f, 0.0f, false);
        g.setGradientFill (rg);
        const juce::String num (snap->numSlices());
        g.drawText (num, readout, juce::Justification::centredLeft);
        const float nw = juce::GlyphArrangement::getStringWidth (uiFont (30.0f, true, 0.02f), num);
        g.setFont (uiFont (10.0f, true, 0.3f));
        g.setColour (colours::textDim);
        g.drawText ("SLICES", readout.withTrimmedLeft (nw + 8.0f).withTrimmedTop (8.0f), juce::Justification::centredLeft);
    }
    g.setFont (uiFont (10.0f, true, 0.2f));
    g.setColour (colours::textDim);
    if (mode == 0)
    {
        g.drawText ("DIVIDE", juce::Rectangle<int> (slicePanel.getX() + 16, divSel.getY(), 56, divSel.getHeight()), juce::Justification::centredLeft);
        g.drawText ("BARS", juce::Rectangle<int> (slicePanel.getX() + 16, barsSel.getY(), 56, barsSel.getHeight()), juce::Justification::centredLeft);
    }
    else if (mode == 2)
    {
        auto r = slicePanel.reduced (16).withTrimmedTop (22 + 30 + 14).removeFromTop (70).toFloat();
        g.setFont (uiFont (11.0f, false, 0.06f));
        g.setColour (colours::textDim);
        g.drawFittedText ("Manual mode: double-click the waveform to add a slice,\n"
                          "drag a marker to move it, right-click (or Alt-click) to delete.\n"
                          "Each slice plays on its own key, starting from ROOT.",
                          r.toNearestInt(), juce::Justification::topLeft, 4);
    }

    if (dragHover)
    {
        g.setColour (colours::cyan.withAlpha (0.05f));
        g.fillAll();
    }
}
