#pragma once

#include "PluginProcessor.h"
#include <functional>

/** Shows the loaded sample, its slices and live playheads.
    - zoom: mouse wheel, +/- buttons, overview strip (drag to scroll), keys + - 0
    - markers: click / Shift-click / lasso to select, drag to move them all, Del or right-click to delete,
      arrows to nudge, double-click to add
    - trim: drag the START / END brackets, or AUTO TRIM */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (OctoSliceProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Called every frame by the editor. */
    void tick (float dt);

    /** Centre of a slice in this component's coordinates (for particle bursts). */
    juce::Point<float> getSliceCentre (int slice) const;

    void setDragHover (bool h) { if (dragHover != h) { dragHover = h; baseDirty = true; } }
    int getSelectedSlice() const { return selectedSlice; }
    int getNumSelectedMarkers() const { return (int) selection.size(); }
    double getZoomFactor() const;

    void zoomBy (double factor, double anchorSample = -1.0);
    void zoomToFit();
    void setSelection (std::vector<int> markers) { selection = std::move (markers); baseDirty = true; }

    std::function<void()> onRequestLoad;

private:
    enum class Drag { none, pending, markers, trimStart, trimEnd, lasso, overview };

    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> overviewArea() const;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    int markerAt (float x) const;
    int trimHandleAt (float x) const;   // 1 = start, 2 = end, 0 = none
    int sliceAt (float x) const;
    bool isSelected (int marker) const;
    void setView (double start, double span);
    void applyDrag (float x, bool shift);

    void rebuildColumns();
    void rebuildOverview();
    void renderBase (juce::Graphics&);
    void drawEmpty (juce::Graphics&, juce::Rectangle<float>);
    void stopAudition();

    OctoSliceProcessor& proc;
    std::shared_ptr<const octo::Snapshot> snap;

    juce::TextButton zoomOutButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) }, zoomInButton { "+" }, fitButton { "FIT" }, trimButton { "AUTO TRIM" };

    double viewStart = 0.0, viewEnd = 1.0;
    const octo::SampleData* cachedSample = nullptr;
    bool columnsValid = false;
    std::vector<float> colMin, colMax, ovMin, ovMax;
    juce::Path wavePath;

    // cached static layer (redrawn only when something changes)
    juce::Image baseImage;
    bool baseDirty = true;
    const octo::Snapshot* baseSnap = nullptr;
    float baseScale = 0.0f;
    int baseRoot = -1;

    // interaction
    Drag drag = Drag::none;
    std::vector<int> selection, lassoBaseSelection, dragBase;
    double dragStartSample = 0.0, overviewGrab = 0.0;
    float downX = 0.0f, lassoX0 = 0.0f, lassoX1 = 0.0f, lastMouseX = 0.0f;
    bool lastShift = false;
    int hoverMarker = -1, hoverTrim = 0, hoverSlice = -1, selectedSlice = -1;
    int auditionNote = -1;
    size_t lastNumPoints = 0;
    bool dragHover = false;
    float time = 0.0f;
    std::array<float, octo::maxSlices> glow {};
    std::array<juce::uint32, octo::maxSlices> lastTrig {};
};
