#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "SliceDetector.h"

namespace octo
{
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::String name;
        juce::String path;
    };

    /** Immutable snapshot shared between the UI and the audio thread. */
    struct Snapshot
    {
        std::shared_ptr<const SampleData> sample;
        std::vector<int> points; // slice start positions, first is always 0

        int numSlices() const noexcept     { return sample ? (int) points.size() : 0; }
        int totalSamples() const noexcept  { return sample ? sample->audio.getNumSamples() : 0; }
        int sliceStart (int i) const noexcept { return points[(size_t) i]; }
        int sliceEnd (int i) const noexcept
        {
            return (size_t) i + 1 < points.size() ? points[(size_t) i + 1] : totalSamples();
        }
    };

    enum class SliceMode { grid = 0, transient, manual };

    struct Voice
    {
        bool active = false;
        int note = -1, slice = -1;
        int start = 0, end = 0;
        double pos = 0.0, inc = 1.0;
        bool reverse = false;
        float velocity = 1.0f;
        float lastEnv = 0.0f;
        juce::ADSR env;
        bool killing = false;
        float killGain = 1.0f, killStep = 0.0f;
        juce::uint64 age = 0;
    };
}

class OctoSliceProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::Timer
{
public:
    OctoSliceProcessor();
    ~OctoSliceProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OctoSlice"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    bool loadFile (const juce::File& file);
    void loadAudio (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name, const juce::String& path);
    std::shared_ptr<const octo::Snapshot> getSnapshot() const;

    octo::SliceMode getMode() const;
    void addMarker (int samplePos);
    void moveMarker (int index, int samplePos);
    void removeMarker (int index);
    void resetToGrid();
    void clearMarkers();
    juce::File createMidiFile();
    void updateSlicesNow() { needsReslice = false; reslice(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    juce::AudioFormatManager formatManager;

    // Visual feedback (written on the audio thread, read by the UI)
    static constexpr int numVoices = 16;
    std::array<std::atomic<float>, numVoices> voicePos {};   // normalised position in the whole sample, < 0 = idle
    std::array<std::atomic<int>, numVoices>   voiceSlice {};
    std::array<std::atomic<float>, numVoices> voiceLevel {};
    std::array<std::atomic<juce::uint32>, octo::maxSlices> sliceTriggers {};
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<juce::uint32> snapshotVersion { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;

    void publish (std::vector<int> points);
    void reslice();
    void startNote (int note, float velocity, const octo::Snapshot& snap);
    void stopNote (int note);
    void renderVoices (juce::AudioBuffer<float>& out, int start, int num, const octo::Snapshot& snap);
    void setModeParam (octo::SliceMode m);

    std::shared_ptr<const octo::Snapshot> current, audioSnap;
    mutable juce::SpinLock snapshotLock;
    std::vector<std::shared_ptr<const octo::Snapshot>> graveyard;
    std::vector<int> manualPoints;

    std::atomic<bool> needsReslice { false };
    double lastBpmUsed = 0.0;

    std::array<octo::Voice, numVoices> voices;
    juce::uint64 voiceCounter = 0;
    const octo::SampleData* lastSamplePtr = nullptr;

    double hostSampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;
    juce::Reverb reverb;
    juce::SmoothedValue<float> gainSmoothed, toneSmoothed;

    // cached parameter pointers
    std::atomic<float>* pMode = nullptr;  std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pBars = nullptr;  std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pMinLen = nullptr; std::atomic<float>* pZeroX = nullptr;
    std::atomic<float>* pRoot = nullptr;  std::atomic<float>* pPlayMode = nullptr;
    std::atomic<float>* pChoke = nullptr; std::atomic<float>* pReverse = nullptr;
    std::atomic<float>* pAttack = nullptr; std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pPitch = nullptr; std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pSpace = nullptr; std::atomic<float>* pGain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctoSliceProcessor)
};
