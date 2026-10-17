// Stands in for libremidi's C header, which psnd's alda/context.h includes for three pointer fields the
// core leaves NULL. The plugin builds psnd's core without libremidi.
#pragma once

typedef struct libremidi_midi_observer_handle libremidi_midi_observer_handle;
typedef struct libremidi_midi_out_handle libremidi_midi_out_handle;
typedef struct libremidi_midi_out_port libremidi_midi_out_port;
