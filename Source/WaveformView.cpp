#include "WaveformView.h"
#include "OctoLookAndFeel.h"
#include <cmath>

using namespace octo;

static constexpr float handleStrip = 22.0f;   // space above the waveform for marker handles
static constexpr float grabRadius = 7.0f;

WaveformView::WaveformView (OctoSliceProcessor& p) : proc (p)
{
    for (size_t i = 0; i < lastTrig.size(); ++i)
        lastTrig[i] = proc.sliceTriggers[i].load();
    snap = proc.getSnapshot();
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton, &trimButton })
    {
        addChildComponent (*b);
        b->setWantsKeyboardFocus (false);
        b->setBufferedToImage (true);
    }
    zoomOutButton.setTooltip ("Zoom out  ( - )");
    zoomInButton.setTooltip ("Zoom in  ( + )");
    fitButton.setTooltip ("Show the whole sample  ( 0 )");
    trimButton.setTooltip ("Cut the silence at the start and the end of the sample");
    zoomOutButton.onClick = [this] { zoomBy (2.0); };
    zoomInButton.onClick  = [this] { zoomBy (0.5); };
    fitButton.onClick     = [this] { zoomToFit(); };
    trimButton.onClick    = [this]
    {
        if (snap != nullptr && snap->isTrimmed()) proc.resetTrim();
        else proc.autoTrim();
        baseDirty = true;
    };
}

void WaveformView::resized()
{
    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (8).withHeight (26);
    trimButton.setBounds (r.removeFromRight (92));
    r.removeFromRight (6);
    fitButton.setBounds (r.removeFromRight (46));
    r.removeFromRight (6);
    zoomInButton.setBounds (r.removeFromRight (30));
    r.removeFromRight (4);
    zoomOutButton.setBounds (r.removeFromRight (30));
    columnsValid = false;
    baseDirty = true;
}

juce::Rectangle<float> WaveformView::overviewArea() const
{
    return { 14.0f, 10.0f, (float) zoomOutButton.getX() - 14.0f - 12.0f, 22.0f };
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (42.0f + handleStrip).withTrimmedBottom (30.0f);
}

double WaveformView::xToSample (float x) const
{
    const auto a = waveArea();
    return viewStart + (double) ((x - a.getX()) / a.getWidth()) * (viewEnd - viewStart);
}

float WaveformView::sampleToX (double s) const
{
    const auto a = waveArea();
    return a.getX() + (float) ((s - viewStart) / (viewEnd - viewStart)) * a.getWidth();
}

double WaveformView::getZoomFactor() const
{
    if (snap == nullptr || snap->sample == nullptr) return 1.0;
    return snap->totalSamples() / juce::jmax (1.0, viewEnd - viewStart);
}

