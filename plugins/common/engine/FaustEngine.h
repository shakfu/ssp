#pragma once

// Hosts a Faust kernel from scripts/faust_kernel.sh as an Engine. Inputs are the kernel's audio
// inputs, then one CV per control; outputs are the kernel's. See docs/dev/faust.md.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "engine/Engine.h"
#include "engine/FaustArch.h"

namespace ssp::faust {

// A slider, number entry, button or checkbox.
struct Control {
    std::string label;
    float init, lo, hi, step;
};

struct Spec {
    int ins = 0, outs = 0;
    std::vector<Control> controls;
};

// Collects the kernel's controls in declaration order.
class Collector : public UI {
public:
    std::vector<Control> controls;
    std::vector<FAUSTFLOAT*> zones;

    void addButton(const char* l, FAUSTFLOAT* z) override { add(l, z, 0, 0, 1, 1); }
    void addCheckButton(const char* l, FAUSTFLOAT* z) override { add(l, z, 0, 0, 1, 1); }
    void addVerticalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi,
                           FAUSTFLOAT s) override {
        add(l, z, i, lo, hi, s);
    }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi,
                             FAUSTFLOAT s) override {
        add(l, z, i, lo, hi, s);
    }
    void addNumEntry(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT s) override {
        add(l, z, i, lo, hi, s);
    }

private:
    void add(const char* l, FAUSTFLOAT* z, float i, float lo, float hi, float s) {
        controls.push_back({ l, i, lo, hi, s });
        zones.push_back(z);
    }
};

// The kernel-independent part, which FaustProcessor drives.
class FaustEngineBase : public engine::Engine {
public:
    const Spec& spec() const { return spec_; }

    // Audio thread: control i's value before its CV.
    void set(int i, float v) { base_[size_t(i)] = v; }

protected:
    void bind(int ins, int outs, Collector& c) {
        spec_ = { ins, outs, c.controls };
        zones_ = c.zones;
        for (auto& k : spec_.controls) base_.push_back(k.init);
    }

    // A CV adds to its control: 1.0 (5 V) spans the range. Read once per block.
    void apply(const float* const* in) {
        for (size_t i = 0; i < zones_.size(); i++) {
            auto& k = spec_.controls[i];
            float v = base_[i] + in[size_t(spec_.ins) + i][0] * (k.hi - k.lo);
            *zones_[i] = std::min(std::max(v, k.lo), k.hi);
        }
    }

private:
    Spec spec_;
    std::vector<FAUSTFLOAT*> zones_;
    std::vector<float> base_;
};

template <class Kernel>
class FaustEngine : public FaustEngineBase {
public:
    // The kernel is on the heap: its delay lines can be megabytes.
    FaustEngine() : kernel_(std::make_unique<Kernel>()) {
        Collector c;
        kernel_->buildUserInterface(&c);
        bind(kernel_->getNumInputs(), kernel_->getNumOutputs(), c);
    }

    void prepare(float sampleRate, int) override { kernel_->init(int(sampleRate)); }

    void process(const float* const* in, float* const* out, int n) override {
        apply(in);
        // compute() does not write its inputs
        kernel_->compute(n, const_cast<FAUSTFLOAT**>(in), const_cast<FAUSTFLOAT**>(out));
    }

private:
    std::unique_ptr<Kernel> kernel_;
};

}  // namespace ssp::faust
