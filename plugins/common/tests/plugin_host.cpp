// Loads an SSP plugin .so the way the SSP does, and runs commands against it; driven by
// test_engine_plugins.py. Uses only the Percussa API, so it needs no JUCE.
//
//   plugin_host <plugin.so> <command>...
//   prepare SR BLOCK     PluginInterface::prepare
//   set NAME VALUE       sets attribute NAME="..." in the saved state (a custom value, or a PARAM's
//                        value when NAME is a parameter id), then restores the state
//   in CH VALUE          holds input CH at VALUE
//   run SECONDS BLOCK    processes, sleeping 2 ms per block so the plugin's worker thread runs
//   wait SECONDS         sleeps, for work the worker does without audio (a compile, a file open)
//   level CH             prints "level CH <mean |x|> <last sample>" over the last run's final block
//   state                prints the state XML

#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "Percussa.h"

using namespace Percussa::SSP;

static std::string getXml(PluginInterface* p) {
    void* buf = nullptr;
    size_t size = 0;
    p->getState(&buf, &size);
    std::string xml = size > 8 ? std::string(static_cast<char*>(buf) + 8) : std::string();
    delete[] static_cast<char*>(buf);
    return xml;
}

// JUCE's copyXmlToBinary format: magic, length, text, NUL
static void putXml(PluginInterface* p, const std::string& xml) {
    std::vector<char> buf(8 + xml.size() + 1, 0);
    uint32_t magic = 0x21324356, len = uint32_t(xml.size() + 1);
    std::memcpy(buf.data(), &magic, 4);
    std::memcpy(buf.data() + 4, &len, 4);
    std::memcpy(buf.data() + 8, xml.c_str(), xml.size());
    p->setState(buf.data(), buf.size());
}

static bool setAttr(std::string& xml, const std::string& name, const std::string& value) {
    // a parameter: <PARAM id="NAME" value="..."/>
    std::string param = "id=\"" + name + "\" value=\"";
    size_t at = xml.find(param);
    if (at != std::string::npos) {
        at += param.size();
    } else {
        std::string attr = " " + name + "=\"";
        at = xml.find(attr);
        if (at == std::string::npos) return false;
        at += attr.size();
    }
    size_t end = xml.find('"', at);
    xml.replace(at, end - at, value);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    void* so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (so == nullptr) {
        std::fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    auto desc = reinterpret_cast<DescriptorFun>(dlsym(so, createDescriptorName))();
    auto* p = reinterpret_cast<InstantiateFun>(dlsym(so, createInstanceName))();
    int nIn = int(desc->inputChannelNames.size()), nOut = int(desc->outputChannelNames.size());
    int nCh = std::max(nIn, nOut);
    std::printf("io %d %d\n", nIn, nOut);

    std::vector<float> inValue(static_cast<size_t>(nIn), 0.0f);
    std::vector<std::vector<float>> buf{ static_cast<size_t>(nCh) };
    std::vector<float*> ptrs{ static_cast<size_t>(nCh), nullptr };
    int lastBlock = 0;

    for (int a = 2; a < argc; a++) {
        std::string cmd = argv[a];
        if (cmd == "prepare") {
            p->prepare(std::atof(argv[a + 1]), std::atoi(argv[a + 2]));
            a += 2;
        } else if (cmd == "set") {
            std::string xml = getXml(p);
            if (!setAttr(xml, argv[a + 1], argv[a + 2])) {
                std::fprintf(stderr, "no %s in state\n", argv[a + 1]);
                return 1;
            }
            putXml(p, xml);
            a += 2;
        } else if (cmd == "in") {
            inValue[size_t(std::atoi(argv[a + 1]))] = float(std::atof(argv[a + 2]));
            a += 2;
        } else if (cmd == "run") {
            double secs = std::atof(argv[a + 1]);
            int block = std::atoi(argv[a + 2]);
            a += 2;
            for (int c = 0; c < nCh; c++) {
                buf[size_t(c)].assign(size_t(block), 0.0f);
                ptrs[size_t(c)] = buf[size_t(c)].data();
            }
            int blocks = int(secs * 48000.0 / block);
            for (int b = 0; b < blocks; b++) {
                for (int c = 0; c < nCh; c++)
                    std::fill(buf[size_t(c)].begin(), buf[size_t(c)].end(), c < nIn ? inValue[size_t(c)] : 0.0f);
                p->process(ptrs.data(), nCh, block);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            lastBlock = block;
        } else if (cmd == "wait") {
            std::this_thread::sleep_for(std::chrono::duration<double>(std::atof(argv[a + 1])));
            a += 1;
        } else if (cmd == "level") {
            int c = std::atoi(argv[a + 1]);
            a += 1;
            double acc = 0.0;
            for (int f = 0; f < lastBlock; f++) acc += std::fabs(buf[size_t(c)][size_t(f)]);
            std::printf("level %d %g %g\n", c, acc / std::max(1, lastBlock),
                        lastBlock ? buf[size_t(c)][size_t(lastBlock - 1)] : 0.0f);
        } else if (cmd == "state") {
            std::printf("state %s\n", getXml(p).c_str());
        } else {
            std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
            return 2;
        }
        std::fflush(stdout);
    }
    delete p;
    delete desc;
    return 0;
}
