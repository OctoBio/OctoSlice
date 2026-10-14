#include "SpaceBackground.h"
#include "OctoLookAndFeel.h"
#include <cmath>
#include <algorithm>

namespace octo
{
    SpaceBackground::SpaceBackground()
    {
        stars.resize (260);
        for (auto& s : stars) respawn (s, false);
    }

    void SpaceBackground::respawn (Star& s, bool far)
    {
        s.x = rng.nextFloat() * 2.0f - 1.0f;
        s.y = rng.nextFloat() * 2.0f - 1.0f;
        s.z = far ? 0.9f + 0.1f * rng.nextFloat() : 0.05f + 0.95f * rng.nextFloat();
        s.pz = s.z;
        s.twinkle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    }

    void SpaceBackground::tick (float dt, const Drive& d)
    {
        drive = d;
        dt = juce::jlimit (0.0f, 0.1f, dt);
        time += dt;
        smoothedLevel += (d.level - smoothedLevel) * juce::jmin (1.0f, dt * 10.0f);
        warpImpulse *= std::exp (-dt * 3.5f);

        const float target = 0.035f + 1.6f * smoothedLevel + 0.55f * warpImpulse;
        speed += (target - speed) * juce::jmin (1.0f, dt * 6.0f);
        const float dir = d.reverse ? -1.0f : 1.0f;

        for (auto& s : stars)
        {
            s.pz = s.z;
            s.z -= dir * speed * dt;
            if (s.z <= 0.02f)      respawn (s, true);
            else if (s.z > 1.0f)   { respawn (s, false); s.z = 0.05f + 0.2f * rng.nextFloat(); s.pz = s.z; }
        }

        for (auto& p : particles)
        {
            p.life -= dt;
            p.p += p.v * dt;
            p.v *= std::exp (-dt * 1.8f);
        }
        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                         particles.end());

