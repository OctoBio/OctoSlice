#pragma once

#include "PluginProcessor.h"
#include <functional>

/** Shows the loaded sample, its slices and live playheads. Markers are editable with the mouse. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (OctoSliceProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override { cacheValid = false; }

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Called every frame by the editor. */
    void tick (float dt);

    /** Centre of a slice in this component's coordinates (for particle bursts). */
    juce::Point<float> getSliceCentre (int slice) const;

    void setDragHover (bool h) { dragHover = h; }
    int getSelectedSlice() const { return selected; }

    std::function<void()> onRequestLoad;

private:
    juce::Rectangle<float> waveArea() const;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    int markerAt (float x) const;
    int sliceAt (float x) const;
    void rebuildCache();
    void drawEmpty (juce::Graphics&, juce::Rectangle<float>);
    void stopAudition();

    OctoSliceProcessor& proc;
    std::shared_ptr<const octo::Snapshot> snap;

    double viewStart = 0.0, viewEnd = 1.0;
    const octo::SampleData* cachedSample = nullptr;
    bool cacheValid = false;
    std::vector<float> colMin, colMax;

    int hoverMarker = -1, dragMarker = -1, hoverSlice = -1, selected = -1;
    int auditionNote = -1;
    bool dragHover = false;
    float time = 0.0f;
    std::array<float, octo::maxSlices> glow {};
    std::array<juce::uint32, octo::maxSlices> lastTrig {};
};
