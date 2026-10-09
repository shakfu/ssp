#pragma once

// Base types a Faust-generated kernel (`class mydsp`) needs, from sk-engines src/engine/faust_arch.h.
// Faust's own dsp.h, UI.h and meta.h carry a GPL-with-exception header; the kernel needs only these.
// They live in ssp::faust, where scripts/faust_kernel.sh places each kernel, so they cannot clash
// with juce::dsp under `using namespace juce`.

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

namespace ssp::faust {

// Metadata sink for kernel.metadata(Meta*).
struct Meta {
    virtual ~Meta() = default;
    virtual void declare(const char* /*key*/, const char* /*value*/) {}
};

// UI sink for kernel.buildUserInterface(UI*). Covers every widget Faust emits; all default to no-ops.
class UI {
public:
    virtual ~UI() = default;
    // layout
    virtual void openTabBox(const char*) {}
    virtual void openHorizontalBox(const char*) {}
    virtual void openVerticalBox(const char*) {}
    virtual void closeBox() {}
    // active widgets
    virtual void addButton(const char*, FAUSTFLOAT*) {}
    virtual void addCheckButton(const char*, FAUSTFLOAT*) {}
    virtual void addVerticalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addHorizontalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addNumEntry(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    // passive widgets
    virtual void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    // per-zone metadata
    virtual void declare(FAUSTFLOAT*, const char*, const char*) {}
};

// Base of the generated kernel, which declares its own virtual methods.
class dsp {
public:
    virtual ~dsp() = default;
};

}  // namespace ssp::faust
