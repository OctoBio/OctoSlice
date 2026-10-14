#include "WaveformView.h"
#include "OctoLookAndFeel.h"
#include <cmath>

using namespace octo;

WaveformView::WaveformView (OctoSliceProcessor& p) : proc (p)
{
    for (size_t i = 0; i < lastTrig.size(); ++i)
        lastTrig[i] = proc.sliceTriggers[i].load();
    snap = proc.getSnapshot();
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (30.0f);
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

int WaveformView::markerAt (float x) const
{
    if (snap == nullptr || snap->sample == nullptr) return -1;
    int best = -1; float bestD = 7.0f;
    for (int i = 1; i < snap->numSlices(); ++i)
    {
        const float d = std::abs (sampleToX (snap->sliceStart (i)) - x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int WaveformView::sliceAt (float x) const
{
    if (snap == nullptr || snap->sample == nullptr) return -1;
    const double s = xToSample (x);
    if (s < 0 || s >= snap->totalSamples()) return -1;
    for (int i = snap->numSlices() - 1; i >= 0; --i)
        if (s >= snap->sliceStart (i)) return i;
    return 0;
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
void WaveformView::rebuildCache()
{
    const auto a = waveArea();
    const int w = juce::jmax (1, (int) a.getWidth());
    colMin.assign ((size_t) w, 0.0f);
    colMax.assign ((size_t) w, 0.0f);
    cacheValid = true;
    if (snap == nullptr || snap->sample == nullptr) return;

    const auto& audio = snap->sample->audio;
    const int len = audio.getNumSamples(), chans = audio.getNumChannels();
    float peak = 0.0f;
    for (int c = 0; c < chans; ++c) peak = juce::jmax (peak, audio.getMagnitude (c, 0, len));
    const float norm = peak > 0.0001f ? 0.92f / peak : 1.0f;

    const double span = viewEnd - viewStart;
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
        colMin[(size_t) col] = mn * norm;
        colMax[(size_t) col] = mx * norm;
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
        cacheValid = false;
        selected = -1;
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

    // planet
    const float pr = 26.0f + 3.0f * pulse + 6.0f * hot;
    juce::ColourGradient body (colours::violet.withAlpha (0.9f), c.x - pr * 0.4f, c.y - pr * 0.5f,
                               juce::Colour (0xff120a30), c.x + pr, c.y + pr, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - pr, c.y - pr, pr * 2.0f, pr * 2.0f);
    g.setColour (colours::cyan.withAlpha (0.25f + 0.3f * hot));
    g.drawEllipse (c.x - pr - 4.0f, c.y - pr - 4.0f, (pr + 4.0f) * 2.0f, (pr + 4.0f) * 2.0f, 1.0f);

    // orbit rings with moving satellites
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

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    float total = 0.0f;
    for (auto gl : glow) total = juce::jmax (total, gl);
    drawGlassPanel (g, bounds, 16.0f, dragHover ? 1.0f : 0.25f * total);

    if (snap == nullptr || snap->sample == nullptr)
    {
        drawEmpty (g, bounds);
        return;
    }

    if (! cacheValid) rebuildCache();

    const auto a = waveArea();
    const int n = snap->numSlices();
    const int root = (int) proc.apvts.getRawParameterValue ("root")->load();
    const float midY = a.getCentreY(), halfH = a.getHeight() * 0.5f;

    juce::Graphics::ScopedSaveState clipAll (g);
    g.reduceClipRegion (bounds.reduced (1.0f).toNearestInt());

    // --- slice backgrounds ------------------------------------------------------
    for (int i = 0; i < n; ++i)
    {
        const float x0 = juce::jmax (a.getX(), sampleToX (snap->sliceStart (i)));
        const float x1 = juce::jmin (a.getRight(), sampleToX (snap->sliceEnd (i)));
        if (x1 <= x0) continue;
        const auto col = sliceColour (i, n);
        const auto r = juce::Rectangle<float> (x0, a.getY(), x1 - x0, a.getHeight());
        const float gl = glow[(size_t) juce::jmin (i, maxSlices - 1)];

        if (i % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.018f)); g.fillRect (r); }
        if (i == selected || i == hoverSlice)
        {
            g.setColour (col.withAlpha (i == selected ? 0.09f : 0.045f));
            g.fillRect (r);
        }
        if (gl > 0.01f)
        {
            juce::ColourGradient grad (col.withAlpha (0.0f), r.getX(), r.getY(), col.withAlpha (0.28f * gl), r.getX(), midY, false);
            grad.addColour (1.0, col.withAlpha (0.0f));
            g.setGradientFill (grad);
            g.fillRect (r);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) midY, a.getX(), a.getRight());

    // --- waveform -----------------------------------------------------------------
    juce::Path wave;
    const int w = (int) colMax.size();
    if (w > 1)
    {
        wave.startNewSubPath (a.getX(), midY - colMax[0] * halfH);
        for (int x = 1; x < w; ++x)
            wave.lineTo (a.getX() + (float) x, midY - juce::jmax (colMax[(size_t) x], 0.003f) * halfH);
        for (int x = w - 1; x >= 0; --x)
            wave.lineTo (a.getX() + (float) x, midY - juce::jmin (colMin[(size_t) x], -0.003f) * halfH);
        wave.closeSubPath();
    }

    for (int i = 0; i < n; ++i)
    {
        const float x0 = juce::jmax (a.getX(), sampleToX (snap->sliceStart (i)));
        const float x1 = juce::jmin (a.getRight(), sampleToX (snap->sliceEnd (i)));
        if (x1 <= x0) continue;
        const float gl = glow[(size_t) juce::jmin (i, maxSlices - 1)];
        const auto col = sliceColour (i, n);

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (x0, a.getY() - 2.0f, x1 - x0, a.getHeight() + 4.0f).toNearestInt());

        const auto edge = col.withAlpha (0.35f + 0.4f * gl);
        const auto core = col.brighter (0.3f + 0.7f * gl).withAlpha (0.75f + 0.25f * gl);
        juce::ColourGradient grad (edge, 0.0f, midY - halfH, edge, 0.0f, midY + halfH, false);
        grad.addColour (0.5, core);
        g.setGradientFill (grad);
        g.fillPath (wave);

        if (gl > 0.05f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f * gl));
            g.strokePath (wave, juce::PathStrokeType (1.0f));
        }
    }

    // --- markers --------------------------------------------------------------------
    for (int i = 1; i < n; ++i)
    {
        const float x = sampleToX (snap->sliceStart (i));
        if (x < a.getX() - 6.0f || x > a.getRight() + 6.0f) continue;
        const bool hot = (i == hoverMarker || i == dragMarker);

        if (hot)
        {
            g.setColour (colours::cyan.withAlpha (0.25f));
            g.fillRect (x - 3.0f, a.getY(), 6.0f, a.getHeight());
        }
        g.setColour (hot ? juce::Colours::white : juce::Colours::white.withAlpha (0.32f));
        g.drawLine (x, a.getY() - 6.0f, x, a.getBottom(), hot ? 1.5f : 1.0f);

        juce::Path diamond;
        const float d = hot ? 6.0f : 4.5f, hy = a.getY() - 11.0f;
        diamond.addQuadrilateral (x, hy - d, x + d, hy, x, hy + d, x - d, hy);
        g.setColour (hot ? colours::cyan : sliceColour (i, n).withAlpha (0.9f));
        g.fillPath (diamond);
    }

    // --- note labels ------------------------------------------------------------------
    g.setFont (uiFont (10.0f, true, 0.1f));
    for (int i = 0; i < n; ++i)
    {
        const float x0 = sampleToX (snap->sliceStart (i)), x1 = sampleToX (snap->sliceEnd (i));
        if (x1 - x0 < 22.0f || x1 < a.getX() || x0 > a.getRight()) continue;
        const float gl = glow[(size_t) juce::jmin (i, maxSlices - 1)];
        const int note = root + i;
        const auto label = note <= 127 ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::String ("--");
        g.setColour (sliceColour (i, n).interpolatedWith (juce::Colours::white, gl).withAlpha (0.55f + 0.45f * juce::jmax (gl, i == selected ? 1.0f : 0.0f)));
        g.drawText (label, juce::Rectangle<float> (x0, a.getBottom() + 6.0f, x1 - x0, 16.0f), juce::Justification::centred);
    }

    // --- playheads ----------------------------------------------------------------------
    const double totalLen = snap->totalSamples();
    for (int i = 0; i < OctoSliceProcessor::numVoices; ++i)
    {
        const float pos = proc.voicePos[(size_t) i].load();
        if (pos < 0.0f) continue;
        const float x = sampleToX (pos * totalLen);
        if (x < a.getX() || x > a.getRight()) continue;
        const float lvl = juce::jlimit (0.2f, 1.0f, proc.voiceLevel[(size_t) i].load());
        g.setColour (juce::Colours::white.withAlpha (0.08f * lvl)); g.fillRect (x - 6.0f, a.getY(), 12.0f, a.getHeight());
        g.setColour (colours::cyan.withAlpha (0.35f * lvl));        g.fillRect (x - 2.0f, a.getY(), 4.0f, a.getHeight());
        g.setColour (juce::Colours::white.withAlpha (lvl));          g.drawLine (x, a.getY(), x, a.getBottom(), 1.3f);
        g.fillEllipse (x - 3.0f, a.getY() - 3.0f, 6.0f, 6.0f);
    }

    // --- zoom overview --------------------------------------------------------------------
    if (viewEnd - viewStart < totalLen - 1.0)
    {
        const auto track = juce::Rectangle<float> (a.getX(), bounds.getBottom() - 6.0f, a.getWidth(), 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (track, 1.2f);
        const float t0 = (float) (viewStart / totalLen), t1 = (float) (viewEnd / totalLen);
        g.setColour (colours::cyan.withAlpha (0.7f));
        g.fillRoundedRectangle (track.withX (track.getX() + track.getWidth() * t0).withWidth (juce::jmax (4.0f, track.getWidth() * (t1 - t0))), 1.2f);
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    hoverMarker = markerAt ((float) e.x);
    hoverSlice = hoverMarker < 0 ? sliceAt ((float) e.x) : -1;
    if (snap == nullptr || snap->sample == nullptr)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (hoverMarker >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    hoverMarker = hoverSlice = -1;
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

    const int m = markerAt ((float) e.x);
    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        if (m >= 1) { proc.removeMarker (m); hoverMarker = -1; }
        return;
    }

    if (m >= 1) { dragMarker = m; return; }

    const int s = sliceAt ((float) e.x);
    if (s >= 0)
    {
        selected = s;
        const int note = (int) proc.apvts.getRawParameterValue ("root")->load() + s;
        if (note <= 127)
        {
            stopAudition();
            proc.keyboardState.noteOn (1, note, 0.85f);
            auditionNote = note;
        }
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker >= 1)
        proc.moveMarker (dragMarker, (int) std::lround (xToSample ((float) e.x)));
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    dragMarker = -1;
    stopAudition();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (snap == nullptr || snap->sample == nullptr || e.mods.isPopupMenu()) return;
    if (markerAt ((float) e.x) >= 0) return;
    stopAudition();
    const double s = xToSample ((float) e.x);
    if (s > 0 && s < snap->totalSamples())
        proc.addMarker ((int) std::lround (s));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (snap == nullptr || snap->sample == nullptr) return;
    const double total = snap->totalSamples();
    double span = viewEnd - viewStart;

    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || e.mods.isShiftDown())
    {
        const double d = (std::abs (wheel.deltaX) > 0.0f ? -wheel.deltaX : -wheel.deltaY) * span * 0.6;
        viewStart = juce::jlimit (0.0, total - span, viewStart + d);
        viewEnd = viewStart + span;
    }
    else
    {
        const double anchor = xToSample ((float) e.x);
        const double factor = std::exp (-wheel.deltaY * 1.6);
        const double newSpan = juce::jlimit (juce::jmin (512.0, total), total, span * factor);
        const double t = (anchor - viewStart) / span;
        viewStart = juce::jlimit (0.0, total - newSpan, anchor - t * newSpan);
        viewEnd = viewStart + newSpan;
    }
    cacheValid = false;
}