int WaveformView::markerAt (float x) const
{
    if (snap == nullptr || snap->sample == nullptr) return -1;
    int best = -1; float bestD = grabRadius;
    for (int i = 1; i < snap->numSlices(); ++i)
    {
        const float d = std::abs (sampleToX (snap->sliceStart (i)) - x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int WaveformView::trimHandleAt (float x) const
{
    if (snap == nullptr || snap->sample == nullptr) return 0;
    const float ds = std::abs (sampleToX (snap->regionStart()) - x);
    const float de = std::abs (sampleToX (snap->regionEnd()) - x);
    if (ds < grabRadius && ds <= de) return 1;
    if (de < grabRadius) return 2;
    return 0;
}

int WaveformView::sliceAt (float x) const
{
    if (snap == nullptr || snap->sample == nullptr) return -1;
    const double s = xToSample (x);
    if (s < snap->regionStart() || s >= snap->regionEnd()) return -1;
    for (int i = snap->numSlices() - 1; i >= 0; --i)
        if (s >= snap->sliceStart (i)) return i;
    return 0;
}

bool WaveformView::isSelected (int marker) const
{
    return std::find (selection.begin(), selection.end(), marker) != selection.end();
}

juce::Point<float> WaveformView::getSliceCentre (int slice) const
{
    const auto a = waveArea();
    if (snap == nullptr || snap->sample == nullptr || slice >= snap->numSlices())
        return a.getCentre();
    const float x = sampleToX (0.5 * (snap->sliceStart (slice) + snap->sliceEnd (slice)));
    return { juce::jlimit (a.getX(), a.getRight(), x), a.getCentreY() };
}

//==============================================================================
void WaveformView::setView (double start, double span)
{
    if (snap == nullptr || snap->sample == nullptr) return;
    const double total = snap->totalSamples();
    span = juce::jlimit (juce::jmin (64.0, total), total, span);
    start = juce::jlimit (0.0, total - span, start);
    if (std::abs (start - viewStart) > 1.0e-6 || std::abs (start + span - viewEnd) > 1.0e-6)
    {
        viewStart = start;
        viewEnd = start + span;
        columnsValid = false;
        baseDirty = true;
    }
}

void WaveformView::zoomBy (double factor, double anchor)
{
    const double span = viewEnd - viewStart;
    if (anchor < 0.0) anchor = viewStart + span * 0.5;
    const double t = (anchor - viewStart) / span;
    const double newSpan = span * factor;
    setView (anchor - t * newSpan, newSpan);
}

void WaveformView::zoomToFit()
{
    if (snap == nullptr || snap->sample == nullptr) return;
    setView (0.0, snap->totalSamples());
}

//==============================================================================
void WaveformView::rebuildColumns()
{
    const auto a = waveArea();
    const int w = juce::jmax (1, (int) a.getWidth());
    colMin.assign ((size_t) w, 0.0f);
    colMax.assign ((size_t) w, 0.0f);
    columnsValid = true;
    wavePath.clear();
    if (snap == nullptr || snap->sample == nullptr) return;

    const auto& audio = snap->sample->audio;
    const int len = audio.getNumSamples(), chans = audio.getNumChannels();
    float peak = 0.0f;
    for (int c = 0; c < chans; ++c) peak = juce::jmax (peak, audio.getMagnitude (c, 0, len));
    const float norm = peak > 0.0001f ? 0.92f / peak : 1.0f;

    const double span = viewEnd - viewStart;
    const double spp = span / w;

    if (spp < 1.5)
    {
        // very zoomed in: follow the actual sample values (interpolated)
        auto valueAt = [&] (double pos)
        {
            const int i = juce::jlimit (0, len - 1, (int) pos);
            const int j = juce::jmin (len - 1, i + 1);
            const float t = (float) (pos - i);
            float v = 0.0f;
            for (int c = 0; c < chans; ++c)
                v += audio.getSample (c, i) * (1.0f - t) + audio.getSample (c, j) * t;
            return v / (float) chans * norm;
        };
        float prev = valueAt (viewStart);
        for (int col = 0; col < w; ++col)
        {
            const float v = valueAt (viewStart + spp * (col + 0.5));
            colMin[(size_t) col] = juce::jmin (v, prev) - 0.012f;
            colMax[(size_t) col] = juce::jmax (v, prev) + 0.012f;
            prev = v;
        }
    }
    else
    {
        for (int col = 0; col < w; ++col)
        {
            const int s0 = juce::jlimit (0, len, (int) (viewStart + span * col / w));
            const int s1 = juce::jlimit (s0, len, juce::jmax (s0 + 1, (int) (viewStart + span * (col + 1) / w)));
            const int stride = juce::jmax (1, (s1 - s0) / 1500);
            float mn = 0.0f, mx = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                const float* d = audio.getReadPointer (c);
                for (int i = s0; i < s1; i += stride)
                {
                    mn = juce::jmin (mn, d[i]);
                    mx = juce::jmax (mx, d[i]);
                }
            }
            colMin[(size_t) col] = juce::jmin (mn * norm, -0.003f);
            colMax[(size_t) col] = juce::jmax (mx * norm, 0.003f);
        }
    }

    const float midY = a.getCentreY(), halfH = a.getHeight() * 0.5f;
    wavePath.startNewSubPath (a.getX(), midY - colMax[0] * halfH);
    for (int x = 1; x < w; ++x)
        wavePath.lineTo (a.getX() + (float) x, midY - colMax[(size_t) x] * halfH);
    for (int x = w - 1; x >= 0; --x)
        wavePath.lineTo (a.getX() + (float) x, midY - colMin[(size_t) x] * halfH);
    wavePath.closeSubPath();
}

void WaveformView::rebuildOverview()
{
    const auto o = overviewArea();
    const int w = juce::jmax (1, (int) o.getWidth());
    ovMin.assign ((size_t) w, 0.0f);
    ovMax.assign ((size_t) w, 0.0f);
    if (snap == nullptr || snap->sample == nullptr) return;

    const auto& audio = snap->sample->audio;
    const int len = audio.getNumSamples();
    float peak = 0.0f;
    for (int c = 0; c < audio.getNumChannels(); ++c) peak = juce::jmax (peak, audio.getMagnitude (c, 0, len));
    const float norm = peak > 0.0001f ? 1.0f / peak : 1.0f;
    for (int col = 0; col < w; ++col)
    {
        const int s0 = (int) ((double) len * col / w), s1 = juce::jmax (s0 + 1, (int) ((double) len * (col + 1) / w));
        const int stride = juce::jmax (1, (s1 - s0) / 400);
        float mn = 0.0f, mx = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = s0; i < juce::jmin (s1, len); i += stride)
            {
                mn = juce::jmin (mn, audio.getSample (c, i));
                mx = juce::jmax (mx, audio.getSample (c, i));
            }
        ovMin[(size_t) col] = mn * norm;
        ovMax[(size_t) col] = mx * norm;
    }
}

//==============================================================================
void WaveformView::tick (float dt)
{
    time += dt;
    snap = proc.getSnapshot();

    if (snap->sample.get() != cachedSample)
    {
        cachedSample = snap->sample.get();
        viewStart = 0.0;
        viewEnd = juce::jmax (1, snap->totalSamples());
        columnsValid = false;
        baseDirty = true;
        selectedSlice = -1;
        selection.clear();
        rebuildOverview();
        for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton, &trimButton })
            b->setVisible (snap->sample != nullptr);
    }

    if (snap->points.size() != lastNumPoints)
    {
        lastNumPoints = snap->points.size();
        if (drag != Drag::markers) selection.clear();
        if (selectedSlice >= snap->numSlices()) selectedSlice = -1;
        baseDirty = true;
    }

    const juce::String trimText = snap->isTrimmed() ? "UNTRIM" : "AUTO TRIM";
    if (trimButton.getButtonText() != trimText) trimButton.setButtonText (trimText);

    // keep scrolling while dragging outside the view
    if (snap->sample != nullptr && (drag == Drag::markers || drag == Drag::trimStart || drag == Drag::trimEnd || drag == Drag::lasso))
    {
        const auto a = waveArea();
        float over = 0.0f;
        if (lastMouseX < a.getX()) over = lastMouseX - a.getX();
        else if (lastMouseX > a.getRight()) over = lastMouseX - a.getRight();
        if (std::abs (over) > 0.0001f)
        {
            const double span = viewEnd - viewStart;
            const double shift = span * (double) juce::jlimit (-3.0f, 3.0f, over / 40.0f) * dt;
            const double oldStart = viewStart;
            setView (viewStart + shift, span);
            if (drag == Drag::lasso) lassoX0 -= (float) ((viewStart - oldStart) / span) * a.getWidth();
            applyDrag (lastMouseX, lastShift);
        }
    }

    const float decay = std::exp (-dt * 4.5f);
    for (auto& gl : glow) gl *= decay;

    for (int i = 0; i < OctoSliceProcessor::numVoices; ++i)
    {
        const int s = proc.voiceSlice[(size_t) i].load();
        if (s >= 0 && s < maxSlices)
            glow[(size_t) s] = juce::jmax (glow[(size_t) s], 0.35f + 0.65f * proc.voiceLevel[(size_t) i].load());
    }

    for (int i = 0; i < maxSlices; ++i)
    {
        const auto t = proc.sliceTriggers[(size_t) i].load();
        if (t != lastTrig[(size_t) i])
        {
            lastTrig[(size_t) i] = t;
            glow[(size_t) i] = 1.0f;
        }
    }
}

