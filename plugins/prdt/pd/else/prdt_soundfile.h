// Pd's sound-file reader for prdt's [sfload], [sfinfo] and [play.file~], in place of ELSE's ffmpeg: WAV, AIFF, CAF
// and NeXT files with 16 or 24-bit integer or 32 or 64-bit float samples. Safe on a thread with no Pd instance.
#pragma once
#include "d_soundfile.h"

// opens path and reads its header; on success, 1 and sf's fd at the first sample frame, else 0 and errno set
int prdt_sf_open(const char* path, t_soundfile* sf);

// the file's length in sample frames
size_t prdt_sf_frames(const t_soundfile* sf);

// reads up to nframes frames into dst, interleaved in dstch channels: the file's channels past dstch are dropped,
// and dst's past the file's zeroed. Returns the frames read, 0 at the end, or -1 on an error.
long prdt_sf_read(t_soundfile* sf, float* dst, int dstch, size_t nframes);
