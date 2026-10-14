#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace octo;

namespace ids
{
    static const juce::String mode = "mode", division = "division", bars = "bars", sens = "sens",
                              minLen = "minLen", zeroX = "zeroX", root = "root", playMode = "playMode",
                              choke = "choke", reverse = "reverse", attack = "attack", release = "release",
                              pitch = "pitch", tone = "tone", space = "space", gain = "gain";
}

static constexpr double maxEmbedSeconds = 90.0;

//==============================================================================
OctoSliceProcessor::OctoSliceProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pMode = raw (ids::mode); pDivision = raw (ids::division); pBars = raw (ids::bars); pSens = raw (ids::sens);
    pMinLen = raw (ids::minLen); pZeroX = raw (ids::zeroX); pRoot = raw (ids::root); pPlayMode = raw (ids::playMode);
    pChoke = raw (ids::choke); pReverse = raw (ids::reverse); pAttack = raw (ids::attack); pRelease = raw (ids::release);
    pPitch = raw (ids::pitch); pTone = raw (ids::tone); pSpace = raw (ids::space); pGain = raw (ids::gain);

    for (auto* id : { &ids::mode, &ids::division, &ids::bars, &ids::sens, &ids::minLen, &ids::zeroX })
        apvts.addParameterListener (*id, this);

    for (auto& v : voicePos)   v = -1.0f;
    for (auto& v : voiceSlice) v = -1;
    for (auto& v : voiceLevel) v = 0.0f;
    for (auto& t : sliceTriggers) t = 0;

    current = std::make_shared<Snapshot>();
    startTimerHz (20);
}

OctoSliceProcessor::~OctoSliceProcessor()
{
    stopTimer();
    for (auto* id : { &ids::mode, &ids::division, &ids::bars, &ids::sens, &ids::minLen, &ids::zeroX })
        apvts.removeParameterListener (*id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout OctoSliceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };

    l.add (std::make_unique<AudioParameterChoice> (pid (ids::mode), "Slice Mode", StringArray { "Grid", "Transient", "Manual" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::division), "Division", StringArray { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::bars), "Sample Length", StringArray { "Auto", "1/2", "1", "2", "4", "8" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::sens), "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 55.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::minLen), "Min Length", NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.5f), 70.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterBool>  (pid (ids::zeroX), "Zero Crossing", true));
    l.add (std::make_unique<AudioParameterInt>   (pid (ids::root), "Root Note", 0, 127, 60,
                                                  AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::playMode), "Play Mode", StringArray { "One-shot", "Gate" }, 1));
    l.add (std::make_unique<AudioParameterBool>  (pid (ids::choke), "Mono Choke", false));
    l.add (std::make_unique<AudioParameterBool>  (pid (ids::reverse), "Reverse", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::attack), "Attack", NormalisableRange<float> (0.5f, 1000.0f, 0.1f, 0.3f), 1.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::release), "Release", NormalisableRange<float> (1.0f, 3000.0f, 0.1f, 0.3f), 60.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::pitch), "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::tone), "Tone", NormalisableRange<float> (80.0f, 20000.0f, 1.0f, 0.25f), 20000.0f,
                                                  AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::space), "Space", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::gain), "Gain", NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

bool OctoSliceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void OctoSliceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    toneFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setResonance (0.75f);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    gainSmoothed.reset (sampleRate, 0.03);
    toneSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    toneSmoothed.setCurrentAndTargetValue (pTone->load());
    for (auto& v : voices) { v.active = false; v.env.setSampleRate (sampleRate); }
}

std::shared_ptr<const Snapshot> OctoSliceProcessor::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl (snapshotLock);
    return current;
}

SliceMode OctoSliceProcessor::getMode() const
{
    return (SliceMode) juce::jlimit (0, 2, (int) pMode->load());
}

