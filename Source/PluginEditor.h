#pragma once

#include "PluginProcessor.h"
#include "OctoLookAndFeel.h"
#include "SpaceBackground.h"
#include "WaveformView.h"

namespace octo
{
    /** Rotary knob with a name and live value read-out. */
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name,
              std::function<juce::String (double)> formatter);
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::Slider slider;

    private:
        juce::String name;
        std::function<juce::String (double)> format;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Segmented selector bound to a choice parameter. */
    class ChoiceSelector : public juce::Component, private juce::Timer
    {
    public:
        ChoiceSelector (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        int getIndex() const { return index; }
        float fontSize = 11.0f;

    private:
        juce::StringArray options;
        juce::ParameterAttachment attachment;
        juce::RangedAudioParameter& param;
        int index = 0, hover = -1;
        float animIndex = 0.0f;
        int segmentAt (int x) const;
        void timerCallback() override;
    };

    /** Drag this onto a DAW track to get one MIDI note per slice. */
    class MidiDragButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MidiDragButton (OctoSliceProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        void refresh() { repaint(); }

    private:
        OctoSliceProcessor& proc;
        bool dragging = false;
    };
}

class OctoSliceEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget
{
public:
    explicit OctoSliceEditor (OctoSliceProcessor&);
    ~OctoSliceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; waveform.setDragHover (true); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; waveform.setDragHover (false); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Advances all animations (also used by the offline renderer). */
    void advanceFrame (float dt);
    double getAveragePaintMs() const { return frameMsAverage; }
    WaveformView& getWaveform() { return waveform; }

private:
    void openFileChooser();
    void updateVisibility();
    void drawLogo (juce::Graphics&, juce::Rectangle<float>);
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void renderChrome (juce::Graphics&);

    OctoSliceProcessor& proc;
    octo::OctoLookAndFeel lnf;
    octo::SpaceBackground space;
    WaveformView waveform;

    juce::TextButton loadButton { "LOAD" };
    octo::MidiDragButton midiButton;

    octo::ChoiceSelector modeSel, divSel, barsSel, playModeSel;
    juce::ToggleButton zeroX { "ZERO-X" }, choke { "MONO" }, reverse { "REVERSE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zeroXAtt, chokeAtt, reverseAtt;
    octo::Knob sens, minLen, root, attack, release, pitch, tone, spaceKnob, gain;
    juce::TextButton clearButton { "CLEAR" }, gridButton { "BACK TO GRID" };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::VBlankAttachment> vblank;

    juce::Rectangle<int> slicePanel, playPanel, soundPanel;
    double lastFrameTime = 0.0;
    float time = 0.0f;
    bool dragHover = false;
    std::array<juce::uint32, octo::maxSlices> lastTrig {};

    // static parts of the UI (panels, labels, header text) cached in an image
    juce::Image chromeImage;
    juce::String chromeKey;
    juce::uint32 lastSnapshotVersion = 0xffffffff;
    double frameMsAverage = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctoSliceEditor)
};
