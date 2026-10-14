// Step 1 of docs/dev/sapf-plugin.md: the sapf-sc interpreter without a server. Loads the prelude,
// then evaluates the blocks of a file one by one, as the plugin's editor will, each with the same
// random seed and an empty stack. Defs and scores go to OUTDIR; the log on stdout and the files must
// match between the PC and the SSP.
//
// sapf-sc cannot interrupt an evaluation, and some examples never end. Each block therefore runs
// first in a forked child, limited to SAPF_BLOCK_SECONDS (default 20) and SAPF_BLOCK_MB more memory
// (default 1024); a block whose child fails is skipped. The parent then runs the blocks that passed,
// so definitions carry on.
//
// usage: sapf_harness PRELUDE FILE OUTDIR

#include "VM.hpp"
#include "sapf/Engine.hpp"

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

long statusKb(const char* field) {  // a /proc/self/status line, such as VmHWM
    std::ifstream in("/proc/self/status");
    std::string line;
    size_t n = std::string(field).size();
    while (std::getline(in, line))
        if (line.compare(0, n, field) == 0 && line[n] == ':') return std::atol(line.c_str() + n + 1);
    return -1;
}

// Runs `code` in a child; "" if it ends in time, else why not.
std::string probe(Thread& th, const std::string& code, unsigned seconds, long mb) {
    std::fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        if (!std::freopen("/dev/null", "w", stdout)) _exit(3);
        rlimit lim = { rlim_t(statusKb("VmSize") + mb * 1024) * 1024, RLIM_INFINITY };
        setrlimit(RLIMIT_AS, &lim);
        alarm(seconds);
        _exit(evaluate(th, code) ? 0 : 4);
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s PRELUDE FILE OUTDIR\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);  // a hung block shows in the log
    setenv("SAPF_SYNTHDEFS", argv[3], 1);
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
    config.sampleRate = 48000.;
    config.preludeFile = argv[1];
    SapfEngine& engine = GetSapfEngine();
    engine.configure(config);
    engine.initialize();
    Thread th;
    double init = since(t0);
    auto t1 = std::chrono::steady_clock::now();
    engine.loadPrelude(th);
    double prelude = since(t1);
    long preludeKb = statusKb("VmHWM");

    auto t2 = std::chrono::steady_clock::now();
    std::vector<std::string> bs = blocks(text.str());
    const char* env = getenv("SAPF_BLOCK_SECONDS");
    unsigned seconds = env ? unsigned(std::atoi(env)) : 20;
    env = getenv("SAPF_BLOCK_MB");
    long mb = env ? std::atol(env) : 1024;
    for (size_t i = 0; i < bs.size(); i++) {
        post("== block %zu\n", i + 1);
        auto tb = std::chrono::steady_clock::now();
        th.clearStack();
        std::string code = "1 setseed\n" + bs[i];
        std::string why = probe(th, code, seconds, mb);
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
        std::fprintf(stderr, "block %zu %.3f s\n", i + 1, since(tb));
    }
    std::fflush(stdout);
    std::fprintf(stderr, "init %.3f s, prelude %.3f s, %zu blocks %.3f s; peak RSS %ld kB after prelude, %ld kB at end\n",
                 init, prelude, bs.size(), since(t2), preludeKb, statusKb("VmHWM"));
    return 0;
}
