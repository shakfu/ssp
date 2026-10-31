// ELSE's [play.file~] (Tim Schoen and Alexandre Porres) on Pd's sound-file reader, not ffmpeg: the same messages,
// flags and outlets, and m3u playlists, for WAV, AIFF, CAF and NeXT files. ELSE decoded in the perform routine; here
// a reader thread per object fills a ring the perform routine drains, as [readsf~] does. Pd's thread finds paths and
// reads headers; the reader only reads samples, as it has no Pd instance. 'open' with no file has no panel to open.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "g_canvas.h"
#include "prdt_soundfile.h"

#define RING 16384  // frames
#define CHUNK 1024  // frames per read
#define ENDS 16     // file ends the perform routine has not reached

typedef struct _playlist {
    t_symbol** arr;  // m3u list of tracks
    t_symbol* dir;   // starting directory
    int size;        // size of the list
    int max;         // size of the memory allocation
} t_playlist;

typedef struct _playfile {
    t_object x_obj;
    t_sample** x_outs;
    unsigned char x_play;  // play/pause toggle
    unsigned char x_open;  // true when a file has been successfully opened
    int x_nch;             // output channels, and the ring's width
    int x_mc;
    int x_n;
    int x_loop;
    int x_sr;              // the file's
    int x_file_nch;
    t_outlet* x_bang_out;
    t_playlist x_plist;
    t_canvas* x_canvas;
    t_symbol* x_play_next;
    // shared with the reader: x_lock guards the request, the generation and the ring's allocation; the indexes are
    // atomic, as the perform routine reads the ring without the lock
    pthread_mutex_t x_lock;
    pthread_cond_t x_cond;
    pthread_t x_thread;
    int x_quit;
    int x_gen;                 // bumped by each flush; a read finished under an older one is dropped
    char x_path[MAXPDSTRING];  // the file to open, set with x_req_open
    int x_req_open;
    long x_req_seek;           // a frame to seek to, or -1
    float* x_ring;             // RING frames of x_nch
    uint64_t x_w, x_r;         // frames written and read
    uint64_t x_ends[ENDS];     // write positions where the file ended
    unsigned x_ends_w, x_ends_r;
    int x_eof;                 // the reader stopped at the end, not looping
    int x_outch;               // the outputs' channels at the last dsp
    int x_ended;               // file ends the perform routine passed, for the clock to bang
    t_clock* x_endclock;
} t_playfile;

static t_class* playfile_class;

// ---------------------- playlist (ELSE's) ------------------------

typedef const char* err_t;

static __thread int depth;  // prevents self-referencing m3u's from causing an infinite loop; prdt: per thread

#define M3U_MAIN(deeper, increment)                                            \
    char line[MAXPDSTRING];                                                    \
    while (fgets(line, MAXPDSTRING, fp) != NULL) {                             \
        line[strcspn(line, "\r\n")] = '\0';                                    \
        int isabs = (line[0] == '/');                                          \
        if ((isabs ? 0 : dlen) + strlen(line) >= MAXPDSTRING) continue;        \
        strcpy(dir + dlen, line);                                              \
        char* ext = strrchr(line, '.');                                        \
        if (ext && !strcmp(ext + 1, "m3u")) {                                  \
            char* fname = strrchr(line, '/');                                  \
            int len = (fname) ? ++fname - line : 0;                            \
            FILE* m3u = fopen(dir, "r");                                       \
            if (m3u) {                                                         \
                if (depth < 0x100) {                                           \
                    depth++;                                                   \
                    (deeper);                                                  \
                    depth--;                                                   \
                }                                                              \
                fclose(m3u);                                                   \
            }                                                                  \
        } else {                                                               \
            (increment);                                                       \
        }                                                                      \
    }

static int m3u_size(FILE* fp, char* dir, int dlen) {
    int size = 0;
    M3U_MAIN(size += m3u_size(m3u, dir, dlen + len), size++)
    return size;
}