//==============================================================================
void WaveformView::drawEmpty (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre().translated (0.0f, -14.0f);
    const float pulse = 0.5f + 0.5f * std::sin (time * 2.0f);
    const float hot = dragHover ? 1.0f : 0.0f;

    const float pr = 26.0f + 3.0f * pulse + 6.0f * hot;
    juce::ColourGradient body (colours::violet.withAlpha (0.9f), c.x - pr * 0.4f, c.y - pr * 0.5f,
                               juce::Colour (0xff120a30), c.x + pr, c.y + pr, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - pr, c.y - pr, pr * 2.0f, pr * 2.0f);
    g.setColour (colours::cyan.withAlpha (0.25f + 0.3f * hot));
    g.drawEllipse (c.x - pr - 4.0f, c.y - pr - 4.0f, (pr + 4.0f) * 2.0f, (pr + 4.0f) * 2.0f, 1.0f);

    for (int k = 0; k < 3; ++k)
    {
        const float rx = 70.0f + 34.0f * (float) k, ry = rx * 0.32f;
        const float tilt = -0.18f + 0.12f * (float) k;
        const auto tf = juce::AffineTransform::rotation (tilt, c.x, c.y);
        juce::Path orbit;
        orbit.addEllipse (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 6.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, orbit, dashes, 2, tf);
        g.setColour ((k == 1 ? colours::cyan : colours::violet).withAlpha (0.25f + 0.25f * hot));
        g.fillPath (dashed);

        const float a = time * (0.8f - 0.2f * (float) k) + (float) k * 2.1f;
        auto sat = juce::Point<float> (c.x + rx * std::cos (a), c.y + ry * std::sin (a)).transformedBy (tf);
        g.setColour (colours::cyan.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (sat));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (sat));
    }

    g.setColour (colours::text.withAlpha (0.85f + 0.15f * hot));
    g.setFont (uiFont (17.0f, true, 0.35f));
    g.drawText (dragHover ? "RELEASE TO LOAD" : "DROP A SAMPLE HERE",
                r.withY (c.y + 70.0f).withHeight (24.0f), juce::Justification::centred);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f, false, 0.2f));
    g.drawText ("or click to browse  -  WAV  AIFF  FLAC  MP3  OGG",
                r.withY (c.y + 96.0f).withHeight (18.0f), juce::Justification::centred);
}

