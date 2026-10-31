// ELSE's [sfload] (Tim Schoen and Alexandre Porres) on Pd's sound-file reader, not ffmpeg: the same messages
// and info outlet, for WAV, AIFF, CAF and NeXT files. [download] is not supported. The threaded reader only reads
// the file; Pd's thread finds the path and fills the arrays, as a thread here has no Pd instance.
#include <pthread.h>
#include <stdlib.h>

#include "g_canvas.h"
#include "prdt_soundfile.h"

#define GROW 65536  // frames per allocation while reading

static t_class* sfload_class;

typedef struct _sfload {
    t_object x_obj;
    t_outlet* x_info_outlet;
    t_canvas* x_canvas;
    t_symbol* x_arr_name;
    int x_threaded;
    int x_ch;          // the channel to load, or -1 for all
    long x_arr_size;   // ms, or -1 for the file's length
    long x_onset;      // ms
    char x_path[MAXPDSTRING];
    // the read: written by the reader, then read by Pd's thread once x_done is set
    float* x_data;     // interleaved
    long x_frames;
    t_soundfile x_sf;
    int x_err;         // errno of a failed read, else 0
    pthread_t x_thread;
    int x_running;     // a thread to join
    int x_done;        // atomic
    t_clock* x_clock;
} t_sfload;

static void sfload_read(t_sfload* x) {
    long cap = 0;
    x->x_frames = 0;
    x->x_err = 0;
    if (!prdt_sf_open(x->x_path, &x->x_sf)) {
        x->x_err = errno ? errno : SOUNDFILE_ERRUNKNOWN;
        return;
    }
    int nch = x->x_sf.sf_nchannels;
    for (;;) {
        if (x->x_frames + GROW > cap) {
            float* d = realloc(x->x_data, (cap + GROW) * nch * sizeof(float));
            if (!d) {
                x->x_err = ENOMEM;
                break;
            }
            x->x_data = d;
            cap += GROW;
        }
        long got = prdt_sf_read(&x->x_sf, x->x_data + x->x_frames * nch, nch, cap - x->x_frames);
        if (got < 0) {
            x->x_err = errno ? errno : EIO;
            break;
        }
        if (got == 0) break;
        x->x_frames += got;
    }
    sys_close(x->x_sf.sf_fd);
    x->x_sf.sf_fd = -1;
}

