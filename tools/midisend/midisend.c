// An ALSA sequencer port that plays the notes written to a FIFO, so a test can play an SSP module
// over ssh without a MIDI keyboard. A module's general panel (RS + LS) lists it as the MIDI input
// "midisend". Runs on the SSP; `make midisend` builds it, copies it to SSP_HOST and starts it.
//
// usage: midisend [FIFO]                      (default /tmp/midisend)
//   echo "on 60 100" > /tmp/midisend          note on: note number, velocity 1..127
//   echo "off 60" > /tmp/midisend             note off
//   echo "ch 2" > /tmp/midisend               channel 1..16 for the notes after it; 1 at start

#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    const char* fifo = argc > 1 ? argv[1] : "/tmp/midisend";
    snd_seq_t* seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        fprintf(stderr, "midisend: cannot open the ALSA sequencer\n");
        return 1;
    }
    snd_seq_set_client_name(seq, "midisend");
    int port = snd_seq_create_simple_port(seq, "midisend", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0 || (mkfifo(fifo, 0666) < 0 && errno != EEXIST)) {
        fprintf(stderr, "midisend: cannot create the port or %s\n", fifo);
        return 1;
    }
    printf("midisend: sequencer client %d port %d, reading %s\n", snd_seq_client_id(seq), port, fifo);
    fflush(stdout);

    int channel = 0;
    for (;;) {
        FILE* f = fopen(fifo, "r");  // waits for a writer; reopened after each one closes
        if (!f) return 1;
        char line[128];
        while (fgets(line, sizeof line, f)) {
            int a, b;
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            snd_seq_ev_set_source(&ev, port);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            if (sscanf(line, "on %d %d", &a, &b) == 2) {
                snd_seq_ev_set_noteon(&ev, channel, a & 0x7F, b & 0x7F);
            } else if (sscanf(line, "off %d", &a) == 1) {
                snd_seq_ev_set_noteoff(&ev, channel, a & 0x7F, 0);
            } else {
                if (sscanf(line, "ch %d", &a) == 1 && a >= 1 && a <= 16) channel = a - 1;
                continue;
            }
            snd_seq_event_output_direct(seq, &ev);
        }
        fclose(f);
    }
}
