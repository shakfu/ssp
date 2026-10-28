#include "PdEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

extern "C" {
#include "m_pd.h"
#include "m_imp.h"
#include "g_canvas.h"
#include "s_stuff.h"
#include "z_libpd.h"
#include "util/z_print_util.h"

void pd_doloadbang(void);
int sys_pollgui(void);
int sys_trylock(void);
// Pd's extra/ objects, built without pd~ (pd/CMakeLists.txt)
void bob_tilde_setup(void);
void bonk_tilde_setup(void);
void choice_setup(void);
void fiddle_tilde_setup(void);
void loop_tilde_setup(void);
void lrshift_tilde_setup(void);
void pique_setup(void);
void sigmund_tilde_setup(void);
void stdout_setup(void);
void cyclone_setup(void);  // external/pd-cyclone: every object, compiled in
}

namespace prdt {

// A saw through a lowpass, so the module sounds with nothing patched.
static const char* BUILTIN = R"pd(#N canvas 0 50 450 300 12;
#X obj 10 10 r pitch;
#X obj 10 40 phasor~;
#X obj 10 70 -~ 0.5;
#X obj 10 100 lop~ 1500;
#X obj 120 10 r cutoff;
#X obj 10 130 *~ 0.045;
#X obj 220 10 r level;
#X obj 220 40 * 0.255;
#X obj 220 70 + 0.045;
#X obj 10 160 dac~ 1 2;
#X text 10 200 @p1 pitch 110 880 Hz log;
#X text 10 220 @p2 level;
#X text 10 240 @p3 cutoff 1500 12000 Hz log;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 4 0 3 1;
#X connect 3 0 5 0;
#X connect 6 0 7 0;
#X connect 7 0 8 0;
#X connect 8 0 5 1;
#X connect 5 0 9 0;
#X connect 5 0 9 1;
)pd";

struct PdEngine::Instance {
    PdEngine* engine = nullptr;
    t_pdinstance* pd = nullptr;
    t_pd* patch = nullptr;
    std::string* log = nullptr;  // set on the worker: prints go here, not to the audio queue
    t_symbol* recv[PARAMS] = {};
    float applied[PARAMS];
    int pos = TICK;  // frame within the current tick
};

static void quiet(const char*) {}

// At dlclose, or exit: Pd's process-wide state, which pd_freeprocess() frees only once every module's
// instance is gone. Synthor unloads prdt.so after its last module, so a reload starts clean.
static struct Teardown {
    bool initialised = false;
    ~Teardown() {
        if (initialised) pd_freeprocess();
    }
} teardown;

// Once per process (per load of the .so): libpd's main instance and the classes all instances share
static void initPd() {
    static std::once_flag once;
    std::call_once(once, [] {
        teardown.initialised = true;
        libpd_set_printhook(quiet);  // before libpd_init: the default for instances without a hook
        libpd_init();
        bob_tilde_setup();
        bonk_tilde_setup();
        choice_setup();
        fiddle_tilde_setup();
        loop_tilde_setup();
        lrshift_tilde_setup();
        pique_setup();
        sigmund_tilde_setup();
        stdout_setup();
        cyclone_setup();
    });
}

// binbuf_evalfile and glob_evalfile, from text in place of a file. With sys_lock() held: the global
// lock libpd_openfile adds is only needed to create classes, which Pd's loader takes for itself.
static t_pd* openText(const std::string& text, const char* name, const char* dir) {
    t_binbuf* b = binbuf_new();
    binbuf_text(b, text.data(), text.size());
    int dsp = canvas_suspend_dsp();
    t_pd* boundx = s__X.s_thing;
    s__X.s_thing = nullptr;
    glob_setfilename(nullptr, gensym(name), gensym(dir));
    t_symbol* a = gensym("#A");
    t_pd *bounda = a->s_thing, *boundn = s__N.s_thing;
    a->s_thing = nullptr;
    s__N.s_thing = &pd_canvasmaker;
    binbuf_eval(b, 0, 0, nullptr);
    if (s__X.s_thing && *s__X.s_thing == canvas_class) canvas_initbang(reinterpret_cast<t_canvas*>(s__X.s_thing));
    a->s_thing = bounda;
    s__N.s_thing = boundn;
    glob_setfilename(nullptr, &s_, &s_);
    binbuf_free(b);
    t_pd* x = nullptr;
    while (x != s__X.s_thing && s__X.s_thing) {
        x = s__X.s_thing;
        vmess(x, gensym("pop"), const_cast<char*>("i"), 1);
    }
    pd_doloadbang();
    canvas_resume_dsp(dsp);
    s__X.s_thing = boundx;
    return x;
}

