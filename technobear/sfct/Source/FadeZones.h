#pragma once

// Where a voice's loop crossfades happen, for drawing them (after softcut-demo).

#include <algorithm>
#include <cmath>

namespace sfct {

// In buffer seconds. The outgoing head fades out past the loop's exit; the incoming head fades in
// from its entry. A fade spans fadeTime of buffer at any rate, since it advances with the head.
struct FadeZone {
    float origin;  // where the fade starts
    float dir;     // direction of travel: 1 forward, -1 reverse
    bool fadingIn;

    // the point `progress` (0..1) along the fade: buffer seconds, and fade value (0 silent, 1 full)
    float time(float progress, float fadeTime) const { return origin + dir * progress * fadeTime; }
    float fade(float progress) const { return fadingIn ? progress : 1.0f - progress; }
};

// Writes up to two zones into `out` and returns how many. softcut treats rate 0 as reverse.
inline unsigned fadeZones(float loopStart, float loopEnd, float rate, bool loop, float fadeTime, FadeZone (&out)[2]) {
    if (fadeTime <= 0.0f) return 0;
    float lo = std::min(loopStart, loopEnd), hi = std::max(loopStart, loopEnd);
    bool fwd = rate > 0.0f;
    float dir = fwd ? 1.0f : -1.0f;
    out[0] = { fwd ? hi : lo, dir, false };
    if (!loop) return 1;
    out[1] = { fwd ? lo : hi, dir, true };
    return 2;
}

// equal-power playback gain of a head at fade value `fade`, as ReadWriteHead::mixFade applies it
inline float fadeGain(float fade) {
    return std::sin(fade * float(M_PI_2));
}

}  // namespace sfct
