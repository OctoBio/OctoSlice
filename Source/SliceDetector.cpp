#include "SliceDetector.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace octo
{
    std::vector<int> gridSlices (int totalSamples, int numSlices)
    {
        std::vector<int> pts;
        numSlices = juce::jlimit (1, maxSlices, numSlices);
        for (int i = 0; i < numSlices; ++i)
            pts.push_back ((int) std::llround ((double) totalSamples * i / numSlices));
        return pts;
    }

    static float monoSample (const juce::AudioBuffer<float>& a, int i)
    {
        float s = 0.0f;
        for (int c = 0; c < a.getNumChannels(); ++c)
            s += a.getSample (c, i);
        return s / (float) juce::jmax (1, a.getNumChannels());
    }

    std::vector<int> transientSlices (const juce::AudioBuffer<float>& audio, double sampleRate,
                                      float sensitivity, int minLengthSamples)
    {
        const int total = audio.getNumSamples();
        std::vector<int> result { 0 };
        if (total < 2048)
            return result;

        constexpr int order = 10, fftSize = 1 << order, hop = 256;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);

        std::vector<float> mono ((size_t) total);
        for (int i = 0; i < total; ++i)
            mono[(size_t) i] = monoSample (audio, i);

        // --- Spectral flux onset function ---------------------------------
        const int numFrames = (total - fftSize) / hop + 1;
        std::vector<float> flux ((size_t) numFrames, 0.0f);
        std::vector<float> fftData ((size_t) fftSize * 2), prev ((size_t) fftSize / 2, 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            std::copy_n (mono.begin() + f * hop, fftSize, fftData.begin());
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            float sum = 0.0f;
            for (int b = 1; b < fftSize / 2; ++b)
            {
                const float m = std::log1p (100.0f * fftData[(size_t) b]);
                sum += juce::jmax (0.0f, m - prev[(size_t) b]);
                prev[(size_t) b] = m;
            }
            flux[(size_t) f] = sum;
        }
        if (numFrames > 0) flux[0] = 0.0f; // first frame always "rises" from silence

        // Energy rise (dB) catches low-frequency hits (kicks, 808s) that barely move the spectral flux
        std::vector<float> rise ((size_t) numFrames, 0.0f), levelDb ((size_t) numFrames, -80.0f);
        for (int f = 0; f < numFrames; ++f)
        {
            float e = 0.0f;
            for (int i = 0; i < fftSize; ++i) { const float v = mono[(size_t) (f * hop + i)]; e += v * v; }
            levelDb[(size_t) f] = juce::jmax (-80.0f, 10.0f * std::log10 (e / fftSize + 1.0e-12f));
        }
        for (int f = 2; f < numFrames; ++f)
            rise[(size_t) f] = juce::jmax (0.0f, levelDb[(size_t) f] - levelDb[(size_t) f - 2]);

        const float maxFlux = *std::max_element (flux.begin(), flux.end());
        const float maxRise = *std::max_element (rise.begin(), rise.end());
        if (maxFlux <= 0.0f && maxRise <= 0.0f)
            return result;
        for (int f = 0; f < numFrames; ++f)
            flux[(size_t) f] = 0.5f * (maxFlux > 0.0f ? flux[(size_t) f] / maxFlux : 0.0f)
                             + 0.5f * (maxRise > 0.0f ? rise[(size_t) f] / maxRise : 0.0f);

        // --- Peak picking ---------------------------------------------------
        sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);
        const float delta = juce::jmap (sensitivity, 0.40f, 0.015f);
        const int meanRadius = 10, peakRadius = 3;

        // Short-time energy envelope (32-sample blocks) for attack refinement
        constexpr int blk = 32;
        const int numBlocks = total / blk;
        std::vector<float> env ((size_t) numBlocks, 0.0f);
        for (int b = 0; b < numBlocks; ++b)
        {
            float e = 0.0f;
            for (int i = 0; i < blk; ++i) { const float s = mono[(size_t) (b * blk + i)]; e += s * s; }
            env[(size_t) b] = std::sqrt (e / blk);
        }

        int lastOnset = 0;
        for (int f = 1; f < numFrames; ++f)
        {
            const float v = flux[(size_t) f];
            bool isPeak = true;
            for (int k = -peakRadius; k <= peakRadius && isPeak; ++k)
            {
                const int j = f + k;
                if (k != 0 && j >= 0 && j < numFrames && flux[(size_t) j] > v)
                    isPeak = false;
            }
            if (! isPeak) continue;

            float mean = 0.0f; int n = 0;
            for (int j = juce::jmax (0, f - meanRadius); j <= juce::jmin (numFrames - 1, f + meanRadius); ++j)
            { mean += flux[(size_t) j]; ++n; }
            mean /= (float) n;

            if (v < mean + delta) continue;

            // Attack enters the analysis window somewhere in [f*hop, f*hop + fftSize)
            const int searchStart = juce::jmax (1, (f * hop - hop) / blk);
            const int searchEnd   = juce::jmin (numBlocks - 1, (f * hop + fftSize) / blk);
            int best = searchStart; float bestRise = -1.0f;
            for (int b = searchStart; b <= searchEnd; ++b)
            {
                const float r = env[(size_t) b] - env[(size_t) b - 1];
                if (r > bestRise) { bestRise = r; best = b; }
            }
            int onset = (best - 1) * blk;

            // step back to the quietest point just before the attack
            {
                const int from = juce::jmax (0, onset - 64);
                int quiet = onset; float qv = 1.0e9f;
                for (int i = onset; i >= from; --i)
                {
                    const float a = std::abs (mono[(size_t) i]);
                    if (a < qv) { qv = a; quiet = i; }
                }
                onset = quiet;
            }

            if (onset - lastOnset < minLengthSamples)
                continue;

            result.push_back (onset);
            lastOnset = onset;
        }

        juce::ignoreUnused (sampleRate);
        sanitise (result, total);
        return result;
    }

    void snapToZeroCrossings (std::vector<int>& points, const juce::AudioBuffer<float>& audio, int radius)
    {
        const int total = audio.getNumSamples();
        for (size_t p = 1; p < points.size(); ++p)
        {
            const int c = points[p];
            int best = c;
            float bestAbs = 1.0e9f;
            for (int d = 0; d <= radius; ++d)
            {
                for (int sgn : { -1, 1 })
                {
                    const int i = c + sgn * d;
                    if (i <= 0 || i >= total) continue;
                    const float a = monoSample (audio, i - 1), b = monoSample (audio, i);
                    if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f))
                    {
                        points[p] = i;
                        goto next;
                    }
                    const float ab = std::abs (b);
                    if (ab < bestAbs) { bestAbs = ab; best = i; }
                }
            }
            points[p] = best;
        next:;
        }
    }

    void sanitise (std::vector<int>& points, int totalSamples, int minGap)
    {
        for (auto& p : points) p = juce::jlimit (0, juce::jmax (0, totalSamples - 1), p);
        points.push_back (0);
        std::sort (points.begin(), points.end());

        std::vector<int> out;
        for (int p : points)
        {
            if (out.empty()) { out.push_back (0); continue; }
            if (p - out.back() >= minGap && totalSamples - p >= minGap)
                out.push_back (p);
        }
        if ((int) out.size() > maxSlices)
            out.resize ((size_t) maxSlices);
        points = std::move (out);
    }
}
