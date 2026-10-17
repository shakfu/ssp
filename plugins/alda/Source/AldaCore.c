// psnd's alda/scheduler.c holds the event list the core fills, and alda_events_play, which sends MIDI
// through psnd's backend. The plugin never plays that way: these empty definitions satisfy the link.
#include "alda/midi_backend.h"

void alda_midi_send_note_on(AldaContext* ctx, int channel, int pitch, int velocity) {
    (void)ctx, (void)channel, (void)pitch, (void)velocity;
}
void alda_midi_send_note_off(AldaContext* ctx, int channel, int pitch) { (void)ctx, (void)channel, (void)pitch; }
void alda_midi_send_program(AldaContext* ctx, int channel, int program) { (void)ctx, (void)channel, (void)program; }
void alda_midi_send_cc(AldaContext* ctx, int channel, int cc, int value) {
    (void)ctx, (void)channel, (void)cc, (void)value;
}
void alda_midi_sleep_ms(AldaContext* ctx, int ms) { (void)ctx, (void)ms; }
