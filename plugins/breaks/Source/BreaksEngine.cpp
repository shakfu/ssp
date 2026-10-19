#include "BreaksEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <vector>

// Source/ssp_api.inc, compiled into the generated engine
extern "C" {
void core_engine_ssp_outputs(CoreEngine*, bool* trigger, bool* clock);
void core_engine_ssp_volume(CoreEngine*, int16_t val);
void core_engine_ssp_filter(CoreEngine*, int16_t val);
void core_engine_ssp_gating(CoreEngine*, int16_t val);
void core_engine_ssp_toggle_playback(CoreEngine*);
void core_engine_ssp_step_bank(CoreEngine*, int steps);
void core_engine_ssp_division(CoreEngine*, uint8_t division);
void core_engine_ssp_tempo(CoreEngine*, uint16_t bpm);
}

namespace brks {

namespace fs = std::filesystem;

namespace {

constexpr float HIGH = 0.2f, LOW = 0.05f;  // gate thresholds: 1 V and 0.25 V
constexpr float VOLTS = 5.0f;              // per SSP unit
// The firmware reads its buttons once a millisecond, so a shorter trigger could pass unseen.
constexpr unsigned MIN_PRESS = CORE_RATE / 200;  // 5 ms
// Mult counts a press as a tap only if released within 200 ms; in 4 sends this, however long its gate.
constexpr unsigned TAP_PRESS = CORE_RATE / 50;  // 20 ms

int16_t knob(float v) {
    return int16_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 1023.0f));
}

bool isCard(const fs::path& p) {
    std::error_code ec;
    for (int b = 1; b <= 16; b++)
        if (fs::is_directory(p / ("bank" + std::to_string(b)), ec)) return true;
    return false;
}

}  // namespace

BreaksEngine::BreaksEngine()
    : volume_(panel.volume), filter_(panel.filter), gating_(panel.gating), tempo_(panel.tempo) {
    std::random_device r;
    core_ = core_engine_create((uint64_t(r()) << 32) | r());  // null when out of memory: silent
}

BreaksEngine::~BreaksEngine() {
    core_engine_destroy(core_);
    delete active_;
}

void BreaksEngine::prepare(float sampleRate, int) {
    step_ = double(CORE_RATE) / sampleRate;
    phase_ = 0.0;
    std::fill(&hist_[0][0], &hist_[0][0] + 8, 0.0f);
}

bool BreaksEngine::read(void* user, unsigned bank, unsigned slot, unsigned variant, uint64_t offset, void* dst,
                      size_t bytes) {
    auto* self = static_cast<BreaksEngine*>(user);
    return self->storage_.read(self->active_, bank, slot, variant, offset, dst, bytes);
}

// A bank the loader finished: a new card, a reload, or the bank the firmware asked for.
void BreaksEngine::takeBank() {
    ecto::Bank* bank;
    if (!storage_.retired.canPush() || !storage_.ready.pop(bank)) return;
    if (active_ && bank->library->generation < active_->library->generation) {
        storage_.retired.push(bank);  // superseded by a later load
        return;
    }
    bool same = active_ && active_->library->generation == bank->library->generation;
    if (active_) storage_.retired.push(active_);
    active_ = bank;
    storage_.activeGeneration = bank->library->generation;
    core_engine_set_catalogue(core_, bank->library->banks.data(), read, this);
    Saved r;
    bool restore = false;
    while (restores_.pop(r)) restore = true;
    if (restore) {
        CoreState s;
        core_engine_get_state(core_, &s);
        s.tempo = std::clamp<uint16_t>(r.tempo, 30, 300);
        s.bank = bank->index;  // load() asked for r.bank; a missing bank gives the first
        s.trigger_mode = std::min<uint8_t>(r.triggerMode, 3);
        // A new engine reads as stopped until a card plays; set_state would keep that, and the first
        // Play would then start instead of stop. A restore starts as a load does.
        s.stopped = s.muted = false;
        core_engine_set_state(core_, &s);
    }
    core_engine_set_resident_bank(core_, bank->index);
    if (!same) {  // a card folder's settings: CV polarity, clock behaviour, effect banks, start tempo
        CoreState s;
        core_engine_get_state(core_, &s);
        ecto::applySettings(*bank->library, s);
        core_engine_update_settings(core_, &s);
        core_engine_apply_start_tempo(core_);
        core_engine_apply_startup_playback(core_);
    }
    requested_ = -1;
}

void BreaksEngine::frame() {
    int16_t pcm[2];
    core_engine_process(core_, pcm);
    std::copy(&hist_[1][0], &hist_[1][0] + 6, &hist_[0][0]);
    hist_[3][0] = pcm[0] / 32768.0f;
    hist_[3][1] = pcm[1] / 32768.0f;
    core_engine_ssp_outputs(core_, &trig_, &clock_);
    if (++frames_ % CORE_BLOCK) return;

    CoreDisplay d;
    core_engine_display(core_, &d);
    if (d.requested_bank < 16 && requested_ != int(d.requested_bank)) {
        requested_ = int(d.requested_bank);
        storage_.requestedBank = requested_;
    }
    CoreState s;
    core_engine_get_state(core_, &s);
    std::unique_lock<std::mutex> l(lock_, std::try_to_lock);  // a missed refresh repeats in 10 ms
    if (!l) return;
    status_.loaded = active_ != nullptr;
    status_.stopped = d.stopped;
    status_.bank = d.bank;
    status_.slot = d.slot;
    status_.slice = d.slice;
    status_.rune = d.rune;
    status_.tempo = d.tempo;
    status_.effects = d.effects;
    status_.allowed = 0;
    for (int i = 0; i < 16; i++) status_.allowed |= uint16_t(s.runes[std::min<unsigned>(d.rune, 6)][i] << i);
    status_.triggerMode = s.trigger_mode;
    status_.division = s.division;
    saved_ = { s.tempo, s.bank, s.trigger_mode };
}