PdEngine::~PdEngine() {
    destroy(gate_.take());
}

void PdEngine::destroy(Instance* i) {
    if (i == nullptr) return;
    std::string closing;  // closebang prints, unread
    i->log = &closing;
    libpd_set_instance(i->pd);
    if (i->patch) libpd_closefile(i->patch);
    // pdinstance_free (m_class.c) frees neither sys_setchsr's sound buffers, the path lists, the names of
    // the built-in symbols, nor the instance itself: 4.6 kB per load, found by LeakSanitizer
    freebytes(STUFF->st_soundin, size_t(STUFF->st_inchannels) * DEFDACBLKSIZE * sizeof(t_sample));
    freebytes(STUFF->st_soundout, size_t(STUFF->st_outchannels) * DEFDACBLKSIZE * sizeof(t_sample));
    STUFF->st_soundin = STUFF->st_soundout = nullptr;
    for (t_namelist** l : { &STUFF->st_externlist, &STUFF->st_searchpath, &STUFF->st_staticpath,
                            &STUFF->st_helppath, &STUFF->st_temppath }) {
        namelist_free(*l);
        *l = nullptr;
    }
    t_pdinstance* pd = i->pd;
    libpd_free_instance(pd);
    for (t_symbol* s : { &pd->pd_s_pointer, &pd->pd_s_float, &pd->pd_s_symbol, &pd->pd_s_bang, &pd->pd_s_list,
                         &pd->pd_s_anything, &pd->pd_s_signal, &pd->pd_s__N, &pd->pd_s__X, &pd->pd_s_x,
                         &pd->pd_s_y, &pd->pd_s_ })
        freebytes(const_cast<char*>(s->s_name), std::strlen(s->s_name) + 1);
    freebytes(pd, sizeof(*pd));
    delete i;
}

const char* PdEngine::builtin() const {
    return BUILTIN;
}

// a whole line, from libpd's concatenator, on the thread that printed it
void PdEngine::print(const char* s) {
    auto* i = static_cast<Instance*>(libpd_get_instancedata());
    if (i == nullptr) return;
    if (i->log) {
        *i->log += s;
        *i->log += "\n";
        return;
    }
    Line line;
    std::strncpy(line.text, s, sizeof(line.text) - 1);
    line.text[sizeof(line.text) - 1] = '\0';
    i->engine->printed_.try_enqueue(line);
}

void PdEngine::addConsole(const std::string& line) const {
    std::lock_guard<std::mutex> lock(consoleLock_);
    console_.push_back(line);
    while (console_.size() > CONSOLE) console_.pop_front();
}

std::string PdEngine::status() const {
    Line line;
    while (printed_.try_dequeue(line)) addConsole(line.text);
    std::string s;
    unsigned n = notes_.load(std::memory_order_relaxed);
    if (n) s = "MIDI notes: " + std::to_string(n);
    std::lock_guard<std::mutex> lock(consoleLock_);
    for (auto& l : console_) s += (s.empty() ? "" : "\n") + l;
    return s;
}

void PdEngine::midi(uint8_t status, uint8_t d1, uint8_t d2) {
    if ((status & 0xF0) == 0x90 && d2 > 0) notes_.fetch_add(1, std::memory_order_relaxed);
    midi_.try_enqueue(Midi{ { status, d1, d2 } });
}

// Pd saves a comment as "#X text X Y words...;", wrapped across lines, with commas and semicolons
// escaped and an optional ", f WIDTH" for its box width.
PdEngine::Specs PdEngine::patchSpecs(const std::string& text) {
    std::string lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = at;
        while (end < text.size() && !(text[end] == ';' && (end == 0 || text[end - 1] != '\\'))) end++;
        std::istringstream words(text.substr(at, end - at));
        at = end + 1;
        std::string w, x, y;
        if (!(words >> w) || w != "#X" || !(words >> w) || w != "text" || !(words >> x >> y)) continue;
        std::string comment;
        while (words >> w) comment += (comment.empty() ? "" : " ") + w;
        size_t width = comment.rfind(" \\, f ");
        if (width != std::string::npos) comment.erase(width);
        if (comment.find("@p") != std::string::npos) lines += "; " + comment + "\n";
    }
    return parseSpecs(lines);
}

