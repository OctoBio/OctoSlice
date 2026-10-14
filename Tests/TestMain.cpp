// Offline test runner: checks slicing, playback, state save/restore and renders UI screenshots.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <functional>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::cout << "  [ok]   " << msg << "\n"; \
                              else { std::cout << "  [FAIL] " << msg << "\n"; ++failures; } } while (0)

static juce::AudioBuffer<float> makeDrumLoop (double sr, std::vector<int>& hits)
{
    const int len = (int) (sr * 2.0); // 1 bar @ 120 bpm
    juce::AudioBuffer<float> b (2, len);
    b.clear();
    juce::Random rng (42);
    for (int h = 0; h < 8; ++h)
    {
        const int start = (int) (h * sr * 0.25);
        hits.push_back (start);
        const bool kick = (h % 2 == 0);
        for (int i = 0; i < (int) (sr * 0.2) && start + i < len; ++i)
        {
            const float t = (float) i / (float) sr;
            float s = kick ? std::sin (2.0f * 3.14159f * (55.0f + 90.0f * std::exp (-t * 30.0f)) * t) * std::exp (-t * 12.0f)
                           : (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 25.0f) * 0.7f;
            b.addSample (0, start + i, s * 0.8f);
            b.addSample (1, start + i, s * 0.8f);
        }
    }
    // quiet pad underneath so it's not pure silence between hits
    for (int i = 0; i < len; ++i)
    {
        const float pad = 0.02f * std::sin (2.0f * 3.14159f * 220.0f * (float) i / (float) sr);
        b.addSample (0, i, pad); b.addSample (1, i, pad);
    }
    return b;
}

static void setParam (OctoSliceProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static float renderNote (OctoSliceProcessor& p, int note, int blocks)
{
    juce::AudioBuffer<float> buf (2, 512);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 10);
        if (b == blocks - 1) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        p.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, 512));
    }
    return peak;
}


