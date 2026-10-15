// Step 1 of docs/dev/sapf-plugin.md: the sapf interpreter and its host-driven audio, without a
// plugin. Loads the prelude, then evaluates the blocks of a file one by one, as the plugin's editor
// will, each with the same random seed and an empty stack. A block that plays is rendered for
// SAPF_RENDER_SECONDS (default 2) in 128-frame host blocks, to OUTDIR/blockNNN.f32 (stereo,
// interleaved, native float32). The log on stdout and the files must match between the PC and the
// SSP; stderr times each block's evaluation and render.
//
// Each block runs first in a forked child, limited to SAPF_BLOCK_SECONDS (default 20) and
// SAPF_BLOCK_MB more memory (default 1024); a block whose child fails is skipped. The parent then
// runs the blocks that passed, so definitions carry on.
//
// usage: sapf_harness PRELUDE FILE OUTDIR

#include "VM.hpp"
#include "sapf/Engine.hpp"
#include "sapf/MaxAudioBackend.hpp"

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const int kHostFrames = 128;
const int kChannels = 2;
const double kSampleRate = 48000.;

// A blank line ends a block only outside brackets: some examples have blank lines inside one.
std::vector<std::string> blocks(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line, block;
    int depth = 0;
    bool quoted = false;  // a string, which can span lines
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            if (depth > 0 || quoted) continue;
            if (!block.empty()) out.push_back(block);
            block.clear();
            depth = 0;
            continue;
        }
        for (char c : line) {
            if (quoted) {
                quoted = c != '"';
            } else if (c == ';') {
                break;  // a comment, to the line's end
            } else if (c == '"') {
                quoted = true;
            } else if (c == '(' || c == '[' || c == '{') {
                depth++;
            } else if (c == ')' || c == ']' || c == '}') {
                depth--;
            }
        }
        block += line + "\n";
    }
    if (!block.empty()) out.push_back(block);
    return out;
}

// What the REPL posts for an evaluation error. False when memory ran out.
bool evaluate(Thread& th, const std::string& code) {
    try {
        P<Fun> fun;
        if (th.compile(code.c_str(), fun, true)) fun->runREPL(th);
    } catch (V& v) {
        post("error: ");
        v.print(th);
        post("\n");
    } catch (int err) {
        if (err <= -1000 && err > -1000 - kNumErrors)
            post("error: %s\n", errString[-1000 - err]);
        else
            post("error: %d\n", err);
    } catch (std::bad_alloc&) {
        post("not enough memory\n");
        return false;
    } catch (...) {
        post("unknown error\n");
    }
    return true;
}

double cpuSeconds() {
    timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

struct Render {
    std::vector<float> frames;  // interleaved
    std::vector<double> costs;  // CPU seconds per host block
    double cpu = 0.;            // seconds
    double worstBlock = 0.;     // the slowest host block, seconds
};

const int kFifoDepths[] = { 1, 2, 4, 8 };

// Host blocks that would be late if a thread of its own rendered into a FIFO `depth` host blocks
// deep, on a core of its own: block i may start once block i - depth has been played, and must end
// before block i is due, depth blocks after the callback asks for it.
int lateBlocks(const std::vector<double>& costs, int depth) {
    const double period = kHostFrames / kSampleRate;
    double done = 0.;
    int late = 0;
    for (size_t i = 0; i < costs.size(); i++) {
        double start = i >= size_t(depth) ? std::max(done, i * period) : done;
        done = start + costs[i];
        if (done > (i + depth) * period) late++;
    }
    return late;
}

// Renders the players `play` started, then stops them. Empty when the output is silent.
Render render(MaxAudioBackend& backend, double seconds) {
    Render r;
    float bufs[kChannels][kHostFrames];
    float* outs[kChannels] = { bufs[0], bufs[1] };
    bool sound = false;
    int total = int(seconds * kSampleRate) / kHostFrames * kHostFrames;
    r.frames.reserve(size_t(total) * kChannels);
    for (int t = 0; t < total; t += kHostFrames) {
        double c0 = cpuSeconds();
        backend.render(outs, kChannels, kHostFrames);
        double dt = cpuSeconds() - c0;
        r.cpu += dt;
        r.costs.push_back(dt);
        if (dt > r.worstBlock) r.worstBlock = dt;
        for (int i = 0; i < kHostFrames; i++)
            for (int c = 0; c < kChannels; c++) {
                r.frames.push_back(bufs[c][i]);
                sound = sound || bufs[c][i] != 0.f;
            }
    }
    backend.stopAll();
    if (!sound) r.frames.clear();
    return r;
}

long statusKb(const char* field) {  // a /proc/self/status line, such as VmHWM
    std::ifstream in("/proc/self/status");
    std::string line;
    size_t n = std::string(field).size();
    while (std::getline(in, line))
        if (line.compare(0, n, field) == 0 && line[n] == ':') return std::atol(line.c_str() + n + 1);
    return -1;
}

// Runs `code` and its render in a child; "" if both end in time, else why not.
std::string probe(Thread& th, MaxAudioBackend& backend, const std::string& code, double seconds,
                  unsigned limit, long mb) {
    std::fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        if (!std::freopen("/dev/null", "w", stdout)) _exit(3);
        rlimit lim = { rlim_t(statusKb("VmSize") + mb * 1024) * 1024, RLIM_INFINITY };
        setrlimit(RLIMIT_AS, &lim);
        alarm(limit);
        if (!evaluate(th, code)) _exit(4);
        try {
            render(backend, seconds);
        } catch (std::bad_alloc&) {
            _exit(4);
        }
        _exit(0);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) return "fork failed";
    if (WIFSIGNALED(status)) return WTERMSIG(status) == SIGALRM ? "timeout" : "signal " + std::to_string(WTERMSIG(status));
    if (WEXITSTATUS(status) == 4) return "out of memory";
    return WEXITSTATUS(status) ? "exit " + std::to_string(WEXITSTATUS(status)) : "";
}