bool PdEngine::compile(const std::string& text, const std::string& path, std::string& error) {
    if (text.find("#N canvas") == std::string::npos) {
        error = "not a Pd patch: no #N canvas";
        return false;
    }
    initPd();
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    std::string name = path.empty() ? "builtin.pd" : path.substr(slash + 1);
    const Specs specs = patchSpecs(text);

    std::string log;
    auto* i = new Instance;
    i->engine = this;
    i->log = &log;
    i->pd = libpd_new_instance();
    libpd_set_instance(i->pd);
    libpd_set_instancedata(i, nullptr);
    libpd_set_printhook(libpd_print_concatenator);
    libpd_set_concatenated_printhook(print);
    libpd_init_audio(CHANNELS, CHANNELS, int(sampleRate()));
    for (auto& d : searchPath_) libpd_add_to_search_path(d.c_str());
    libpd_start_message(1);
    libpd_add_float(1.0f);
    libpd_finish_message("pd", "dsp");

    sys_lock();
    i->patch = openText(text, name.c_str(), dir.c_str());
    for (int p = 0; p < PARAMS; p++) i->recv[p] = gensym(specs[size_t(p)].label.empty() ? paramName(p) : specs[size_t(p)].label.c_str());
    sys_unlock();

    if (i->patch == nullptr || log.find("couldn't create") != std::string::npos) {
        // Pd prints the object's text, then "... couldn't create" as an error
        std::istringstream lines(log);
        std::string line, previous;
        for (; std::getline(lines, line); previous = line) {
            if (line.find("couldn't create") != std::string::npos) line = previous + " " + line.substr(line.find("..."));
            else if (line.rfind("error", 0) != 0) continue;
            error += (error.empty() ? "" : "\n") + line;
        }
        if (error.empty()) error = "the patch made no canvas";
        destroy(i);
        return false;
    }
    std::istringstream lines(log);
    std::string line;
    while (std::getline(lines, line)) addConsole(line);
    i->log = nullptr;
    std::fill(std::begin(i->applied), std::end(i->applied), std::nanf(""));  // never equal: all sent

    Instance* old = gate_.take();
    gate_.publish(i);
    destroy(old);
    return true;
}

void PdEngine::process(const float* const* in, float* const* out, int n) {
    Instance* i = gate_.begin();
    if (i) libpd_set_instance(i->pd);
    if (i == nullptr || sys_trylock() != 0) {
        for (int c = 0; c < CHANNELS; c++) std::fill(out[c], out[c] + n, 0.0f);
        gate_.end();
        return;
    }
    pd_this->pd_islocked = 1;  // as sys_lock() sets it; pd_globallock() checks it
    Midi m;
    while (midi_.try_dequeue(m)) {
        int ch = m.b[0] & 0x0F, d1 = m.b[1] & 0x7F, d2 = m.b[2] & 0x7F;
        switch (m.b[0] & 0xF0) {
            case 0x80: inmidi_noteon(0, ch, d1, 0); break;
            case 0x90: inmidi_noteon(0, ch, d1, d2); break;
            case 0xA0: inmidi_polyaftertouch(0, ch, d1, d2); break;
            case 0xB0: inmidi_controlchange(0, ch, d1, d2); break;
            case 0xC0: inmidi_programchange(0, ch, d1); break;
            case 0xD0: inmidi_aftertouch(0, ch, d1); break;
            case 0xE0: inmidi_pitchbend(0, ch, d1 | (d2 << 7)); break;
            default: break;
        }
    }
    for (int p = 0; p < PARAMS; p++) {
        float v = value(p);
        if (v == i->applied[p]) continue;
        i->applied[p] = v;
        if (i->recv[p]->s_thing) pd_float(i->recv[p]->s_thing, v);
    }
    sys_pollgui();
    t_sample* sin = STUFF->st_soundin;
    t_sample* sout = STUFF->st_soundout;
    for (int f = 0; f < n; f++) {
        if (i->pos == TICK) {
            std::memset(sout, 0, sizeof(t_sample) * CHANNELS * TICK);
            sched_tick();
            i->pos = 0;
        }
        for (int c = 0; c < CHANNELS; c++) {
            sin[c * TICK + i->pos] = in[c][f];
            out[c][f] = sout[c * TICK + i->pos];
        }
        i->pos++;
    }
    sys_unlock();
    gate_.end();
}

}  // namespace prdt