        for (auto& r : rings) r.age += dt;
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.age >= r.maxAge; }), rings.end());
    }

    void SpaceBackground::burst (juce::Point<float> where, juce::Colour colour, float strength)
    {
        const int n = (int) (14 + 18 * strength);
        for (int i = 0; i < n && particles.size() < 600; ++i)
        {
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float sp = (40.0f + rng.nextFloat() * 180.0f) * (0.6f + 0.6f * strength);
            Particle p;
            p.p = where;
            p.v = { std::cos (a) * sp, std::sin (a) * sp * 0.6f };
            p.maxLife = p.life = 0.5f + rng.nextFloat() * 0.9f;
            p.size = 1.0f + rng.nextFloat() * 2.2f;
            p.c = colour.withRotatedHue ((rng.nextFloat() - 0.5f) * 0.08f);
            particles.push_back (p);
        }
        if (rings.size() < 40)
            rings.push_back ({ where, 0.0f, 0.9f + 0.4f * strength, colour });
    }

    void SpaceBackground::draw (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto centre = juce::Point<float> (area.getCentreX(), area.getY() + area.getHeight() * 0.38f);

        // --- deep background -------------------------------------------------
        {
            juce::ColourGradient bg (juce::Colour (0xff0b0c26), centre.x, centre.y,
                                     colours::bg, area.getRight(), area.getBottom(), true);
            g.setGradientFill (bg);
            g.fillRect (area);
        }

        // --- nebulae ----------------------------------------------------------
        const float hueShift = drive.pitch / 24.0f * 0.12f;
        const float lvl = smoothedLevel;
        struct Neb { float bx, by, ax, ay, fx, fy, r; juce::Colour c; };
        const Neb nebs[] = {
            { 0.22f, 0.30f, 0.07f, 0.05f, 0.050f, 0.037f, 0.42f, colours::violet },
            { 0.78f, 0.36f, 0.06f, 0.06f, 0.031f, 0.044f, 0.38f, colours::cyan },
            { 0.55f, 0.85f, 0.10f, 0.04f, 0.023f, 0.052f, 0.46f, colours::pink },
        };
        for (const auto& n : nebs)
        {
            const float x = area.getX() + area.getWidth() * (n.bx + n.ax * std::sin (time * n.fx * 6.28f));
            const float y = area.getY() + area.getHeight() * (n.by + n.ay * std::cos (time * n.fy * 6.28f));
            const float r = area.getWidth() * n.r * (0.85f + 0.35f * drive.space) * (1.0f + 0.12f * lvl);
            const float a = 0.10f + 0.10f * drive.space + 0.16f * lvl;
            const auto c = n.c.withRotatedHue (hueShift);
            juce::ColourGradient grad (c.withAlpha (a), x, y, c.withAlpha (0.0f), x + r, y, true);
            grad.addColour (0.4, c.withAlpha (a * 0.45f));
            g.setGradientFill (grad);
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }

        // --- planet horizon -----------------------------------------------------
        {
            const float pr = area.getWidth() * 0.62f;
            const auto pc = juce::Point<float> (area.getRight() - area.getWidth() * 0.12f, area.getBottom() + pr * 0.78f);
            juce::ColourGradient body (juce::Colour (0xff0d1030), pc.x, pc.y - pr, juce::Colour (0xff020309), pc.x, pc.y, false);
            g.setGradientFill (body);
            g.fillEllipse (pc.x - pr, pc.y - pr, pr * 2.0f, pr * 2.0f);
            for (int i = 4; i >= 0; --i)
            {
                g.setColour (colours::cyan.withRotatedHue (hueShift).withAlpha ((0.05f + 0.10f * lvl) / (float) (i + 1)));
                g.drawEllipse (pc.x - pr, pc.y - pr, pr * 2.0f, pr * 2.0f, 1.5f + (float) i * 3.0f);
            }
        }

        // --- stars ----------------------------------------------------------------
        const float f = area.getWidth() * 0.5f;
        const float brightness = 0.45f + 0.55f * drive.tone;
        for (const auto& s : stars)
        {
            const auto proj = [&] (float z) { return juce::Point<float> (centre.x + s.x / z * f * 0.5f, centre.y + s.y / z * f * 0.35f); };
            const auto p = proj (s.z);
            if (! area.contains (p)) continue;

            const float depth = 1.0f - s.z;
            const float tw = 0.75f + 0.25f * std::sin (time * 3.0f + s.twinkle);
            const float alpha = juce::jlimit (0.0f, 1.0f, depth * depth * 1.4f * tw * brightness);
            auto q = proj (juce::jmax (0.02f, s.pz));
            float streak = p.getDistanceFrom (q);
            if (streak > 26.0f) { q = p + (q - p) * (26.0f / streak); streak = 26.0f; }
            const auto col = (s.twinkle > 4.5f ? colours::cyan : s.twinkle < 1.0f ? colours::violet : juce::Colours::white);

            if (streak > 1.5f)
            {
                g.setColour (col.withAlpha (alpha * 0.45f));
                g.drawLine ({ q, p }, 0.5f + depth * 1.1f);
            }
            else
            {
                const float sz = 0.6f + depth * 2.4f;
                g.setColour (col.withAlpha (alpha));
                g.fillEllipse (p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz);
            }
        }

        // --- shock rings & particles ------------------------------------------------
        for (const auto& r : rings)
        {
            const float t = r.age / r.maxAge;
            const float rad = 8.0f + t * 150.0f;
            g.setColour (r.c.withAlpha ((1.0f - t) * 0.45f));
            g.drawEllipse (r.p.x - rad, r.p.y - rad * 0.55f, rad * 2.0f, rad * 1.1f, 1.4f * (1.0f - t) + 0.4f);
        }
        for (const auto& p : particles)
        {
            const float t = p.life / p.maxLife;
            g.setColour (p.c.withAlpha (t * 0.25f));
            g.fillEllipse (p.p.x - p.size * 2.0f, p.p.y - p.size * 2.0f, p.size * 4.0f, p.size * 4.0f);
            g.setColour (p.c.brighter (0.5f).withAlpha (t));
            g.fillEllipse (p.p.x - p.size * 0.5f, p.p.y - p.size * 0.5f, p.size, p.size);
        }
    }
}
