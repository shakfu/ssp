#pragma once

#include "ScWorld.h"
#include "Scsynth.hpp"

#include <mutex>

namespace sapf {

// sapf-sc's server: a ScWorld the plugin's audio callback runs. Bundle times and the ola scheduler
// follow the World's sample clock. Register it with setEmbeddedScsynth once the World is open.
class SapfServer : public ScsynthServer {
public:
    explicit SapfServer(scsy::ScWorld& world) : world_(world) {}

    std::string sendSync(const std::vector<std::string>& packets, int timeoutMs) override;
    void send(const std::string& packet) override { world_.send(packet); }
    std::string address() override { return "the plugin's scsynth"; }
    // >sf and >sfo: an NRT render needs a World of its own; not supported on the SSP
    std::string render(const std::string&, const std::string&, int, int64_t, double) override {
        return "rendering to a file is not supported in the plugin";
    }
    double now() override { return world_.now(); }

private:
    scsy::ScWorld& world_;
    std::mutex sync_;  // sapf syncs from its evaluation thread and each ola thread; the reply ring has one reader
};

}  // namespace sapf
