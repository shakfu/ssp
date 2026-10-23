// Prints the notes and controllers that ALSA sequencer ports send, one line each: "ms client:port channel
// on|off|cc number value", ms from start. Subscribes to every readable port whose name contains NAME, so a
// test can watch a module's MIDI output, or a controller's input, over ssh. Runs on the SSP, which has no
// alsa-utils; `make mididump` builds it and copies it to SSP_HOST's /tmp.
//
// usage: mididump [NAME]                      (default gseq)
//   ssh root@192.168.1.6 /tmp/mididump gseq                       until Ctrl-C
//   ssh root@192.168.1.6 '/tmp/mididump LPMiniMK3 > /tmp/lp.log &'  in the background; killall mididump

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char** argv) {
    const char* want = argc > 1 ? argv[1] : "gseq";
    snd_seq_t* seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) return 1;
    snd_seq_set_client_name(seq, "mididump");
    int me = snd_seq_create_simple_port(seq, "in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                        SND_SEQ_PORT_TYPE_APPLICATION);
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t* pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        int c = snd_seq_client_info_get_client(ci);
        snd_seq_port_info_set_client(pi, c);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const char* name = snd_seq_port_info_get_name(pi);
            if (strstr(name, want) && (snd_seq_port_info_get_capability(pi) & SND_SEQ_PORT_CAP_SUBS_READ)) {
                int p = snd_seq_port_info_get_port(pi);
                if (snd_seq_connect_from(seq, me, c, p) == 0) printf("# from %d:%d %s\n", c, p, name);
            }
        }
    }
    fflush(stdout);
    const double t0 = ms();
    for (;;) {
        snd_seq_event_t* ev;
        if (snd_seq_event_input(seq, &ev) < 0) continue;
        const double t = ms() - t0;
        if (ev->type == SND_SEQ_EVENT_NOTEON || ev->type == SND_SEQ_EVENT_NOTEOFF) {
            const int on = ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0;
            printf("%.1f %d:%d %d %s %d %d\n", t, ev->source.client, ev->source.port, ev->data.note.channel + 1,
                   on ? "on" : "off", ev->data.note.note, ev->data.note.velocity);
        } else if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
            printf("%.1f %d:%d %d cc %d %d\n", t, ev->source.client, ev->source.port, ev->data.control.channel + 1,
                   ev->data.control.param, ev->data.control.value);
        }
        fflush(stdout);
    }
}