void WaveformView::renderBase (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawGlassPanel (g, bounds, 16.0f, dragHover ? 1.0f : 0.0f);
    if (snap == nullptr || snap->sample == nullptr) return;

    const auto a = waveArea();
    const int n = snap->numSlices();
    const int root = baseRoot;
    const float midY = a.getCentreY();
    const float xrs = juce::jlimit (a.getX(), a.getRight(), sampleToX (snap->regionStart()));
    const float xre = juce::jlimit (a.getX(), a.getRight(), sampleToX (snap->regionEnd()));
    const auto fullH = juce::Rectangle<float> (0.0f, a.getY() - handleStrip, 0.0f, a.getHeight() + handleStrip);

    juce::Graphics::ScopedSaveState clipAll (g);
    g.reduceClipRegion (bounds.reduced (1.0f).toNearestInt());

    // --- slice backgrounds ------------------------------------------------------
    for (int i = 0; i < n; ++i)
    {
        const float x0 = juce::jmax (a.getX(), sampleToX (snap->sliceStart (i)));
        const float x1 = juce::jmin (a.getRight(), sampleToX (snap->sliceEnd (i)));
        if (x1 <= x0) continue;
        const auto r = juce::Rectangle<float> (x0, a.getY(), x1 - x0, a.getHeight());
        if (i % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.018f)); g.fillRect (r); }
        if (i == selectedSlice || i == hoverSlice)
        {
            g.setColour (sliceColour (i, n).withAlpha (i == selectedSlice ? 0.09f : 0.045f));
            g.fillRect (r);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) midY, a.getX(), a.getRight());

    // --- waveform: trimmed-out parts in grey, slices in colour ---------------------
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::RectangleList<int> outside;
        if (xrs > a.getX()) outside.add (juce::Rectangle<float> (a.getX(), a.getY() - 2.0f, xrs - a.getX(), a.getHeight() + 4.0f).toNearestInt());
        if (xre < a.getRight()) outside.add (juce::Rectangle<float> (xre, a.getY() - 2.0f, a.getRight() - xre, a.getHeight() + 4.0f).toNearestInt());
        if (! outside.isEmpty())
        {
            g.reduceClipRegion (outside);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.fillPath (wavePath);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const float x0 = juce::jmax (a.getX(), sampleToX (snap->sliceStart (i)));
        const float x1 = juce::jmin (a.getRight(), sampleToX (snap->sliceEnd (i)));
        if (x1 <= x0) continue;
        const auto col = sliceColour (i, n);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (x0, a.getY() - 2.0f, x1 - x0, a.getHeight() + 4.0f).toNearestInt());
        const float halfH = a.getHeight() * 0.5f;
        juce::ColourGradient grad (col.withAlpha (0.35f), 0.0f, midY - halfH, col.withAlpha (0.35f), 0.0f, midY + halfH, false);
        grad.addColour (0.5, col.brighter (0.3f).withAlpha (0.78f));
        g.setGradientFill (grad);
        g.fillPath (wavePath);
    }

    // --- trimmed-out shade ------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    if (xrs > a.getX()) g.fillRect (fullH.withX (a.getX()).withWidth (xrs - a.getX()));
    if (xre < a.getRight()) g.fillRect (fullH.withX (xre).withWidth (a.getRight() - xre));

    // --- markers --------------------------------------------------------------------
    for (int i = 1; i < n; ++i)
    {
        const float x = sampleToX (snap->sliceStart (i));
        if (x < a.getX() - 6.0f || x > a.getRight() + 6.0f) continue;
        const bool sel = isSelected (i);
        const bool hot = (i == hoverMarker);

        if (sel)
        {
            g.setColour (colours::cyan.withAlpha (0.16f));
            g.fillRect (x - 4.0f, a.getY(), 8.0f, a.getHeight());
        }
        g.setColour (sel ? colours::cyan : hot ? juce::Colours::white : juce::Colours::white.withAlpha (0.32f));
        g.drawLine (x, a.getY() - 8.0f, x, a.getBottom(), sel || hot ? 1.5f : 1.0f);

        juce::Path diamond;
        const float d = sel || hot ? 6.0f : 4.5f, hy = a.getY() - 12.0f;
        diamond.addQuadrilateral (x, hy - d, x + d, hy, x, hy + d, x - d, hy);
        if (sel)
        {
            g.setColour (colours::cyan.withAlpha (0.35f));
            g.fillEllipse (x - 9.0f, hy - 9.0f, 18.0f, 18.0f);
        }
        g.setColour (sel ? juce::Colours::white : hot ? colours::cyan : sliceColour (i, n).withAlpha (0.9f));
        g.fillPath (diamond);
    }

    // --- trim brackets ----------------------------------------------------------------
    for (int k = 0; k < 2; ++k)
    {
        const double pos = k == 0 ? snap->regionStart() : snap->regionEnd();
        const float x = sampleToX (pos);
        if (x < a.getX() - 2.0f || x > a.getRight() + 2.0f) continue;
        const bool hot = hoverTrim == k + 1 || (k == 0 ? drag == Drag::trimStart : drag == Drag::trimEnd);
        const float dir = k == 0 ? 1.0f : -1.0f;
        const auto col = colours::pink.withAlpha (hot ? 1.0f : 0.75f);

        if (hot)
        {
            g.setColour (colours::pink.withAlpha (0.15f));
            g.fillRect (k == 0 ? x : x - 6.0f, a.getY() - handleStrip, 6.0f, a.getHeight() + handleStrip);
        }
        g.setColour (col);
        g.drawLine (x, a.getY() - handleStrip + 2.0f, x, a.getBottom(), hot ? 2.0f : 1.5f);
        g.drawLine (x, a.getY() - handleStrip + 2.0f, x + dir * 9.0f, a.getY() - handleStrip + 2.0f, 2.0f);
        g.drawLine (x, a.getBottom(), x + dir * 9.0f, a.getBottom(), 2.0f);

        g.setFont (uiFont (9.0f, true, 0.2f));
        const auto label = juce::Rectangle<float> (k == 0 ? x + 4.0f : x - 44.0f, a.getY() - handleStrip + 5.0f, 40.0f, 12.0f);
        g.setColour (colours::pink.withAlpha (hot ? 1.0f : 0.6f));
        g.drawText (k == 0 ? "START" : "END", label, k == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    // --- note labels ------------------------------------------------------------------
    g.setFont (uiFont (10.0f, true, 0.1f));
    for (int i = 0; i < n; ++i)
    {
        const float x0 = sampleToX (snap->sliceStart (i)), x1 = sampleToX (snap->sliceEnd (i));
        if (x1 - x0 < 22.0f || x1 < a.getX() || x0 > a.getRight()) continue;
        const int note = root + i;
        const auto label = note <= 127 ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::String ("--");
        g.setColour (sliceColour (i, n).withAlpha (i == selectedSlice ? 1.0f : 0.6f));
        const float cx0 = juce::jmax (x0, a.getX()), cx1 = juce::jmin (x1, a.getRight());
        g.drawText (label, juce::Rectangle<float> (cx0, a.getBottom() + 6.0f, juce::jmax (22.0f, cx1 - cx0), 16.0f), juce::Justification::centred);
    }

    // --- overview strip ------------------------------------------------------------------
    {
        const auto o = overviewArea();
        const double total = snap->totalSamples();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (o, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawRoundedRectangle (o, 5.0f, 1.0f);

        const int w = (int) ovMax.size();
        const float my = o.getCentreY(), hh = o.getHeight() * 0.42f;
        const float ors = o.getX() + (float) (snap->regionStart() / total) * o.getWidth();
        const float ore = o.getX() + (float) (snap->regionEnd() / total) * o.getWidth();
        for (int x = 0; x < w; ++x)
        {
            const float px = o.getX() + (float) x;
            const bool inside = px >= ors && px <= ore;
            g.setColour (inside ? colours::cyan.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.15f));
            g.drawVerticalLine ((int) px, my - ovMax[(size_t) x] * hh, my - ovMin[(size_t) x] * hh + 1.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        for (int i = 1; i < n; ++i)
        {
            const float px = o.getX() + (float) (snap->sliceStart (i) / total) * o.getWidth();
            g.drawVerticalLine ((int) px, o.getY() + 3.0f, o.getY() + 6.0f);
        }

        const float vx0 = o.getX() + (float) (viewStart / total) * o.getWidth();
        const float vx1 = o.getX() + (float) (viewEnd / total) * o.getWidth();
        auto vp = juce::Rectangle<float> (vx0, o.getY(), juce::jmax (6.0f, vx1 - vx0), o.getHeight());
        g.setColour (colours::cyan.withAlpha (0.12f));
        g.fillRoundedRectangle (vp, 4.0f);
        g.setColour (colours::cyan.withAlpha (drag == Drag::overview ? 1.0f : 0.7f));
        g.drawRoundedRectangle (vp, 4.0f, 1.2f);

        const double z = getZoomFactor();
        if (z > 1.05)
        {
            g.setFont (uiFont (9.5f, true, 0.1f));
            g.setColour (colours::text.withAlpha (0.8f));
            const auto txt = "x" + juce::String (z, z < 10.0 ? 1 : 0);
            g.drawText (txt, o.reduced (6.0f, 0.0f), juce::Justification::centredRight);
        }
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (snap == nullptr || snap->sample == nullptr)
    {
        drawGlassPanel (g, bounds, 16.0f, dragHover ? 1.0f : 0.0f);
        drawEmpty (g, bounds);
        return;
    }

    if (! columnsValid) { rebuildColumns(); baseDirty = true; }

    const float scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int root = (int) proc.apvts.getRawParameterValue ("root")->load();
    if (baseDirty || baseSnap != snap.get() || std::abs (scale - baseScale) > 0.001f || root != baseRoot
        || baseImage.getWidth() != juce::roundToInt ((float) getWidth() * scale))
    {
        baseSnap = snap.get();
        baseScale = scale;
        baseRoot = root;
        baseImage = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale),
                                 juce::roundToInt ((float) getHeight() * scale), true);
        juce::Graphics ig (baseImage);
        ig.addTransform (juce::AffineTransform::scale (scale));
        renderBase (ig);
        baseDirty = false;
    }
    g.drawImage (baseImage, bounds);

    const auto a = waveArea();
    const int n = snap->numSlices();
    const float midY = a.getCentreY(), halfH = a.getHeight() * 0.5f;

    float maxGlow = 0.0f;
    for (int i = 0; i < juce::jmin (n, maxSlices); ++i) maxGlow = juce::jmax (maxGlow, glow[(size_t) i]);
    if (maxGlow > 0.02f)
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (colours::cyan.withAlpha (0.05f * maxGlow / (float) i));
            g.drawRoundedRectangle (bounds.reduced (1.0f).expanded ((float) i * 2.5f - 2.5f), 16.0f, 2.5f);
        }

    juce::Graphics::ScopedSaveState clipAll (g);
    g.reduceClipRegion (bounds.reduced (1.0f).toNearestInt());

    // --- live overlays: glowing slices --------------------------------------------------
    for (int i = 0; i < juce::jmin (n, maxSlices); ++i)
    {
        const float gl = glow[(size_t) i];
        if (gl < 0.02f) continue;
        const float x0 = juce::jmax (a.getX(), sampleToX (snap->sliceStart (i)));
        const float x1 = juce::jmin (a.getRight(), sampleToX (snap->sliceEnd (i)));
        if (x1 <= x0) continue;
        const auto col = sliceColour (i, n);
        const auto r = juce::Rectangle<float> (x0, a.getY(), x1 - x0, a.getHeight());

        juce::ColourGradient bg (col.withAlpha (0.0f), r.getX(), r.getY(), col.withAlpha (0.26f * gl), r.getX(), midY, false);
        bg.addColour (1.0, col.withAlpha (0.0f));
        g.setGradientFill (bg);
        g.fillRect (r);

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.expanded (0.0f, 2.0f).toNearestInt());
        juce::ColourGradient wg (col.withAlpha (0.3f * gl), 0.0f, midY - halfH, col.withAlpha (0.3f * gl), 0.0f, midY + halfH, false);
        wg.addColour (0.5, col.brighter (1.0f).withAlpha (0.65f * gl));
        g.setGradientFill (wg);
        g.fillPath (wavePath);
        g.setColour (juce::Colours::white.withAlpha (0.35f * gl));
        g.strokePath (wavePath, juce::PathStrokeType (1.0f));
    }

    // --- playheads ------------------------------------------------------------------------
    const double totalLen = snap->totalSamples();
    const auto o = overviewArea();
    for (int i = 0; i < OctoSliceProcessor::numVoices; ++i)
    {
        const float pos = proc.voicePos[(size_t) i].load();
        if (pos < 0.0f) continue;
        const float lvl = juce::jlimit (0.2f, 1.0f, proc.voiceLevel[(size_t) i].load());

        const float ox = o.getX() + pos * o.getWidth();
        g.setColour (juce::Colours::white.withAlpha (lvl));
        g.drawVerticalLine ((int) ox, o.getY() + 2.0f, o.getBottom() - 2.0f);

        const float x = sampleToX (pos * totalLen);
        if (x < a.getX() || x > a.getRight()) continue;
        g.setColour (juce::Colours::white.withAlpha (0.08f * lvl)); g.fillRect (x - 6.0f, a.getY(), 12.0f, a.getHeight());
        g.setColour (colours::cyan.withAlpha (0.35f * lvl));        g.fillRect (x - 2.0f, a.getY(), 4.0f, a.getHeight());
        g.setColour (juce::Colours::white.withAlpha (lvl));          g.drawLine (x, a.getY(), x, a.getBottom(), 1.3f);
        g.fillEllipse (x - 3.0f, a.getY() - 3.0f, 6.0f, 6.0f);
    }

    // --- lasso ----------------------------------------------------------------------------
    if (drag == Drag::lasso)
    {
        const float x0 = juce::jmin (lassoX0, lassoX1), x1 = juce::jmax (lassoX0, lassoX1);
        const auto r = juce::Rectangle<float> (x0, a.getY() - handleStrip, x1 - x0, a.getHeight() + handleStrip);
        g.setColour (colours::cyan.withAlpha (0.10f));
        g.fillRect (r);
        const float dashes[] = { 4.0f, 3.0f };
        juce::Path p; p.addRectangle (r);
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (colours::cyan.withAlpha (0.8f));
        g.fillPath (dashed);
    }

    // --- position read-out while dragging --------------------------------------------------
    if (drag == Drag::markers || drag == Drag::trimStart || drag == Drag::trimEnd)
    {
        int pos = -1;
        if (drag == Drag::trimStart) pos = snap->regionStart();
        else if (drag == Drag::trimEnd) pos = snap->regionEnd();
        else if (! selection.empty() && selection.front() < n) pos = snap->sliceStart (selection.front());
        if (pos >= 0)
        {
            const float x = juce::jlimit (a.getX() + 40.0f, a.getRight() - 40.0f, sampleToX (pos));
            const auto txt = juce::String (pos / snap->sample->sampleRate * 1000.0, 1) + " ms";
            auto box = juce::Rectangle<float> (74.0f, 18.0f).withCentre ({ x, a.getBottom() - 14.0f });
            g.setColour (juce::Colour (0xe0080a18));
            g.fillRoundedRectangle (box, 9.0f);
            g.setColour (colours::cyan);
            g.drawRoundedRectangle (box, 9.0f, 1.0f);
            g.setFont (uiFont (10.0f, true, 0.05f));
            g.setColour (colours::text);
            g.drawText (txt, box, juce::Justification::centred);
        }
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const int prevM = hoverMarker, prevT = hoverTrim, prevS = hoverSlice;
    hoverMarker = hoverTrim = 0;
    hoverMarker = -1;
    hoverSlice = -1;

    if (snap != nullptr && snap->sample != nullptr && e.y > overviewArea().getBottom() + 4.0f)
    {
        hoverMarker = markerAt ((float) e.x);
        hoverTrim = hoverMarker < 0 ? trimHandleAt ((float) e.x) : 0;
        hoverSlice = (hoverMarker < 0 && hoverTrim == 0) ? sliceAt ((float) e.x) : -1;
    }

    if (prevM != hoverMarker || prevT != hoverTrim || prevS != hoverSlice) baseDirty = true;

    if (snap == nullptr || snap->sample == nullptr)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (overviewArea().contains (e.position))
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (hoverMarker >= 0 || hoverTrim != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoverMarker >= 0 || hoverTrim != 0 || hoverSlice >= 0) baseDirty = true;
    hoverMarker = hoverSlice = -1;
    hoverTrim = 0;
}

void WaveformView::stopAudition()
{
    if (auditionNote >= 0)
    {
        proc.keyboardState.noteOff (1, auditionNote, 0.0f);
        auditionNote = -1;
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (snap == nullptr || snap->sample == nullptr)
    {
        if (onRequestLoad) onRequestLoad();
        return;
    }

    const float x = (float) e.x;
    lastMouseX = x;
    downX = x;
    lastShift = e.mods.isShiftDown() || e.mods.isCommandDown();

    // overview: scroll the view
    if (overviewArea().contains (e.position))
    {
        const auto o = overviewArea();
        const double total = snap->totalSamples(), span = viewEnd - viewStart;
        const double s = (x - o.getX()) / o.getWidth() * total;
        if (s < viewStart || s > viewEnd) setView (s - span * 0.5, span);
        overviewGrab = s - viewStart;
        drag = Drag::overview;
        baseDirty = true;
        return;
    }

    const int m = markerAt (x);

    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        if (m >= 1)
        {
            if (isSelected (m)) proc.removeMarkers (selection);
            else proc.removeMarker (m);
            selection.clear();
            hoverMarker = -1;
            baseDirty = true;
        }
        return;
    }

    if (m >= 1)
    {
        if (lastShift)
        {
            if (isSelected (m)) selection.erase (std::find (selection.begin(), selection.end(), m));
            else selection.push_back (m);
        }
        else if (! isSelected (m))
            selection = { m };

        if (isSelected (m))
        {
            drag = Drag::markers;
            dragBase = snap->points;
            dragStartSample = xToSample (x);
        }
        baseDirty = true;
        return;
    }

    if (const int t = trimHandleAt (x))
    {
        drag = t == 1 ? Drag::trimStart : Drag::trimEnd;
        baseDirty = true;
        return;
    }

    // empty area: audition the slice, a drag turns into a lasso selection
    drag = Drag::pending;
    const int s = sliceAt (x);
    if (s >= 0)
    {
        selectedSlice = s;
        baseDirty = true;
        const int note = (int) proc.apvts.getRawParameterValue ("root")->load() + s;
        if (note <= 127)
        {
            stopAudition();
            proc.keyboardState.noteOn (1, note, 0.85f);
            auditionNote = note;
        }
    }
}

void WaveformView::applyDrag (float x, bool shift)
{
    if (snap == nullptr || snap->sample == nullptr) return;
    switch (drag)
    {
        case Drag::markers:
            proc.moveMarkers (dragBase, selection, (int) std::lround (xToSample (x) - dragStartSample));
            break;
        case Drag::trimStart:
            proc.setTrim ((int) std::lround (xToSample (x)), snap->regionEnd(), false);
            break;
        case Drag::trimEnd:
            proc.setTrim (snap->regionStart(), (int) std::lround (xToSample (x)), false);
            break;
        case Drag::lasso:
        {
            lassoX1 = x;
            const float x0 = juce::jmin (lassoX0, lassoX1), x1 = juce::jmax (lassoX0, lassoX1);
            selection = shift ? lassoBaseSelection : std::vector<int>();
            for (int i = 1; i < snap->numSlices(); ++i)
            {
                const float mx = sampleToX (snap->sliceStart (i));
                if (mx >= x0 && mx <= x1 && ! isSelected (i)) selection.push_back (i);
            }
            break;
        }
        case Drag::overview:
        {
            const auto o = overviewArea();
            const double s = (x - o.getX()) / o.getWidth() * snap->totalSamples();
            setView (s - overviewGrab, viewEnd - viewStart);
            break;
        }
        case Drag::none:
        case Drag::pending:
            break;
    }
    baseDirty = true;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    const float x = (float) e.x;
    lastMouseX = x;
    lastShift = e.mods.isShiftDown() || e.mods.isCommandDown();

    if (drag == Drag::pending && std::abs (x - downX) > 4.0f)
    {
        stopAudition();
        drag = Drag::lasso;
        lassoX0 = downX;
        lassoBaseSelection = lastShift ? selection : std::vector<int>();
    }
    applyDrag (x, lastShift);
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::trimStart || drag == Drag::trimEnd)
        proc.setTrim (snap->regionStart(), snap->regionEnd(), true);
    if (drag == Drag::pending && ! (e.mods.isShiftDown() || e.mods.isCommandDown()) && ! selection.empty())
        selection.clear();
    if (drag == Drag::markers)
        lastNumPoints = proc.getSnapshot()->points.size();

    drag = Drag::none;
    stopAudition();
    baseDirty = true;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (snap == nullptr || snap->sample == nullptr || e.mods.isPopupMenu()) return;

    if (overviewArea().contains (e.position)) { zoomToFit(); return; }
    if (markerAt ((float) e.x) >= 0 || trimHandleAt ((float) e.x) != 0) return;

    stopAudition();
    const int pos = (int) std::lround (xToSample ((float) e.x));
    if (pos > snap->regionStart() && pos < snap->regionEnd())
    {
        proc.addMarker (pos);
        auto now = proc.getSnapshot();
        selection.clear();
        for (int i = 1; i < now->numSlices(); ++i)
            if (now->points[(size_t) i] == pos) selection = { i };
        snap = now;
        lastNumPoints = now->points.size();
        baseDirty = true;
    }
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (snap == nullptr || snap->sample == nullptr) return;
    const double span = viewEnd - viewStart;

    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || e.mods.isShiftDown())
    {
        const double d = (std::abs (wheel.deltaX) > 0.0f ? -wheel.deltaX : -wheel.deltaY) * span * 0.6;
        setView (viewStart + d, span);
    }
    else
    {
        zoomBy (std::exp (-wheel.deltaY * 1.6), xToSample ((float) e.x));
    }
}