void BreaksEngine::process(const float* const* in, float* const* out, int n) {
    if (!core_) {
        for (int o = 0; o < O_MAX; o++) std::fill(out[o], out[o] + n, 0.0f);
        return;
    }
    takeBank();
    if (panel.volume != volume_) core_engine_ssp_volume(core_, knob(volume_ = panel.volume));
    if (panel.filter != filter_) core_engine_ssp_filter(core_, knob(filter_ = panel.filter));
    if (panel.gating != gating_) core_engine_ssp_gating(core_, knob(gating_ = panel.gating));
    if (panel.division != division_) core_engine_ssp_division(core_, division_ = panel.division);
    if (panel.tempo != tempo_) core_engine_ssp_tempo(core_, tempo_ = panel.tempo);
    if (int steps = bankSteps_.exchange(0)) core_engine_ssp_step_bank(core_, steps);
    for (int k = 0; k < Panel::K_MAX; k++) controls_.knobs[k] = std::clamp(panel.knobs[k], 0.0f, 1.0f);
    for (int c = 0; c < 3; c++) controls_.connected[c] = cvPatched[c];

    static constexpr int gateOf[Panel::B_MAX] = { I_MODE, -1, -1, I_TAP };  // held as the button is
    for (int i = 0; i < n; i++) {
        const bool multWas = gate_[I_MULT], bankWas = gate_[I_BANK];
        for (int g = I_CLOCK; g <= I_PLAY; g++) gate_[g] = in[g][i] > (gate_[g] ? LOW : HIGH);
        if (gate_[I_MULT] && !multWas) pressFor_[Panel::B_MULT] = TAP_PRESS;
        if (gate_[I_BANK] && !bankWas) core_engine_ssp_step_bank(core_, 1);
        if (gate_[I_CLOCK] && !clockIn_) core_engine_clock(core_, true);
        else if (!gate_[I_CLOCK]) core_engine_clock(core_, false);
        clockIn_ = gate_[I_CLOCK];

        bool play = panel.play || gate_[I_PLAY];
        if (play && !playWas_) core_engine_ssp_toggle_playback(core_);
        playWas_ = play;

        for (int b = 0; b < Panel::B_MAX; b++) {
            bool held = panel.buttons[b] || (gateOf[b] >= 0 && gate_[gateOf[b]]);
            if (held && !controls_.buttons[b]) pressFor_[b] = std::max(pressFor_[b], MIN_PRESS);
            controls_.buttons[b] = held || pressFor_[b] > 0;
        }
        for (int c = 0; c < 3; c++) controls_.cv[c] = in[I_AMEN + c][i] * VOLTS;
        core_engine_controls(core_, &controls_);

        for (phase_ += step_; phase_ >= 1.0; phase_ -= 1.0) {
            frame();
            for (auto& p : pressFor_)
                if (p) p--;
        }
        // 4-point Hermite between frames 1 and 2 of hist_
        const float t = float(phase_);
        for (int c = 0; c < 2; c++) {
            const float xm = hist_[0][c], x0 = hist_[1][c], x1 = hist_[2][c], x2 = hist_[3][c];
            const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
            out[O_L + c][i] = ((c3 * t + c2) * t + c1) * t + x0;
        }
        out[O_TRIG][i] = trig_ ? 1.0f : 0.0f;
        out[O_CLOCK][i] = clock_ ? 1.0f : 0.0f;
    }
}

void BreaksEngine::load(const std::string& path, int bank) {
    std::string root = resolve(path);
    std::lock_guard<std::mutex> l(lock_);
    if (root.empty()) {  // the card playing stays
        error_ = path.empty() ? "" : "no card folder (bank1 ... bank16) in " + path;
        return;
    }
    error_.clear();
    folder_ = root;
    storage_.load(root, bank < 0 || bank > 15 ? 16u : unsigned(bank));  // 16: the card's first bank
}

void BreaksEngine::restore(const Saved& s) {
    restores_.push(s);
}

std::string BreaksEngine::folder() const {
    std::lock_guard<std::mutex> l(lock_);
    return folder_;
}

std::string BreaksEngine::message() const {
    std::lock_guard<std::mutex> l(lock_);
    return error_.empty() ? storage_.status() : error_;
}

Status BreaksEngine::status() const {
    std::lock_guard<std::mutex> l(lock_);
    return status_;
}

Saved BreaksEngine::saved() const {
    std::lock_guard<std::mutex> l(lock_);
    return saved_;
}

std::string BreaksEngine::resolve(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path p = fs::path(path).lexically_normal();
    if (!p.has_filename()) p = p.parent_path();
    if (fs::is_regular_file(p, ec)) p = p.parent_path();
    if (!fs::is_directory(p, ec)) return {};
    if (isCard(p)) return p.string();
    if (p.filename().string().rfind("bank", 0) == 0 && isCard(p.parent_path())) return p.parent_path().string();
    std::vector<fs::path> cards;
    for (auto& e : fs::directory_iterator(p, ec))
        if (e.is_directory(ec) && isCard(e.path())) cards.push_back(e.path());
    if (cards.empty()) return {};
    return std::min_element(cards.begin(), cards.end())->string();
}

}  // namespace brks