//==============================================================================
void OctoSliceProcessor::startNote (int note, float velocity, const Snapshot& snap)
{
    const int slice = note - (int) pRoot->load();
    if (slice < 0 || slice >= snap.numSlices())
        return;

    const bool choke = pChoke->load() > 0.5f;
    for (auto& v : voices)
    {
        if (v.active && ! v.killing && (choke || v.note == note))
        {
            v.killing = true;
            v.killGain = 1.0f;
            v.killStep = 1.0f / (float) juce::jmax (1.0, hostSampleRate * 0.004);
        }
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age) target = &v;
    }

    auto& v = *target;
    v.active = true;
    v.killing = false;
    v.note = note;
    v.slice = slice;
    v.start = snap.sliceStart (slice);
    v.end = snap.sliceEnd (slice);
    v.reverse = pReverse->load() > 0.5f;
    v.pos = v.reverse ? (double) (v.end - 1) : (double) v.start;
    v.inc = snap.sample->sampleRate / hostSampleRate * std::pow (2.0, pPitch->load() / 12.0);
    v.velocity = 0.25f + 0.75f * velocity;
    v.age = ++voiceCounter;
    v.env.setSampleRate (hostSampleRate);
    v.env.setParameters ({ pAttack->load() / 1000.0f, 0.0f, 1.0f, pRelease->load() / 1000.0f });
    v.env.reset();
    v.env.noteOn();

    sliceTriggers[(size_t) slice].fetch_add (1);
}

void OctoSliceProcessor::stopNote (int note)
{
    const bool gate = (int) pPlayMode->load() == 1;
    if (! gate) return;
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.killing)
        {
            v.env.setParameters ({ pAttack->load() / 1000.0f, 0.0f, 1.0f, pRelease->load() / 1000.0f });
            v.env.noteOff();
        }
}