static int playlist_fill(t_playlist* pl, FILE* fp, char* dir, int dlen, int i) {
    int oldlen = strlen(pl->dir->s_name);
    M3U_MAIN(i = playlist_fill(pl, m3u, dir, dlen + len, i), pl->arr[i++] = gensym(dir + oldlen))
    return i;
}

static err_t playlist_m3u(t_playlist* pl, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return "Could not open m3u";
    depth = 1;
    char dir[MAXPDSTRING];
    strcpy(dir, pl->dir->s_name);
    int size = m3u_size(fp, dir, strlen(dir));
    if (size < 1) {
        fclose(fp);  // prdt: was left open
        return "Playlist is empty";
    }
    if (size > pl->max) {
        pl->arr = (t_symbol**)resizebytes(pl->arr, pl->max * sizeof(t_symbol*), size * sizeof(t_symbol*));
        pl->max = size;
    }
    pl->size = size;
    rewind(fp);
    playlist_fill(pl, fp, dir, strlen(pl->dir->s_name), 0);
    fclose(fp);
    return 0;
}

// ---------------------- the reader ------------------------

static void* playfile_reader(void* arg) {
    t_playfile* x = arg;
    t_soundfile sf;
    ssize_t databytes = 0;  // the file's sample data, from sf_headersize
    int open = 0;
    float* buf = NULL;
    int bufch = 0;
    pthread_mutex_lock(&x->x_lock);
    while (!x->x_quit) {
        if (x->x_req_open) {
            x->x_req_open = 0;
            if (open) sys_close(sf.sf_fd);
            char path[MAXPDSTRING];
            strcpy(path, x->x_path);
            pthread_mutex_unlock(&x->x_lock);
            open = prdt_sf_open(path, &sf);
            databytes = open ? sf.sf_bytelimit : 0;
            pthread_mutex_lock(&x->x_lock);
            x->x_eof = !open;
            continue;
        }
        if (x->x_req_seek >= 0) {
            long frame = x->x_req_seek;
            x->x_req_seek = -1;
            if (open) {
                ssize_t at = (ssize_t)frame * sf.sf_bytesperframe;
                if (at > databytes) at = databytes;
                lseek(sf.sf_fd, sf.sf_headersize + at, SEEK_SET);
                sf.sf_bytelimit = databytes - at;
                x->x_eof = 0;
            }
            continue;
        }
        uint64_t w = x->x_w, r = __atomic_load_n(&x->x_r, __ATOMIC_ACQUIRE);
        if (!open || x->x_eof || RING - (w - r) < CHUNK) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 10 * 1000 * 1000;  // the perform routine signals without the lock, so a wakeup can be lost
            if (ts.tv_nsec >= 1000000000) ts.tv_sec++, ts.tv_nsec -= 1000000000;
            pthread_cond_timedwait(&x->x_cond, &x->x_lock, &ts);
            continue;
        }
        int nch = x->x_nch, gen = x->x_gen, loop = x->x_loop;
        if (nch != bufch) {
            free(buf);
            if (!(buf = malloc(CHUNK * nch * sizeof(float)))) break;
            bufch = nch;
        }
        pthread_mutex_unlock(&x->x_lock);
        long got = prdt_sf_read(&sf, buf, nch, CHUNK);
        pthread_mutex_lock(&x->x_lock);
        if (gen != x->x_gen || nch != x->x_nch) continue;  // flushed meanwhile
        if (got > 0) {
            for (long f = 0; f < got; f++)
                memcpy(x->x_ring + ((w + f) % RING) * nch, buf + f * nch, nch * sizeof(float));
            __atomic_store_n(&x->x_w, w + got, __ATOMIC_RELEASE);
            continue;
        }
        // the end, or an error: mark where, then go round or stop
        if (x->x_ends_w - __atomic_load_n(&x->x_ends_r, __ATOMIC_ACQUIRE) < ENDS) {
            x->x_ends[x->x_ends_w % ENDS] = w;
            __atomic_store_n(&x->x_ends_w, x->x_ends_w + 1, __ATOMIC_RELEASE);
        }
        if (loop && got == 0 && databytes > 0)
            x->x_req_seek = 0;
        else
            x->x_eof = 1;
    }
    if (open) sys_close(sf.sf_fd);
    free(buf);
    pthread_mutex_unlock(&x->x_lock);
    return NULL;
}

