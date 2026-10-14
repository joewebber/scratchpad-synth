#include "ui/Neumorph.h"

#include <cmath>
#include <map>
#include <tuple>

namespace scratchpad::ui
{

namespace
{
    // Message-thread only, like all painting.
    using Key = std::tuple<int, int, int, int, const Palette*>; // w, h, radius*10, kind, palette
    std::map<Key, juce::Image> cache;

    constexpr int inset_kind = 3;

    const ShadowPair& pairFor (const Palette& p, int kind)
    {
        switch (kind)
        {
            case (int) Elevation::raised: return p.raised;
            case (int) Elevation::lifted: return p.lifted;
            case (int) Elevation::small:  return p.small;
            default:                      return p.inset;
        }
    }

    int marginFor (const ShadowPair& s)
    {
        float m = 0.0f;
        for (const auto* layer : { &s.shade, &s.light })
            m = juce::jmax (m, juce::jmax (std::abs (layer->dx), std::abs (layer->dy)) + layer->blur);
        return (int) std::ceil (m) + 2;
    }

    void shadowOf (juce::Graphics& g, const juce::Path& path, const Shadow& s)
    {
        // CSS blur radius B is a Gaussian with sigma B/2; JUCE's radius is sigma.
        juce::DropShadow (s.colour, juce::jmax (1, juce::roundToInt (s.blur * 0.5f)),
                          { juce::roundToInt (s.dx), juce::roundToInt (s.dy) })
            .drawForPath (g, path);
    }

    juce::Image& imageFor (int w, int h, float radius, int kind, const Palette& p)
    {
        const Key key { w, h, juce::roundToInt (radius * 10.0f), kind, &p };
        auto it = cache.find (key);
        if (it != cache.end())
            return it->second;

        if (cache.size() > 256)
            cache.clear();

        const auto& pair = pairFor (p, kind);
        juce::Image image;

        if (kind == inset_kind)
        {
            image = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics g (image);

            juce::Path rounded;
            rounded.addRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, radius);
            g.reduceClipRegion (rounded);

            // A frame around the well: its shadow falls inside the edge.
            juce::Path frame;
            frame.setUsingNonZeroWinding (false);
            frame.addRectangle (-60.0f, -60.0f, (float) w + 120.0f, (float) h + 120.0f);
            frame.addRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, radius);

            shadowOf (g, frame, pair.shade);
            shadowOf (g, frame, pair.light);
        }
        else
        {
            const int m = marginFor (pair);
            image = juce::Image (juce::Image::ARGB, w + 2 * m, h + 2 * m, true);
            juce::Graphics g (image);

            juce::Path rounded;
            rounded.addRoundedRectangle ((float) m, (float) m, (float) w, (float) h, radius);
            shadowOf (g, rounded, pair.shade);
            shadowOf (g, rounded, pair.light);
        }

        return cache.emplace (key, std::move (image)).first->second;
    }
}

void drawRaised (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                 const Palette& p, Elevation e, juce::Colour fill)
{
    const int x = juce::roundToInt (bounds.getX()), y = juce::roundToInt (bounds.getY());
    const int w = juce::roundToInt (bounds.getWidth()), h = juce::roundToInt (bounds.getHeight());
    if (w <= 0 || h <= 0)
        return;

    const int m = marginFor (pairFor (p, (int) e));
    g.drawImageAt (imageFor (w, h, radius, (int) e, p), x - m, y - m);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
}

void drawInset (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                const Palette& p, juce::Colour fill)
{
    const int x = juce::roundToInt (bounds.getX()), y = juce::roundToInt (bounds.getY());
    const int w = juce::roundToInt (bounds.getWidth()), h = juce::roundToInt (bounds.getHeight());
    if (w <= 0 || h <= 0)
        return;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.drawImageAt (imageFor (w, h, radius, inset_kind, p), x, y);
}

} // namespace scratchpad::ui
