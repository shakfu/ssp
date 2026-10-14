#include "SapfServer.h"

namespace sapf {

std::string SapfServer::sendSync(const std::vector<std::string>& packets, int timeoutMs) {
    std::lock_guard<std::mutex> lock(sync_);
    std::string error;
    return world_.sendInBatches(packets, error, timeoutMs) ? "" : error;
}

}  // namespace sapf