double since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

double envDouble(const char* name, double fallback) {
    const char* v = getenv(name);
    return v ? std::atof(v) : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s PRELUDE FILE OUTDIR\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);  // a hung block shows in the log
    setenv("SAPF_RECORDINGS", argv[3], 1);
    std::ifstream in(argv[2]);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    std::stringstream text;
    text << in.rdbuf();

    auto t0 = std::chrono::steady_clock::now();
    SapfEngineConfig config;
    config.sampleRate = kSampleRate;
    config.blockSize = int(envDouble("SAPF_BLOCK_FRAMES", kDefaultZBlockSize));
    config.preludeFile = argv[1];
    config.logFile = "/dev/null";
    config.enableManta = false;
    config.openFiles = false;
    SapfEngine& engine = GetSapfEngine();
    engine.configure(config);
    engine.initialize();
    MaxAudioBackend& backend = InstallMaxMSPBackend();
    RegisterMaxAudioPrimitives();
    Thread th;
    double init = since(t0);
    auto t1 = std::chrono::steady_clock::now();
    engine.loadPrelude(th);
    double prelude = since(t1);
    long preludeKb = statusKb("VmHWM");

    auto t2 = std::chrono::steady_clock::now();
    std::vector<std::string> bs = blocks(text.str());
    double seconds = envDouble("SAPF_RENDER_SECONDS", 2.);
    unsigned limit = unsigned(envDouble("SAPF_BLOCK_SECONDS", 20.));
    long mb = long(envDouble("SAPF_BLOCK_MB", 1024.));
    double worstLoad = 0.;
    for (size_t i = 0; i < bs.size(); i++) {
        post("== block %zu\n", i + 1);
        auto tb = std::chrono::steady_clock::now();
        th.clearStack();
        std::string code = "1 setseed\n" + bs[i];
        std::string why = probe(th, backend, code, seconds, limit, mb);
        if (!why.empty()) {
            post("skipped: %s\n", why.c_str());
            std::fprintf(stderr, "block %zu skipped: %s\n", i + 1, why.c_str());
            continue;
        }
        evaluate(th, code);
        if (th.stackDepth()) {
            post("left on stack: %zu\n", th.stackDepth());
            th.clearStack();
        }
        double eval = since(tb);
        Render r = render(backend, seconds);
        if (r.frames.empty()) {
            std::fprintf(stderr, "block %zu eval %.3f s\n", i + 1, eval);
            continue;
        }
        char name[32];
        std::snprintf(name, sizeof name, "block%03zu.f32", i + 1);
        std::ofstream(std::string(argv[3]) + "/" + name, std::ios::binary)
            .write(reinterpret_cast<const char*>(r.frames.data()), std::streamsize(r.frames.size() * sizeof(float)));
        post("rendered '%s'\n", name);
        // load: CPU time per second of audio; worst: the slowest host block over its duration
        double load = r.cpu / seconds, worst = r.worstBlock / (kHostFrames / kSampleRate);
        if (load > worstLoad) worstLoad = load;
        std::string fifo;
        for (int d : kFifoDepths) fifo += " " + std::to_string(lateBlocks(r.costs, d));
        int over = 0;
        for (double c : r.costs) over += c > kHostFrames / kSampleRate;
        // over: host blocks over their time in the callback; fifo: late blocks at each depth in kFifoDepths
        std::fprintf(stderr, "block %zu eval %.3f s, render load %.3f, worst block %.2f, over %d, fifo%s\n", i + 1, eval,
                     load, worst, over, fifo.c_str());
    }
    std::fflush(stdout);
    std::fprintf(stderr, "init %.3f s, prelude %.3f s, %zu blocks %.3f s, worst render load %.3f; "
                 "peak RSS %ld kB after prelude, %ld kB at end\n",
                 init, prelude, bs.size(), since(t2), worstLoad, preludeKb, statusKb("VmHWM"));
    return 0;
}