// ---------------------- Pd's thread ------------------------

// with x_lock held: drops what the ring holds; the caller asks the reader to open or seek
static void playfile_flush(t_playfile* x) {
    x->x_gen++;
    __atomic_store_n(&x->x_r, x->x_w, __ATOMIC_RELEASE);
    __atomic_store_n(&x->x_ends_r, x->x_ends_w, __ATOMIC_RELEASE);
}

static void playfile_seek(t_playfile* x, t_float ms) {
    if (!x->x_open) return;
    pthread_mutex_lock(&x->x_lock);
    playfile_flush(x);
    x->x_req_seek = ms > 0 ? (long)(ms * x->x_sr / 1000.) : 0;
    pthread_cond_signal(&x->x_cond);
    pthread_mutex_unlock(&x->x_lock);
}

// with x_lock held: the ring at a width of nch
static int playfile_resize(t_playfile* x, int nch) {
    float* ring = calloc((size_t)RING * nch, sizeof(float));
    if (!ring) return 0;
    free(x->x_ring);
    x->x_ring = ring;
    x->x_nch = nch;
    return 1;
}

static err_t playfile_load(t_playfile* x, int index) {
    char url[MAXPDSTRING];
    const char* fname = x->x_plist.arr[index]->s_name;
    if (fname[0] == '/')  // absolute path
        snprintf(url, MAXPDSTRING, "%s", fname);
    else {
        size_t dir_len = strlen(x->x_plist.dir->s_name);
        snprintf(url, MAXPDSTRING, "%s%s%s", x->x_plist.dir->s_name,
                 dir_len > 0 && x->x_plist.dir->s_name[dir_len - 1] != '/' ? "/" : "", fname);
    }
    t_soundfile sf;  // the header, here; the reader opens the file again for the samples
    if (!prdt_sf_open(url, &sf)) {
        pd_error(x, "[play.file~]: '%s': %s", url, soundfile_strerror(errno));
        return "Failed to open input stream";
    }
    sys_close(sf.sf_fd);
    x->x_sr = sf.sf_samplerate;
    int pd_sr = sys_getsr();
    if (x->x_sr != pd_sr)
        post("[play.file~] Warning: file's sample rate (%d Hz) differs from Pd's (%d Hz)", x->x_sr, pd_sr);
    x->x_file_nch = sf.sf_nchannels;
    int resized = 0;
    pthread_mutex_lock(&x->x_lock);
    if (x->x_mc && x->x_file_nch != x->x_nch) {
        if (!playfile_resize(x, x->x_file_nch)) {
            pthread_mutex_unlock(&x->x_lock);
            return "Out of memory";
        }
        resized = 1;
    }
    playfile_flush(x);
    snprintf(x->x_path, MAXPDSTRING, "%s", url);
    x->x_req_open = 1;
    x->x_req_seek = -1;
    pthread_cond_signal(&x->x_cond);
    pthread_mutex_unlock(&x->x_lock);
    if (resized) canvas_update_dsp();
    return 0;
}

static void playfile_pause(t_playfile* x) {
    x->x_play = 0;
}

static void playfile_continue(t_playfile* x) {
    x->x_play = 1;
}

static void playfile_start(t_playfile* x, t_float f, t_float ms) {
    int track = f;
    err_t err_msg = "";
    if (0 < track && track <= x->x_plist.size) {
        if ((err_msg = playfile_load(x, track - 1))) pd_error(x, "[play.file~] 'base start': %s.", err_msg);
        x->x_open = !err_msg;
        playfile_seek(x, ms);
    } else
        playfile_seek(x, 0);
    x->x_play = !err_msg;
}