static double benchmarkFrames (OctoSliceProcessor& proc, juce::Component& ed, std::function<void (float)> advance, int frames)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::Image target (juce::Image::ARGB, ed.getWidth(), ed.getHeight(), true);
    double total = 0.0;
    for (int f = 0; f < frames; ++f)
    {
        juce::MidiBuffer midi;
        if (f % 12 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + (f / 12) % 8, (juce::uint8) 110), 0);
        proc.processBlock (buf, midi);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        advance (1.0f / 60.0f);
        {
            juce::Graphics g (target);
            ed.paintEntireComponent (g, true);
        }
        if (f >= 10) total += juce::Time::getMillisecondCounterHiRes() - t0;
    }
    return total / (frames - 10);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String shotDir = argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp");

    std::cout << "OctoSlice offline tests\n";
    auto proc = std::make_unique<OctoSliceProcessor>();
    proc->prepareToPlay (48000.0, 512);
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);

    std::vector<int> hits;
    const double sr = 44100.0;
    proc->loadAudio (makeDrumLoop (sr, hits), sr, "Test Drum Loop 120", {});

    // --- Grid -----------------------------------------------------------------
    setParam (*proc, "mode", 0); setParam (*proc, "division", 2); setParam (*proc, "bars", 2); setParam (*proc, "zeroX", 0);
    proc->updateSlicesNow();
    CHECK (proc->getSnapshot()->numSlices() == 8, "grid 1/8 over 1 bar -> 8 slices (got " + std::to_string (proc->getSnapshot()->numSlices()) + ")");
    setParam (*proc, "division", 3); proc->updateSlicesNow();
    CHECK (proc->getSnapshot()->numSlices() == 16, "grid 1/16 -> 16 slices");
    setParam (*proc, "bars", 3); proc->updateSlicesNow();
    CHECK (proc->getSnapshot()->numSlices() == 32, "grid 1/16 x 2 bars -> 32 slices");
    setParam (*proc, "bars", 0); proc->hostBpm = 120.0; proc->updateSlicesNow();
    CHECK (proc->getSnapshot()->numSlices() == 16, "grid auto length @120bpm -> 1 bar -> 16 slices");

    // --- Transients -------------------------------------------------------------
    setParam (*proc, "mode", 1); setParam (*proc, "sens", 55); proc->updateSlicesNow();
    {
        auto s = proc->getSnapshot();
        std::cout << "  transient points:";
        for (int p : s->points) std::cout << " " << p;
        std::cout << "\n";
        CHECK (s->numSlices() == 8, "transient detection finds 8 hits (got " + std::to_string (s->numSlices()) + ")");
        int worst = 0;
        for (int h : hits)
        {
            int best = 1 << 30;
            for (int p : s->points) best = juce::jmin (best, std::abs (p - h));
            worst = juce::jmax (worst, best);
        }
        CHECK (worst < (int) (sr * 0.006), "every hit located within 6 ms (worst " + juce::String (worst / sr * 1000.0, 2).toStdString() + " ms)");
    }

    // --- Manual editing ------------------------------------------------------------
    proc->addMarker (30000);
    CHECK ((int) proc->getMode() == 2, "adding a marker switches to manual mode");
    CHECK (proc->getSnapshot()->numSlices() == 9, "manual marker added (9 slices)");
    proc->moveMarker (1, 12000);
    CHECK (proc->getSnapshot()->points[1] == 12000, "marker moved");
    proc->removeMarker (1);
    CHECK (proc->getSnapshot()->numSlices() == 8, "marker removed");


    // --- Trim -----------------------------------------------------------------------------------
    {
        auto tp = std::make_unique<OctoSliceProcessor>();
        tp->prepareToPlay (48000.0, 512);
        std::vector<int> h2;
        auto loop = makeDrumLoop (sr, h2);
        const int lead = (int) (sr * 0.5), tail = (int) (sr * 0.7);
        juce::AudioBuffer<float> padded (2, loop.getNumSamples() + lead + tail);
        padded.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < loop.getNumSamples(); ++i)
                padded.setSample (c, lead + i, (std::abs (loop.getSample (c, i)) > 0.05f || true) ? loop.getSample (c, i) : 0.0f);
        tp->loadAudio (std::move (padded), sr, "Padded loop", {});
        setParam (*tp, "mode", 0); setParam (*tp, "division", 2); setParam (*tp, "bars", 2); setParam (*tp, "zeroX", 0);
        tp->updateSlicesNow();
        tp->autoTrim();
        auto s = tp->getSnapshot();
        CHECK (std::abs (s->regionStart() - lead) < (int) (sr * 0.004), "auto-trim finds the real start (" + juce::String ((s->regionStart() - lead) / sr * 1000.0, 1).toStdString() + " ms off)");
        CHECK (s->regionEnd() > lead + loop.getNumSamples() - (int) (sr * 0.06) && s->regionEnd() < lead + loop.getNumSamples() + (int) (sr * 0.03),
               "auto-trim cuts the silent tail");
        CHECK (s->numSlices() == 8 && s->points.front() == s->regionStart(), "grid slices live inside the trimmed region");
        CHECK (s->sliceEnd (s->numSlices() - 1) == s->regionEnd(), "last slice stops at the END bracket");

        tp->setTrim (s->regionStart() + 1000, s->regionEnd(), false);
        tp->setTrim (s->regionStart() + 1000, s->regionEnd(), true);
        CHECK (tp->getSnapshot()->regionStart() == s->regionStart() + 1000, "START bracket can be dragged");

        juce::MemoryBlock mb; tp->getStateInformation (mb);
        auto other = std::make_unique<OctoSliceProcessor>();
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (other->getSnapshot()->regionStart() == tp->getSnapshot()->regionStart()
               && other->getSnapshot()->regionEnd() == tp->getSnapshot()->regionEnd(), "trim saved with the project");
        tp->resetTrim();
        CHECK (! tp->getSnapshot()->isTrimmed(), "UNTRIM restores the full sample");
    }

    // --- Multi-marker editing ----------------------------------------------------------------------
    {
        setParam (*proc, "mode", 0); setParam (*proc, "division", 2); setParam (*proc, "bars", 2); setParam (*proc, "zeroX", 0);
        proc->updateSlicesNow();
        auto base = proc->getSnapshot()->points;   // 8 slices, 11025 apart
        proc->moveMarkers (base, { 2, 3, 4 }, 3000);
        auto p = proc->getSnapshot()->points;
        CHECK (p[2] == base[2] + 3000 && p[3] == base[3] + 3000 && p[4] == base[4] + 3000 && p[5] == base[5], "3 selected markers move together");
        proc->moveMarkers (base, { 2, 3, 4 }, 100000);
        p = proc->getSnapshot()->points;
        CHECK (p[4] < p[5] && p[5] - p[4] >= 64, "group move is clamped by the next marker (no crossing)");
        proc->removeMarkers ({ 2, 3 });
        CHECK (proc->getSnapshot()->numSlices() == 6, "deleting a selection removes all selected markers");
    }

    // --- Playback -------------------------------------------------------------------
    {
        setParam (*proc, "root", 60);
        const float peak = renderNote (*proc, 61, 20);
        CHECK (peak > 0.05f, "note C#3 plays slice 2 (peak " + juce::String (peak, 3).toStdString() + ")");
        { juce::AudioBuffer<float> flush (2, 512); juce::MidiBuffer none; for (int i = 0; i < 400; ++i) proc->processBlock (flush, none); }
        const float silent = renderNote (*proc, 40, 4);
        CHECK (silent < 1.0e-4f, "notes below root are silent");
        setParam (*proc, "reverse", 1); setParam (*proc, "pitch", 7); setParam (*proc, "space", 40); setParam (*proc, "tone", 2000);
        const float peak2 = renderNote (*proc, 62, 20);
        CHECK (peak2 > 0.001f && std::isfinite (peak2), "reverse + pitch + space + tone still produce clean audio");
        setParam (*proc, "reverse", 0); setParam (*proc, "pitch", 0); setParam (*proc, "space", 0); setParam (*proc, "tone", 20000);
    }

    // --- State round trip -------------------------------------------------------------
    {
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        auto other = std::make_unique<OctoSliceProcessor>();
        other->prepareToPlay (48000.0, 512);
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        auto a = proc->getSnapshot(), b = other->getSnapshot();
        CHECK (b->sample != nullptr && b->totalSamples() == a->totalSamples(), "sample restored from saved state (" + std::to_string (mb.getSize() / 1024) + " KB)");
        CHECK (b->points == a->points, "slice points restored");
        CHECK (other->getMode() == proc->getMode(), "slice mode restored");
    }

    // --- MIDI export ----------------------------------------------------------------------
    {
        auto f = proc->createMidiFile();
        juce::MidiFile mf;
        juce::FileInputStream fis (f);
        CHECK (f.existsAsFile() && mf.readFrom (fis) && mf.getNumTracks() == 1, "MIDI file written: " + f.getFileName().toStdString());
        int notes = 0;
        if (mf.getNumTracks() > 0)
            for (auto* e : *mf.getTrack (0)) if (e->message.isNoteOn()) ++notes;
        CHECK (notes == proc->getSnapshot()->numSlices(), "one MIDI note per slice");
    }

    // --- UI screenshots ----------------------------------------------------------------------
    {
        auto empty = std::make_unique<OctoSliceProcessor>();
        empty->prepareToPlay (48000.0, 512);
        std::unique_ptr<OctoSliceEditor> ed (dynamic_cast<OctoSliceEditor*> (empty->createEditor()));
        for (int i = 0; i < 40; ++i) ed->advanceFrame (1.0f / 60.0f);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (shotDir + "/octoslice_empty.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        CHECK (f.getSize() > 1000, "rendered empty-state screenshot");
    }
    {
        setParam (*proc, "mode", 1); proc->updateSlicesNow();
        std::unique_ptr<OctoSliceEditor> ed (dynamic_cast<OctoSliceEditor*> (proc->createEditor()));
        juce::AudioBuffer<float> buf (2, 512);
        for (int frame = 0; frame < 50; ++frame)
        {
            juce::MidiBuffer midi;
            if (frame == 20) midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 120), 0);
            if (frame == 38) midi.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 110), 0);
            proc->processBlock (buf, midi);
            ed->advanceFrame (1.0f / 60.0f);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (shotDir + "/octoslice_loaded.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        CHECK (f.getSize() > 1000, "rendered loaded-state screenshot");

        setParam (*proc, "mode", 0);
        for (int i = 0; i < 5; ++i) ed->advanceFrame (1.0f / 60.0f);
        proc->updateSlicesNow();
        for (int i = 0; i < 5; ++i) ed->advanceFrame (1.0f / 60.0f);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f2 (shotDir + "/octoslice_grid.png");
        f2.deleteFile(); juce::FileOutputStream os2 (f2); juce::PNGImageFormat().writeImageToStream (img2, os2);
    }


    // --- zoomed view with a selection + benchmark ----------------------------------------------------
    {
        setParam (*proc, "mode", 0); setParam (*proc, "division", 3); setParam (*proc, "bars", 2); proc->updateSlicesNow();
        std::unique_ptr<OctoSliceEditor> ed (dynamic_cast<OctoSliceEditor*> (proc->createEditor()));
        auto* edp = ed.get();
        for (int i = 0; i < 5; ++i) ed->advanceFrame (1.0f / 60.0f);
        const double ms = benchmarkFrames (*proc, *ed, [edp] (float dt) { edp->advanceFrame (dt); }, 130);
        std::cout << "  frame time (software renderer, 1000x688): " << juce::String (ms, 2) << " ms\n";
        CHECK (ms < 25.0, "a full frame renders fast enough for smooth animation");

        proc->setTrim (2000, proc->getSnapshot()->totalSamples() - 9000, true);
        auto& wv = ed->getWaveform();
        for (int i = 0; i < 5; ++i) ed->advanceFrame (1.0f / 60.0f);
        wv.zoomBy (0.4, proc->getSnapshot()->sliceStart (5));
        juce::MouseEvent* none = nullptr; juce::ignoreUnused (none);
        // select markers 4..6 with the keyboard-free API: simulate a lasso through mouse events
        const auto origin = wv.getScreenPosition();
        juce::ignoreUnused (origin);
        juce::AudioBuffer<float> buf (2, 512);
        for (int frame = 0; frame < 30; ++frame)
        {
            juce::MidiBuffer midi;
            if (frame == 10) midi.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 120), 0);
            proc->processBlock (buf, midi);
            ed->advanceFrame (1.0f / 60.0f);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (shotDir + "/octoslice_zoom.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        CHECK (f.getSize() > 1000, "rendered zoomed screenshot");

        wv.zoomToFit();
        wv.setSelection ({ 6, 7, 8 });
        for (int i = 0; i < 3; ++i) ed->advanceFrame (1.0f / 60.0f);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f2 (shotDir + "/octoslice_select.png");
        f2.deleteFile(); juce::FileOutputStream os2 (f2); juce::PNGImageFormat().writeImageToStream (img2, os2);
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
