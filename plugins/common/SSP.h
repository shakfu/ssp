#pragma once

#include <algorithm>

// Title suffix after the description. A plugin built with SSP_TITLE_NO_MANUFACTURER omits it.
#ifdef SSP_TITLE_NO_MANUFACTURER
#define SSP_TITLE_SUFFIX ""
#else
#define SSP_TITLE_SUFFIX " @ " JucePlugin_Manufacturer
#endif

// Version on screen and in the descriptor: the build date, or the CMake project version for a
// plugin built with SSP_VERSION_FROM_PROJECT.
#ifdef SSP_VERSION_FROM_PROJECT
#define SSP_VERSION_STRING JucePlugin_VersionString
#else
#define SSP_VERSION_STRING TB_BUILD_DATE
#endif


enum SSPButtons {
    SSP_Soft_1,
    SSP_Soft_2,
    SSP_Soft_3,
    SSP_Soft_4,
    SSP_Soft_5,
    SSP_Soft_6,
    SSP_Soft_7,
    SSP_Soft_8,
    SSP_Left,
    SSP_Right,
    SSP_Up,
    SSP_Down,
    SSP_Shift_L,
    SSP_Shift_R,
    SSP_LastBtn
};


static constexpr unsigned SSP_FULL_WIDTH = 1600;
static constexpr unsigned SSP_FULL_HEIGHT = 480;

#ifdef COMPACT_UI_SCALE
static constexpr unsigned SSP_COMPACT_SCALE = COMPACT_UI_SCALE;
#else
static constexpr unsigned SSP_COMPACT_SCALE = 2;
#endif

static constexpr unsigned SSP_COMPACT_WIDTH = 320 * SSP_COMPACT_SCALE;
static constexpr unsigned SSP_COMPACT_HEIGHT = 240 * SSP_COMPACT_SCALE;

inline float cv2Pitch(float r) {
    static constexpr float p1 = 0.0f;  // first C note
    static constexpr float p2 = 0.2f;  // second C note
    static constexpr float scale = 12.0f / (p2 - p1);
    float arg = r;
    arg = arg - p1;
    arg = arg * scale;
    return arg;
}


inline float pitch2Cv(float r) {
    static constexpr float p1 = 0.0f;  // first C note
    static constexpr float p2 = 0.2f;  // second C note
    static constexpr float iscale = (p2 - p1) / 12.0f;
    float arg = r;
    arg = arg * iscale;
    arg = arg + p1;
    return arg;
}