static void* sfload_thread(void* arg) {
    t_sfload* x = arg;
    sfload_read(x);
    __atomic_store_n(&x->x_done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void sfload_fill(t_sfload* x, t_garray* garray, int ch, long size, long start, long count) {
    garray_resize_long(garray, size);
    int n;
    t_word* vec;
    if (!garray_getfloatwords(garray, &n, &vec)) return;
    int nch = x->x_sf.sf_nchannels;
    for (long i = 0; i < n; i++) vec[i].w_float = i < count ? x->x_data[(start + i) * nch + ch] : 0;
    garray_redraw(garray);
}

// as ELSE's: "<ch>-<name>" for each channel, else <name> for the first, or for the one asked for
static void sfload_update_arrays(t_sfload* x) {
    float srkhz = sys_getsr() * 0.001;
    long start = x->x_onset * srkhz;
    long filesize = x->x_frames - start;
    if (filesize < 0) filesize = 0, start = x->x_frames;
    long arraysize = x->x_arr_size > 0 ? (long)(x->x_arr_size * srkhz) : filesize;
    long readsize = filesize < arraysize ? filesize : arraysize;  // ELSE left it unset for a longer file
    for (int ch = 0; ch < x->x_sf.sf_nchannels; ch++) {
        if (x->x_ch != -1 && ch != x->x_ch) continue;
        char name[MAXPDSTRING];
        snprintf(name, MAXPDSTRING, "%i-%s", ch, x->x_arr_name->s_name);
        t_garray* garray = (t_garray*)pd_findbyclass(gensym(name), garray_class);
        if (garray)
            sfload_fill(x, garray, ch, arraysize, start, readsize);
        else {
            if (x->x_ch == -1 && ch > 0) return;
            if ((garray = (t_garray*)pd_findbyclass(x->x_arr_name, garray_class)))
                sfload_fill(x, garray, ch, arraysize, start, readsize);
        }
    }
}

static void sfload_output(t_sfload* x) {
    if (x->x_err) {
        pd_error(x, "[sfload]: %s: %s", x->x_path, soundfile_strerror(x->x_err));
        return;
    }
    if (x->x_ch >= x->x_sf.sf_nchannels) {
        pd_error(x, "[sfload]: channel (%d) is out of range (from 0 to %d)", x->x_ch, x->x_sf.sf_nchannels - 1);
        return;
    }
    sfload_update_arrays(x);
    t_atom info[4];
    SETFLOAT(info + 0, x->x_frames);
    SETFLOAT(info + 1, x->x_sf.sf_samplerate);
    SETFLOAT(info + 2, x->x_sf.sf_nchannels);
    SETFLOAT(info + 3, x->x_sf.sf_bytespersample * 8);
    outlet_list(x->x_info_outlet, &s_list, 4, info);
}

static void sfload_join(t_sfload* x) {
    if (x->x_running) {
        pthread_join(x->x_thread, NULL);
        x->x_running = 0;
    }
}

static void sfload_tick(t_sfload* x) {
    if (!__atomic_load_n(&x->x_done, __ATOMIC_ACQUIRE)) {
        clock_delay(x->x_clock, 20);
        return;
    }
    sfload_join(x);
    sfload_output(x);
}

static void sfload_load(t_sfload* x, t_symbol* s, int ac, t_atom* av) {
    (void)s;
    if (!x->x_arr_name) {
        pd_error(x, "[sfload]: No array set");
        return;
    }
    if (!ac || av->a_type != A_SYMBOL) {
        pd_error(x, "[sfload]: 1st argument of 'load' must be a symbol");
        return;
    }
    t_symbol* file = atom_getsymbol(av);
    sfload_join(x);  // one read at a time: the reader uses the fields set below
    clock_unset(x->x_clock);
    x->x_ch = -1, x->x_arr_size = -1, x->x_onset = 0;
    if (ac >= 2 && av[1].a_type == A_FLOAT && (x->x_ch = atom_getint(av + 1)) < 0) x->x_ch = -1;
    if (ac >= 3 && av[2].a_type == A_FLOAT) x->x_arr_size = atom_getint(av + 2);
    if (ac >= 4 && av[3].a_type == A_FLOAT) x->x_onset = atom_getint(av + 3);
    if (x->x_onset < 0) x->x_onset = 0;
    char zero[MAXPDSTRING];
    snprintf(zero, MAXPDSTRING, "%i-%s", x->x_ch == -1 ? 0 : x->x_ch, x->x_arr_name->s_name);
    if (!pd_findbyclass(x->x_arr_name, garray_class) && !pd_findbyclass(gensym(zero), garray_class)) {
        pd_error(x, "[sfload]: Array \"%s\" or \"%s\" not found", x->x_arr_name->s_name, zero);
        return;
    }
    char dir[MAXPDSTRING], *name;
    int fd = canvas_open(x->x_canvas, file->s_name, "", dir, &name, MAXPDSTRING, 1);
    if (fd < 0) {
        pd_error(x, "[sfload] file '%s' not found", file->s_name);
        return;
    }
    sys_close(fd);
    snprintf(x->x_path, MAXPDSTRING, "%s/%s", dir, name);
    if (x->x_threaded) {
        x->x_done = 0;
        if (pthread_create(&x->x_thread, NULL, sfload_thread, x) != 0) {
            pd_error(x, "[sfload]: Error creating thread");
            return;
        }
        x->x_running = 1;
        clock_delay(x->x_clock, 20);
    } else {
        sfload_read(x);
        sfload_output(x);
    }
}

static void sfload_download(t_sfload* x, t_symbol* s, int ac, t_atom* av) {
    (void)s, (void)ac, (void)av;
    pd_error(x, "[sfload]: 'download' needs ffmpeg, which prdt does not have");
}

static void sfload_threaded(t_sfload* x, t_floatarg f) {
    x->x_threaded = (f != 0);
}

static void sfload_set(t_sfload* x, t_symbol* s) {
    x->x_arr_name = s;
}

static void sfload_free(t_sfload* x) {
    sfload_join(x);
    clock_free(x->x_clock);
    free(x->x_data);
}

static void* sfload_new(t_symbol* s, int ac, t_atom* av) {
    (void)s;
    t_sfload* x = (t_sfload*)pd_new(sfload_class);
    if (ac && av->a_type == A_SYMBOL && atom_getsymbol(av) == gensym("-t")) {
        x->x_threaded = 1;
        av++, ac--;
    }
    if (ac && av->a_type == A_SYMBOL) x->x_arr_name = atom_getsymbol(av);
    x->x_canvas = canvas_getcurrent();
    x->x_sf.sf_fd = -1;
    x->x_clock = clock_new(x, (t_method)sfload_tick);
    x->x_info_outlet = outlet_new(&x->x_obj, &s_list);
    return x;
}

void sfload_setup(void) {
    sfload_class = class_new(gensym("sfload"), (t_newmethod)sfload_new, (t_method)sfload_free, sizeof(t_sfload), 0,
                             A_GIMME, 0);
    class_addmethod(sfload_class, (t_method)sfload_load, gensym("load"), A_GIMME, 0);
    class_addmethod(sfload_class, (t_method)sfload_download, gensym("download"), A_GIMME, 0);
    class_addmethod(sfload_class, (t_method)sfload_set, gensym("set"), A_SYMBOL, 0);
    class_addmethod(sfload_class, (t_method)sfload_threaded, gensym("threaded"), A_FLOAT, 0);
}