static int playfile_find_file(t_playfile* x, t_symbol* file, char* dir_out, char** filename_out) {
    int fd = canvas_open(x->x_canvas, file->s_name, "", dir_out, filename_out, MAXPDSTRING, 1);
    if (fd < 0) {
        pd_error(x, "[play.file~] file '%s' not found", file->s_name);
        return 0;
    }
    sys_close(fd);
    return 1;
}

static void playfile_open(t_playfile* x, t_symbol* s, int ac, t_atom* av) {
    (void)s;
    x->x_play = 0;
    err_t err_msg = 0;
    if (ac == 0 || av->a_type != A_SYMBOL) {
        pd_error(x, "[play.file~]: 'open' needs a file name; prdt has no file panel");
        return;
    }
    char dirname[MAXPDSTRING];
    char* filename = NULL;
    if (!playfile_find_file(x, av->a_w.w_symbol, dirname, &filename)) return;
    t_playlist* pl = &x->x_plist;
    pl->dir = gensym(dirname);
    const char* ext = strrchr(filename, '.');
    if (ext && !strcmp(ext + 1, "m3u")) {
        char path[MAXPDSTRING];
        snprintf(path, MAXPDSTRING, "%s/%s", dirname, filename);  // ELSE opened the name alone, from Pd's folder
        err_msg = playlist_m3u(pl, path);
    } else {
        pl->size = 1;
        pl->arr[0] = gensym(filename);
    }
    if (err_msg || (err_msg = playfile_load(x, 0))) pd_error(x, "[play.file~]: open: %s.", err_msg);
    x->x_open = !err_msg;
}

static void playfile_float(t_playfile* x, t_float f) {
    playfile_start(x, f, 0);
}

static void playfile_bang(t_playfile* x) {
    playfile_float(x, 1);
}

static void playfile_stop(t_playfile* x) {
    playfile_float(x, 0);
}

static void playfile_loop(t_playfile* x, t_float f) {
    x->x_loop = f != 0;
    pthread_mutex_lock(&x->x_lock);
    if (x->x_loop && x->x_eof) {  // the reader stopped at the end: go round from there
        x->x_req_seek = 0;
        pthread_cond_signal(&x->x_cond);
    }
    pthread_mutex_unlock(&x->x_lock);
}

static void playfile_set(t_playfile* x, t_symbol* s) {
    t_atom at;
    SETSYMBOL(&at, s);
    if (x->x_open && x->x_play)
        x->x_play_next = s;
    else
        playfile_open(x, gensym("open"), 1, &at);
}

// the perform routine passed the end of the file: a bang for each end, then the next file, as ELSE's did
static void playfile_end(t_playfile* x) {
    int ended = x->x_ended;
    x->x_ended = 0;
    while (ended--) outlet_bang(x->x_bang_out);
    if (x->x_play_next) {
        t_atom at;
        SETSYMBOL(&at, x->x_play_next);
        x->x_play_next = NULL;
        playfile_open(x, gensym("open"), 1, &at);
        x->x_play = x->x_loop && x->x_open;
    } else if (!x->x_loop)
        playfile_seek(x, 0);
}

