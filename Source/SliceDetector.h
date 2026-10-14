#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace octo
{
    constexpr int maxSlices = 128;

    /** Equal divisions of the whole sample. */
    std::vector<int> gridSlices (int totalSamples, int numSlices);

    /** Onset detection (spectral flux + adaptive threshold), refined to the attack start.
        sensitivity: 0..1 (higher = more slices). minLengthSamples: minimum gap between slices. */
    std::vector<int> transientSlices (const juce::AudioBuffer<float>& audio,
                                      double sampleRate,
                                      float sensitivity,
                                      int minLengthSamples);

    /** Moves every point (except the first) to the nearest zero crossing within +/- radius. */
    void snapToZeroCrossings (std::vector<int>& points, const juce::AudioBuffer<float>& audio, int radius);

    /** Sorts, removes duplicates / points too close together, forces a 0 start, clamps to maxSlices. */
    void sanitise (std::vector<int>& points, int totalSamples, int minGap = 32);
}
