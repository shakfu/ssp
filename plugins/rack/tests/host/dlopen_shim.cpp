// Linked with -Wl,--wrap=dlopen: sanitizers abort on RTLD_DEEPBIND, which Module::loadModule
// passes, so strip it in tests. Production code is unchanged.

#include <dlfcn.h>

extern "C" void* __real_dlopen(const char* file, int mode);
extern "C" void* __wrap_dlopen(const char* file, int mode) {
    return __real_dlopen(file, mode & ~RTLD_DEEPBIND);
}
