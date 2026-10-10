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

}  // namespace scsy
