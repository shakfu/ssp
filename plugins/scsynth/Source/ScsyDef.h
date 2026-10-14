#pragma once

#include <string>
#include <vector>

namespace scsy {

// The first SynthDef in a .scsyndef file: its name and its named controls.
struct SynthDefInfo {
    struct Control {
        std::string name;
        int index = 0;      // the first of its values among the def's controls
        float value = 0.0f; // its initial value
    };
    std::string name;
    std::vector<Control> controls;  // by index
};

// Reads `bytes`, a .scsyndef in SCgf version 2 as nanosynth and sclang write it. False, with
// `error` set, if it is not one.
bool readSynthDef(const std::string& bytes, SynthDefInfo& info, std::string& error);

// `bytes` with its first SynthDef renamed to `name`; empty if `bytes` is not a SynthDef file.
std::string renameSynthDef(const std::string& bytes, const std::string& name);

// A score: an NRT command file, as sapf-sc's >nrt writes. The first /s_new names the main def, which
// takes the controls. Each later /s_new starts a voice at its bundle's time. /d_recv and buffer
// commands run when the score loads, wherever they are. The score loops at its last bundle's time.
struct Score {
    std::string main;                // the main def's file bytes
    std::vector<std::string> voices; // voice defs' file bytes
    std::vector<std::string> setup;  // buffer commands (/b_...), in file order
    struct Event {
        double time;  // seconds from the start
        int voice;    // into voices
    };
    std::vector<Event> events;       // by time
    double length = 0.0;             // seconds; the loop's period
};

// `message`, a /b_allocRead, with a relative path made relative to `dir`; other messages unchanged.
std::string resolveBufferPath(const std::string& message, const std::string& dir);

// Whether `bytes` starts like an NRT command file: a size, then "#bundle".
bool isScore(const std::string& bytes);

// Reads `bytes` into `score`. False, with `error` set, for a file it cannot play: a command other
// than /d_recv, /s_new, /d_free, /c_set, /g_freeAll and /b_..., an /s_new of a def not received
// before it, or no /s_new.
bool readScore(const std::string& bytes, Score& score, std::string& error);

}  // namespace scsy
