// prdt_soundfile.h's reader, on d_soundfile.c's header parsers
#include <fcntl.h>

#include "prdt_soundfile.h"

int open_soundfile_via_fd(int fd, t_soundfile* sf, size_t skipframes);  // d_soundfile.c, not in its header

#define SCALE (1. / (1024. * 1024. * 1024. * 2.))  // as d_soundfile.c
#define CHUNK 1024                                 // frames per read

int prdt_sf_open(const char* path, t_soundfile* sf) {
    soundfile_clear(sf);
    sf->sf_headersize = -1;  // read the header; 0 or more means a raw file of a given format
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    return open_soundfile_via_fd(fd, sf, 0) >= 0;
}

size_t prdt_sf_frames(const t_soundfile* sf) {
    return sf->sf_bytesperframe > 0 ? (size_t)sf->sf_bytelimit / sf->sf_bytesperframe : 0;
}

static float sample(const t_soundfile* sf, const unsigned char* p) {
    int big = sf->sf_bigendian;
    switch (sf->sf_bytespersample) {
        // unsigned shifts: a top byte of 128 or more shifted into an int's sign bit is undefined
        case 2: return SCALE * (int32_t)(big ? ((uint32_t)p[0] << 24 | p[1] << 16) : ((uint32_t)p[1] << 24 | p[0] << 16));
        case 3:
            return SCALE * (int32_t)(big ? ((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8)
                                         : ((uint32_t)p[2] << 24 | p[1] << 16 | p[0] << 8));
        case 4: {
            union { uint32_t u; float f; } a;
            a.u = big ? ((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3])
                      : ((uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]);
            return a.f;
        }
        case 8: {
            union { uint64_t u; double d; } a;
            a.u = 0;
            for (int i = 0; i < 8; i++) a.u |= (uint64_t)p[big ? i : 7 - i] << (8 * (7 - i));
            return (float)a.d;
        }
        default: return 0;
    }
}

long prdt_sf_read(t_soundfile* sf, float* dst, int dstch, size_t nframes) {
    unsigned char buf[CHUNK * 8 * 4];  // CHUNK frames of up to 4 channels at 8 bytes, or fewer frames of more
    size_t perread = sizeof(buf) / sf->sf_bytesperframe, done = 0;
    int nch = sf->sf_nchannels < dstch ? sf->sf_nchannels : dstch;
    if (perread == 0) return -1;
    while (done < nframes && sf->sf_bytelimit > 0) {
        size_t want = nframes - done;
        if (want > perread) want = perread;
        if ((ssize_t)(want * sf->sf_bytesperframe) > sf->sf_bytelimit) want = sf->sf_bytelimit / sf->sf_bytesperframe;
        if (want == 0) break;
        ssize_t got = read(sf->sf_fd, buf, want * sf->sf_bytesperframe);
        if (got < 0) return -1;
        size_t frames = got / sf->sf_bytesperframe;
        if (frames == 0) break;
        sf->sf_bytelimit -= frames * sf->sf_bytesperframe;
        for (size_t f = 0; f < frames; f++) {
            const unsigned char* p = buf + f * sf->sf_bytesperframe;
            float* d = dst + (done + f) * dstch;
            int c = 0;
            for (; c < nch; c++) d[c] = sample(sf, p + c * sf->sf_bytespersample);
            for (; c < dstch; c++) d[c] = 0;
        }
        done += frames;
    }
    return (long)done;
}
