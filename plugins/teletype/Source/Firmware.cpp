#include "Firmware.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace ttyp {

namespace {

bool writeAll(int fd, const void* data, size_t size) {
    auto p = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t n = ::write(fd, p, size);
        if (n <= 0) return false;
        p += n;
        size -= size_t(n);
    }
    return true;
}

// Linux: an anonymous memory file, so nothing reaches the card or /tmp. Its fd stays open while
// loaded: glibc's dlopen returns an already loaded object of the same name, and a closed fd's number,
// so its /proc/self/fd path, is reused by the next instance. Elsewhere: a temp file, unlinked once
// loaded.
void* openImage(const void* image, size_t size, int& keepFd, std::string& err) {
#ifdef __linux__
    int fd = memfd_create("ttyp_firmware", MFD_CLOEXEC);
    if (fd < 0) {
        err = std::string("memfd_create: ") + strerror(errno);
        return nullptr;
    }
    void* h = nullptr;
    if (writeAll(fd, image, size)) {
        std::string path = "/proc/self/fd/" + std::to_string(fd);
        h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) err = dlerror();
    } else {
        err = "cannot write firmware image";
    }
    if (h)
        keepFd = fd;
    else
        ::close(fd);
    return h;
#else
    char path[] = "/tmp/ttyp_firmware_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        err = std::string("mkstemp: ") + strerror(errno);
        return nullptr;
    }
    void* h = nullptr;
    if (writeAll(fd, image, size)) {
        h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) err = dlerror();
    } else {
        err = "cannot write firmware image";
    }
    ::close(fd);
    ::unlink(path);
    return h;
#endif
}

}  // namespace

std::unique_ptr<Firmware> Firmware::load(const void* image, size_t size, std::string& err) {
    std::unique_ptr<Firmware> fw(new Firmware);
    fw->handle_ = openImage(image, size, fw->fd_, err);
    if (!fw->handle_) return nullptr;
    bool ok = true;
    auto sym = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(fw->handle_, name));
        if (!fn) {
            err = std::string("missing ") + name;
            ok = false;
        }
    };
    sym(fw->init, "hardware_init");
    sym(fw->step, "hardware_step");
    sym(fw->triggerInterrupt, "hardware_triggerInterrupt");
    sym(fw->getGPIO, "hardware_getGPIO");
    sym(fw->setGPIO, "hardware_setGPIO");
    sym(fw->setADC, "hardware_setADC");
    sym(fw->getDAC, "hardware_getDAC");
    sym(fw->readNVRAM, "hardware_readNVRAM");
    sym(fw->writeNVRAM, "hardware_writeNVRAM");
    sym(fw->readVRAM, "hardware_readVRAM");
    sym(fw->writeVRAM, "hardware_writeVRAM");
    sym(fw->setScreenBuffer, "hardware_setScreenBuffer");
    sym(fw->hidConnect, "hardware_hidConnect");
    sym(fw->hidMessage, "hardware_hidMessage");
    sym(fw->hidFrame, "hardware_hidFrame");
    sym(fw->serializePreset, "hardware_serializePreset");
    sym(fw->deserializePreset, "hardware_deserializePreset");
    if (!ok) return nullptr;  // the destructor closes the handle
    return fw;
}

Firmware::~Firmware() {
    if (handle_) dlclose(handle_);
    if (fd_ >= 0) ::close(fd_);
}

}  // namespace ttyp
