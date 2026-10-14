#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace octo
{
    /** Animated deep-space scene: warp starfield, drifting nebulae, particle bursts. */
    class SpaceBackground
    {
    public:
        struct Drive
        {
            float level = 0.0f;      // output level 0..1
            float pitch = 0.0f;      // semitones, shifts the nebula hue
            float space = 0.0f;      // reverb amount 0..1, nebula size/brightness
            float tone = 1.0f;       // 0..1 filter openness, star brightness
            bool  reverse = false;   // stars fly away instead of toward you
            int   numSlices = 0;
        };

        SpaceBackground();

        void tick (float dt, const Drive& d);
        void draw (juce::Graphics& g, juce::Rectangle<float> area) const;

        void burst (juce::Point<float> where, juce::Colour colour, float strength = 1.0f);
        void kick (float amount) { warpImpulse = juce::jmin (3.0f, warpImpulse + amount); }

    private:
        struct Star { float x, y, z, pz, twinkle; };
        struct Particle { juce::Point<float> p, v; float life, maxLife, size; juce::Colour c; };
        struct Ring { juce::Point<float> p; float age, maxAge; juce::Colour c; };

        void respawn (Star& s, bool far);

        std::vector<Star> stars;
        std::vector<Particle> particles;
        std::vector<Ring> rings;
        juce::Random rng { 0x0c705 };
        float time = 0.0f, warpImpulse = 0.0f, speed = 0.0f;
        float smoothedLevel = 0.0f;
        Drive drive;
    };
}