static inline float hermite (const float* d, int len, double pos)
{
    const int i = (int) pos;
    const float t = (float) (pos - i);
    auto at = [d, len] (int k) { return d[juce::jlimit (0, len - 1, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void OctoSliceProcessor::renderVoices (juce::AudioBuffer<float>& out, int start, int num, const Snapshot& snap)
{
    if (snap.sample == nullptr) return;
    const auto& audio = snap.sample->audio;
    const int len = audio.getNumSamples();
    const float* srcL = audio.getReadPointer (0);
    const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
    float* outL = out.getWritePointer (0, start);
    float* outR = out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0, start);
    const bool mono = out.getNumChannels() == 1;
    const double edgeFade = juce::jmax (8.0, snap.sample->sampleRate * 0.002);

    for (auto& v : voices)
    {
        if (! v.active) continue;
        const int vEnd = juce::jmin (v.end, len), vStart = juce::jlimit (0, len, v.start);

        for (int i = 0; i < num; ++i)
        {
            const double dist = v.reverse ? (v.pos - vStart) : (vEnd - 1 - v.pos);
            if (dist <= 0.0) { v.active = false; break; }

            const float e = v.env.getNextSample();
            if (! v.env.isActive()) { v.active = false; break; }

            v.lastEnv = e * (v.killing ? v.killGain : 1.0f);
            float g = e * v.velocity * (float) juce::jmin (1.0, dist / edgeFade);
            if (v.killing)
            {
                v.killGain -= v.killStep;
                if (v.killGain <= 0.0f) { v.active = false; break; }
                g *= v.killGain;
            }

            const float l = hermite (srcL, len, v.pos) * g;
            const float r = hermite (srcR, len, v.pos) * g;
            if (mono) outL[i] += 0.5f * (l + r);
            else { outL[i] += l; outR[i] += r; }

            v.pos += v.reverse ? -v.inc : v.inc;
        }
    }
}

void OctoSliceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    {
        const juce::SpinLock::ScopedTryLockType sl (snapshotLock);
        if (sl.isLocked()) audioSnap = current; // old snapshots stay alive in the graveyard -> no free here
    }
    const auto snap = audioSnap;
    if (snap == nullptr)
    {
        for (auto& v : voicePos) v = -1.0f;
        return;
    }

    if (snap->sample.get() != lastSamplePtr)
    {
        for (auto& v : voices) v.active = false;
        lastSamplePtr = snap->sample.get();
    }

    int cursor = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        if (t > cursor) { renderVoices (buffer, cursor, t - cursor, *snap); cursor = t; }

        if (msg.isNoteOn())
        {
            if (snap->sample) startNote (msg.getNoteNumber(), msg.getFloatVelocity(), *snap);
        }
        else if (msg.isNoteOff())
            stopNote (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices) v.active = false;
    }
    if (cursor < numSamples)
        renderVoices (buffer, cursor, numSamples - cursor, *snap);

    // --- Tone filter --------------------------------------------------------
    toneSmoothed.setTargetValue (pTone->load());
    if (toneSmoothed.isSmoothing() || toneSmoothed.getTargetValue() < 19900.0f)
    {
        const int chs = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            const float fc = toneSmoothed.getNextValue();
            if ((i & 15) == 0) toneFilter.setCutoffFrequency (juce::jmin (fc, (float) hostSampleRate * 0.45f));
            for (int c = 0; c < chs; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                d[i] = toneFilter.processSample (c, d[i]);
            }
        }
    }

    // --- Space (reverb) -------------------------------------------------------
    const float space = pSpace->load() / 100.0f;
    if (space > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.4f * space;
        rp.damping = 0.35f;
        rp.width = 1.0f;
        rp.wetLevel = 0.55f * space;
        rp.dryLevel = 1.0f - 0.35f * space;
        reverb.setParameters (rp);
        if (buffer.getNumChannels() > 1)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        else
            reverb.processMono (buffer.getWritePointer (0), numSamples);
    }

    // --- Gain + metering -------------------------------------------------------
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    float sumSq = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gainSmoothed.getNextValue();
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[i] *= g;
            sumSq += d[i] * d[i];
        }
    }
    const float rms = std::sqrt (sumSq / (float) juce::jmax (1, numSamples * buffer.getNumChannels()));
    outputLevel = juce::jmax (rms, outputLevel.load() * 0.92f);

    // --- Voice feedback for the UI --------------------------------------------
    const float total = (float) juce::jmax (1, snap->totalSamples());
    for (int i = 0; i < numVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        voicePos[(size_t) i] = v.active ? (float) v.pos / total : -1.0f;
        voiceSlice[(size_t) i] = v.active ? v.slice : -1;
        voiceLevel[(size_t) i] = v.active ? v.lastEnv : 0.0f;
    }

    midi.clear();
}

//==============================================================================
void OctoSliceProcessor::parameterChanged (const juce::String&, float)
{
    needsReslice = true;
}

void OctoSliceProcessor::timerCallback()
{
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const auto& p) { return p.use_count() <= 1; }),
                     graveyard.end());

    if (getMode() == SliceMode::grid && (int) pBars->load() == 0)
    {
        const double bpm = hostBpm.load();
        if (std::abs (bpm - lastBpmUsed) > 0.01)
            needsReslice = true;
    }

    if (needsReslice.exchange (false))
        reslice();
}

void OctoSliceProcessor::publish (std::vector<int> points)
{
    auto old = getSnapshot();
    auto snap = std::make_shared<Snapshot>();
    snap->sample = old->sample;
    if (snap->sample)
        sanitise (points, snap->sample->audio.getNumSamples());
    else
        points.clear();
    snap->points = std::move (points);

    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        current = snap;
    }
    graveyard.push_back (old);
    ++snapshotVersion;
}