// n frames from the ring to outch blocks, stopping at the end of the file unless looping
static void playfile_dsp_out(t_playfile* x, t_sample** outs, int outch, int n) {
    uint64_t r = x->x_r, w = __atomic_load_n(&x->x_w, __ATOMIC_ACQUIRE);
    int nch = x->x_nch, minch = nch < outch ? nch : outch, i = 0;
    while (x->x_play && i < n) {
        unsigned er = x->x_ends_r, ew = __atomic_load_n(&x->x_ends_w, __ATOMIC_ACQUIRE);
        uint64_t end = er != ew ? x->x_ends[er % ENDS] : UINT64_MAX;
        if (r >= end) {  // the end of the file
            __atomic_store_n(&x->x_ends_r, er + 1, __ATOMIC_RELEASE);
            x->x_ended++;
            clock_delay(x->x_endclock, 0);
            if (!x->x_loop) x->x_play = 0;
            continue;
        }
        uint64_t avail = (end < w ? end : w) - r;
        if (avail == 0) break;  // the reader is behind: silence
        int take = avail < (uint64_t)(n - i) ? (int)avail : n - i;
        for (int k = 0; k < take; k++, i++, r++) {
            const float* frame = x->x_ring + (r % RING) * nch;
            for (int ch = 0; ch < minch; ch++) outs[ch][i] = frame[ch];
            for (int ch = minch; ch < outch; ch++) outs[ch][i] = 0;
        }
    }
    for (; i < n; i++)
        for (int ch = 0; ch < outch; ch++) outs[ch][i] = 0;
    __atomic_store_n(&x->x_r, r, __ATOMIC_RELEASE);
    pthread_cond_signal(&x->x_cond);  // without the lock: the reader also wakes every 10 ms
}

static t_int* playfile_perform(t_int* w) {
    t_playfile* x = (t_playfile*)(w[1]);
    playfile_dsp_out(x, x->x_outs, x->x_outch, x->x_n);
    return w + 2;
}

static t_int* playfile_perform_mc(t_int* w) {
    t_playfile* x = (t_playfile*)(w[1]);
    t_sample* out = (t_sample*)(w[2]);
    for (int ch = 0; ch < x->x_outch; ch++) x->x_outs[ch] = out + ch * x->x_n;
    playfile_dsp_out(x, x->x_outs, x->x_outch, x->x_n);
    return w + 3;
}

static void playfile_dsp(t_playfile* x, t_signal** sp) {
    x->x_n = sp[0]->s_n;
    int sr = sp[0]->s_sr;
    if (x->x_sr > 0 && x->x_sr != sr)
        post("[play.file~] Warning: file's sample rate (%d Hz) differs from Pd's (%d Hz)", x->x_sr, sr);
    if (x->x_mc) {
        x->x_outs = (t_sample**)resizebytes(x->x_outs, x->x_outch * sizeof(t_sample*), x->x_nch * sizeof(t_sample*));
        x->x_outch = x->x_nch;
        signal_setmultiout(&sp[0], x->x_outch);
        dsp_add(playfile_perform_mc, 2, x, sp[0]->s_vec);
    } else {
        for (int i = x->x_outch; i--;) {
            signal_setmultiout(&sp[i], 1);
            x->x_outs[i] = sp[i]->s_vec;
        }
        dsp_add(playfile_perform, 1, x);
    }
}

static void playfile_free(t_playfile* x) {
    pthread_mutex_lock(&x->x_lock);
    x->x_quit = 1;
    pthread_cond_signal(&x->x_cond);
    pthread_mutex_unlock(&x->x_lock);
    pthread_join(x->x_thread, NULL);
    pthread_mutex_destroy(&x->x_lock);
    pthread_cond_destroy(&x->x_cond);
    clock_free(x->x_endclock);
    free(x->x_ring);
    t_playlist* pl = &x->x_plist;
    freebytes(pl->arr, pl->max * sizeof(t_symbol*));
    freebytes(x->x_outs, x->x_outch * sizeof(t_sample*));
}

