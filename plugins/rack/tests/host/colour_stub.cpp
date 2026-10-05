// juce_graphics headers define colour constants, which need this constructor at link time.
// Building juce_graphics for it would add freetype, harfbuzz and sheenbidi; the test draws nothing.

#include <juce_graphics/juce_graphics.h>

juce::Colour::Colour(juce::uint32) noexcept {}