void OctoSliceProcessor::reslice()
{
    auto snap = getSnapshot();
    if (snap->sample == nullptr) return;
    const auto& s = *snap->sample;
    const int total = s.audio.getNumSamples();

    std::vector<int> pts;
    switch (getMode())
    {
        case SliceMode::grid:
        {
            static const double barValues[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0 };
            double bars = barValues[juce::jlimit (0, 5, (int) pBars->load())];
            if (bars < 0.25)
            {
                lastBpmUsed = hostBpm.load();
                const double raw = (total / s.sampleRate) * lastBpmUsed / 240.0;
                bars = 0.5;
                for (double b : { 1.0, 2.0, 4.0, 8.0 })
                    if (std::abs (std::log2 (raw / b)) < std::abs (std::log2 (raw / bars))) bars = b;
            }
            const int div = 1 << (juce::jlimit (0, 5, (int) pDivision->load()) + 1);
            pts = gridSlices (total, juce::jmax (1, (int) std::lround (div * bars)));
            break;
        }
        case SliceMode::transient:
            pts = transientSlices (s.audio, s.sampleRate, pSens->load() / 100.0f,
                                   (int) (pMinLen->load() / 1000.0f * s.sampleRate));
            break;
        case SliceMode::manual:
            pts = manualPoints.empty() ? std::vector<int> { 0 } : manualPoints;
            break;
    }

    if (getMode() != SliceMode::manual && pZeroX->load() > 0.5f)
        snapToZeroCrossings (pts, s.audio, (int) (s.sampleRate * 0.004));

    publish (std::move (pts));
}

void OctoSliceProcessor::setModeParam (SliceMode m)
{
    if (auto* p = apvts.getParameter (ids::mode))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
        p->endChangeGesture();
    }
}

void OctoSliceProcessor::addMarker (int samplePos)
{
    auto snap = getSnapshot();
    if (snap->sample == nullptr || snap->numSlices() >= maxSlices) return;
    if (getMode() != SliceMode::manual) manualPoints = snap->points;
    manualPoints.push_back (samplePos);
    sanitise (manualPoints, snap->totalSamples());
    setModeParam (SliceMode::manual);
    publish (manualPoints);
}

void OctoSliceProcessor::moveMarker (int index, int samplePos)
{
    auto snap = getSnapshot();
    if (snap->sample == nullptr || index <= 0 || index >= snap->numSlices()) return;
    if (getMode() != SliceMode::manual) { manualPoints = snap->points; setModeParam (SliceMode::manual); }
    if (index >= (int) manualPoints.size()) return;

    const int lo = manualPoints[(size_t) index - 1] + 64;
    const int hi = ((size_t) index + 1 < manualPoints.size() ? manualPoints[(size_t) index + 1] : snap->totalSamples()) - 64;
    if (hi <= lo) return;
    manualPoints[(size_t) index] = juce::jlimit (lo, hi, samplePos);
    publish (manualPoints);
}

void OctoSliceProcessor::removeMarker (int index)
{
    auto snap = getSnapshot();
    if (snap->sample == nullptr || index <= 0 || index >= snap->numSlices()) return;
    if (getMode() != SliceMode::manual) manualPoints = snap->points;
    if (index < (int) manualPoints.size())
        manualPoints.erase (manualPoints.begin() + index);
    setModeParam (SliceMode::manual);
    publish (manualPoints);
}

void OctoSliceProcessor::clearMarkers()
{
    manualPoints = { 0 };
    setModeParam (SliceMode::manual);
    publish (manualPoints);
}

void OctoSliceProcessor::resetToGrid()
{
    setModeParam (SliceMode::grid);
    reslice();
}

//==============================================================================
bool OctoSliceProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, chans > 1);

    loadAudio (std::move (buf), reader->sampleRate, file.getFileNameWithoutExtension(), file.getFullPathName());
    return true;
}

void OctoSliceProcessor::loadAudio (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name, const juce::String& path)
{
    auto data = std::make_shared<SampleData>();
    data->audio = std::move (audio);
    data->sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    data->name = name;
    data->path = path;

    auto snap = std::make_shared<Snapshot>();
    snap->sample = data;
    snap->points = { 0 };
    auto old = getSnapshot();
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        current = snap;
    }
    graveyard.push_back (old);
    ++snapshotVersion;

    manualPoints.clear();
    if (getMode() == SliceMode::manual)
        setModeParam (SliceMode::transient);
    needsReslice = false;
    reslice();
}