static void* playfile_new(t_symbol* s, int ac, t_atom* av) {
    (void)s;
    t_playfile* x = (t_playfile*)pd_new(playfile_class);
    x->x_canvas = canvas_getcurrent();
    x->x_sr = -1;
    x->x_file_nch = 1;
    x->x_req_seek = -1;
    t_playlist* pl = &x->x_plist;
    pl->max = 1;
    pl->arr = (t_symbol**)getbytes(pl->max * sizeof(t_symbol*));
    int nch = 1, filefound = 0, loop = 0, mc = 0, ncharg = 0;
    t_atom at[1];
    while (ac && av->a_type == A_SYMBOL &&
           (av->a_w.w_symbol == gensym("-loop") || av->a_w.w_symbol == gensym("-mc"))) {
        if (av->a_w.w_symbol == gensym("-loop"))
            loop = 1;
        else
            mc = 1;
        ac--, av++;
    }
    if (ac && av->a_type == A_FLOAT) {  // channels
        if (!mc) {
            ncharg = 1;
            if ((nch = atom_getfloat(av)) < 1) nch = 1;
        } else
            post("[play.file~] channels arg is ignore in -mc mode");
        ac--, av++;
    }
    if (ac && av->a_type == A_SYMBOL) {
        char dir[MAXPDSTRING], *file = NULL;
        t_symbol* sym = atom_getsymbol(av);
        if ((filefound = playfile_find_file(x, sym, dir, &file))) {
            SETSYMBOL(at, sym);
            t_soundfile sf;
            char path[MAXPDSTRING];
            snprintf(path, MAXPDSTRING, "%s/%s", dir, file);
            if (!ncharg && prdt_sf_open(path, &sf)) {  // channels from the file
                x->x_file_nch = nch = sf.sf_nchannels;
                sys_close(sf.sf_fd);
            }
        }
        ac--, av++;
    }
    x->x_mc = mc;
    x->x_loop = loop;
    pthread_mutex_init(&x->x_lock, NULL);
    pthread_cond_init(&x->x_cond, NULL);
    if (!playfile_resize(x, nch)) nch = 0;
    x->x_outch = nch;
    x->x_outs = (t_sample**)getbytes(nch * sizeof(t_sample*));
    if (mc)
        outlet_new(&x->x_obj, &s_signal);
    else
        for (int i = 0; i < nch; i++) outlet_new(&x->x_obj, &s_signal);
    x->x_bang_out = outlet_new(&x->x_obj, 0);
    x->x_endclock = clock_new(x, (t_method)playfile_end);
    pthread_create(&x->x_thread, NULL, playfile_reader, x);
    if (filefound) playfile_open(x, gensym("open"), 1, at);
    if (ac) {  // autostart, then loop
        if (av->a_type != A_FLOAT) goto errstate;
        playfile_start(x, atom_getfloat(av) != 0, 0.0f);
        ac--, av++;
        if (ac) {
            if (av->a_type != A_FLOAT) goto errstate;
            x->x_loop = atom_getfloat(av) != 0;
        }
    }
    return x;
errstate:
    pd_error(x, "[play.file~] improper args");
    pd_free((t_pd*)x);  // ELSE returned without freeing
    return NULL;
}

void setup_play0x2efile_tilde(void) {
    playfile_class = class_new(gensym("play.file~"), (t_newmethod)playfile_new, (t_method)playfile_free,
                               sizeof(t_playfile), CLASS_MULTICHANNEL, A_GIMME, 0);
    class_addbang(playfile_class, playfile_bang);
    class_addfloat(playfile_class, playfile_float);
    class_addmethod(playfile_class, (t_method)playfile_bang, gensym("start"), A_NULL);
    class_addmethod(playfile_class, (t_method)playfile_stop, gensym("stop"), A_NULL);
    class_addmethod(playfile_class, (t_method)playfile_open, gensym("open"), A_GIMME, 0);
    class_addmethod(playfile_class, (t_method)playfile_dsp, gensym("dsp"), A_CANT, 0);
    class_addmethod(playfile_class, (t_method)playfile_seek, gensym("seek"), A_FLOAT, 0);
    class_addmethod(playfile_class, (t_method)playfile_loop, gensym("loop"), A_FLOAT, 0);
    class_addmethod(playfile_class, (t_method)playfile_continue, gensym("continue"), A_NULL);
    class_addmethod(playfile_class, (t_method)playfile_pause, gensym("pause"), A_NULL);
    class_addmethod(playfile_class, (t_method)playfile_set, gensym("set"), A_SYMBOL, 0);
}