bool WaveformView::keyPressed (const juce::KeyPress& key)
{
    if (snap == nullptr || snap->sample == nullptr) return false;
    const int code = key.getKeyCode();
    const auto ch = key.getTextCharacter();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (selection.empty()) return false;
        proc.removeMarkers (selection);
        selection.clear();
        baseDirty = true;
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { selection.clear(); baseDirty = true; return true; }
    if (key.getModifiers().isCommandDown() && (ch == 'a' || ch == 'A'))
    {
        selection.clear();
        for (int i = 1; i < snap->numSlices(); ++i) selection.push_back (i);
        baseDirty = true;
        return true;
    }
    if (ch == '+' || ch == '=') { zoomBy (0.5); return true; }
    if (ch == '-')              { zoomBy (2.0); return true; }
    if (ch == '0')              { zoomToFit(); return true; }

    if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) && ! selection.empty())
    {
        const double spp = (viewEnd - viewStart) / juce::jmax (1.0f, waveArea().getWidth());
        int step = juce::jmax (1, (int) std::lround (spp));
        if (key.getModifiers().isShiftDown()) step *= 10;
        proc.moveMarkers (snap->points, selection, code == juce::KeyPress::leftKey ? -step : step);
        baseDirty = true;
        return true;
    }
    return false;
}