//==============================================================================
juce::File OctoSliceProcessor::createMidiFile()
{
    auto snap = getSnapshot();
    if (snap->sample == nullptr) return {};

    const double bpm = juce::jlimit (20.0, 400.0, hostBpm.load());
    constexpr int ppq = 960;
    auto toTicks = [&] (int samplePos) { return samplePos / snap->sample->sampleRate * (bpm / 60.0) * ppq; };

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    const int root = (int) pRoot->load();
    for (int i = 0; i < snap->numSlices() && root + i < 128; ++i)
    {
        const double on = toTicks (snap->sliceStart (i));
        const double off = juce::jmax (on + 10.0, toTicks (snap->sliceEnd (i)) - 1.0);
        seq.addEvent (juce::MidiMessage::noteOn (1, root + i, (juce::uint8) 100), on);
        seq.addEvent (juce::MidiMessage::noteOff (1, root + i), off);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);

    auto name = juce::File::createLegalFileName (snap->sample->name.isEmpty() ? "slices" : snap->sample->name);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OctoSlice").getChildFile (name + "_slices.mid");
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream fos (f);
    if (! fos.openedOk()) return {};
    mf.writeTo (fos);
    fos.flush();
    return f;
}

//==============================================================================
void OctoSliceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("OctoSliceState");
    state.appendChild (apvts.copyState(), nullptr);

    auto snap = getSnapshot();
    if (snap->sample)
    {
        const auto& s = *snap->sample;
        juce::ValueTree st ("Sample");
        st.setProperty ("name", s.name, nullptr);
        st.setProperty ("path", s.path, nullptr);
        st.setProperty ("sampleRate", s.sampleRate, nullptr);

        juce::StringArray pts;
        for (int p : manualPoints) pts.add (juce::String (p));
        st.setProperty ("manual", pts.joinIntoString (","), nullptr);

        if (s.audio.getNumSamples() <= s.sampleRate * maxEmbedSeconds)
        {
            juce::MemoryBlock mb;
            {
                std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (mb, false);
                juce::FlacAudioFormat flac;
                const auto opts = juce::AudioFormatWriterOptions{}
                                      .withSampleRate (juce::jlimit (8000.0, 192000.0, s.sampleRate))
                                      .withNumChannels (s.audio.getNumChannels())
                                      .withBitsPerSample (24);
                if (auto writer = flac.createWriterFor (os, opts))
                    writer->writeFromAudioSampleBuffer (s.audio, 0, s.audio.getNumSamples());
            }
            if (mb.getSize() > 0)
                st.setProperty ("audio", juce::var (mb), nullptr);
        }
        state.appendChild (st, nullptr);
    }

    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void OctoSliceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid()) return;

    auto params = state.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto st = state.getChildWithName ("Sample");
    if (! st.isValid()) return;

    bool loaded = false;
    if (auto* mb = st.getProperty ("audio").getBinaryData())
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (
            flac.createReaderFor (new juce::MemoryInputStream (*mb, true), true));
        if (reader != nullptr)
        {
            const int len = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
            reader->read (&buf, 0, len, 0, true, reader->numChannels > 1);
            const auto mode = getMode();
            loadAudio (std::move (buf), (double) st.getProperty ("sampleRate", reader->sampleRate),
                       st.getProperty ("name").toString(), st.getProperty ("path").toString());
            setModeParam (mode);
            loaded = true;
        }
    }
    if (! loaded)
    {
        const juce::File f (st.getProperty ("path").toString());
        const auto mode = getMode();
        if (f.existsAsFile() && loadFile (f))
        {
            setModeParam (mode);
            loaded = true;
        }
    }

    if (loaded)
    {
        manualPoints.clear();
        for (auto& t : juce::StringArray::fromTokens (st.getProperty ("manual").toString(), ",", ""))
            if (t.isNotEmpty()) manualPoints.push_back (t.getIntValue());
        needsReslice = false;
        reslice();
    }
}

//==============================================================================
juce::AudioProcessorEditor* OctoSliceProcessor::createEditor()
{
    return new OctoSliceEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OctoSliceProcessor();
}
