#include "KeyInput.h"

#include "Teletype.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>

#include "Keymap.h"
#endif

namespace ttyp {

KeyInput& KeyInput::get() {
    static KeyInput k;
    return k;
}

void KeyInput::acquire() {
    auto& k = get();
    std::lock_guard<std::mutex> lock(k.refLock_);
    if (k.refs_++ == 0) k.start();
}

void KeyInput::release() {
    auto& k = get();
    std::lock_guard<std::mutex> lock(k.refLock_);
    if (--k.refs_ == 0) k.stop();
}

void KeyInput::setTarget(Teletype* t) {
    auto& k = get();
    std::lock_guard<std::mutex> lock(k.lock_);
    if (k.target_ && k.target_ != t) k.target_->releaseAll();  // keys held while switching
    k.target_ = t;
}

void KeyInput::clearTarget(Teletype* t) {
    auto& k = get();
    std::lock_guard<std::mutex> lock(k.lock_);
    if (k.target_ == t) k.target_ = nullptr;
}

bool KeyInput::keyboardConnected() {
    return get().keyboard_.load();
}

void KeyInput::setKeyboardScan(bool on) {
    get().scan_ = on;
}

void KeyInput::deliver(uint8_t kind, uint8_t usage, uint8_t mod) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!target_) return;
    switch (kind) {
        case 0: target_->keyDown(usage); break;
        case 1: target_->keyUp(usage); break;
        case 2: target_->keyTap(usage, mod); break;
        case 3: target_->releaseAll(); break;
        default: break;  // unknown record kinds from the socket are ignored
    }
}

// refLock_ is held: start and stop run only from acquire and release
void KeyInput::start() {
#ifdef __linux__
    // TTYP_KEYBOARD=0: TCP only, for hosts whose keyboard the plugin must not grab
    if (const char* e = std::getenv("TTYP_KEYBOARD"); e && std::string(e) == "0") scan_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
#endif
}

void KeyInput::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

#ifdef __linux__

namespace {

bool testBit(const unsigned long* bits, unsigned bit) {
    constexpr unsigned W = 8 * sizeof(unsigned long);
    return (bits[bit / W] >> (bit % W)) & 1;
}

// The first event node with keys and LEDs: a keyboard, not the front panel (event0-5), and not a
// keyboard's extra System or Consumer Control nodes. Grabbed, so the console and SysRq do not see it.
int openKeyboard() {
    for (int i = 0; i < 32; i++) {
        std::string path = "/dev/input/event" + std::to_string(i);
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long ev[1] = {};
        unsigned long keys[(KEY_MAX + 1) / (8 * sizeof(unsigned long)) + 1] = {};
        bool kbd = ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) >= 0 && testBit(ev, EV_KEY) && testBit(ev, EV_LED) &&
                   ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && testBit(keys, KEY_A) &&
                   testBit(keys, KEY_ENTER);
        if (kbd && ioctl(fd, EVIOCGRAB, 1) == 0) return fd;
        ::close(fd);
    }
    return -1;
}

int openServer() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(KeyInput::PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // never the LAN: the port has no authentication
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || ::listen(fd, 1) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

void KeyInput::run() {
    using clock = std::chrono::steady_clock;
    int kbd = -1, cli = -1;
    int srv = openServer();
    uint8_t rec[4];
    unsigned have = 0;
    auto lastScan = clock::now() - std::chrono::seconds(1);

    auto dropKeyboard = [&] {
        ::close(kbd);
        kbd = -1;
        keyboard_ = false;
        deliver(3, 0, 0);
    };
    auto dropClient = [&] {
        ::close(cli);
        cli = -1;
        have = 0;
        deliver(3, 0, 0);
    };

    while (running_.load()) {
        if (kbd < 0 && scan_.load() && clock::now() - lastScan >= std::chrono::seconds(1)) {
            kbd = openKeyboard();
            keyboard_ = kbd >= 0;
            lastScan = clock::now();
        }
        pollfd fds[3];
        int n = 0, iKbd = -1, iSrv = -1, iCli = -1;
        if (kbd >= 0) fds[iKbd = n++] = { kbd, POLLIN, 0 };
        if (srv >= 0) fds[iSrv = n++] = { srv, POLLIN, 0 };
        if (cli >= 0) fds[iCli = n++] = { cli, POLLIN, 0 };
        if (::poll(fds, nfds_t(n), 100) <= 0) continue;

        if (iKbd >= 0 && fds[iKbd].revents) {
            input_event evs[64];
            ssize_t r = ::read(kbd, evs, sizeof evs);
            if (r < 0 && errno != EAGAIN) {
                dropKeyboard();  // ENODEV when unplugged
            } else {
                for (ssize_t i = 0; i < r / ssize_t(sizeof(input_event)); i++) {
                    // value 2 is autorepeat: the firmware repeats held keys itself
                    if (evs[i].type != EV_KEY || evs[i].value > 1) continue;
                    if (uint8_t u = hidUsage(evs[i].code)) deliver(evs[i].value ? 0 : 1, u, 0);
                }
            }
        }
        if (iSrv >= 0 && (fds[iSrv].revents & POLLIN)) {
            int c = ::accept4(srv, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (c >= 0) {
                if (cli >= 0) dropClient();  // one client: the newest
                cli = c;
            }
        }
        if (iCli >= 0 && fds[iCli].revents) {
            uint8_t buf[256];
            ssize_t r = ::recv(cli, buf, sizeof buf, 0);
            if (r == 0 || (r < 0 && errno != EAGAIN)) {
                dropClient();
            } else {
                for (ssize_t i = 0; i < r; i++) {
                    rec[have++] = buf[i];
                    if (have == 4) {
                        deliver(rec[0], rec[1], rec[2]);
                        have = 0;
                    }
                }
            }
        }
    }
    if (kbd >= 0) ::close(kbd);
    if (cli >= 0) ::close(cli);
    if (srv >= 0) ::close(srv);
    keyboard_ = false;
}

#else

void KeyInput::run() {}

#endif

}  // namespace ttyp
