#pragma once

#include <atomic>
#include <cstdint>

namespace ssp::engine {

// A sequencer's position in ticks, TICKS_PER_QUARTER a quarter note, driven by its own tempo or by a
// clock: rising edges on an input, PPQN of them a quarter, or MIDI clock, 24 a quarter, with Start, Stop
// and Continue. Each pulse moves the position to the next pulse's; between pulses it runs at the last
// interval's rate, never past the next pulse, so it stays locked and halts when the clock stops. A rising
// edge on the reset input moves it to 0. JUCE-free, as the engines.
//
// The owner loops, if at all: the position grows until wrap(). Each audio block, in this order:
//
//   follow(source, rate); transport(); while (midiPulse()) {...}
//   per sample: resetInput(); clockInput(); play the events up to swung(); advance()
//
// A pulse can carry the position past the owner's loop end: the owner checks when midiPulse() or
// clockInput() returns true.
class Clock {
public:
    enum Source : uint8_t { FREE, INPUT, MIDI };
    enum Transport : uint8_t { NONE, START, STOP, CONTINUE };
    static constexpr int TICKS_PER_QUARTER = 480;
    static constexpr int MIDI_PPQN = 24;
    // rising and falling thresholds of the reset and clock inputs, as teletype's: 2.21 V and 0.8 V at 0.2 a volt
    static constexpr float TRIG_HIGH = 2.21f * 0.2f, TRIG_LOW = 0.8f * 0.2f;

    // MIDI input thread: a clock tick at `seconds`, and the transport. Used while the source is MIDI.
    void midiClock(double seconds);
    void midiStart() { transport_.store(START, std::memory_order_release); }
    void midiStop() { transport_.store(STOP, std::memory_order_release); }
    void midiContinue() { transport_.store(CONTINUE, std::memory_order_release); }

    // Audio thread, each block first. A new source takes over from the pulse at or before the position, at
    // the tempo until two pulses are measured.
    void follow(Source source, float sampleRate);
    // Then, every block: for the MIDI source, Start (the position goes to 0), Stop (halts) or Continue;
    // NONE otherwise or without news. The owner restarts on Start and releases its notes on Stop.
    Transport transport();
    // Then, until false: takes the MIDI clock ticks that came before transport(); true at each that moved
    // the position, which they do while the source is MIDI and not halted.
    bool midiPulse();

    // Each sample: the reset input's value; true at a rising edge, which moved the position to 0. Then the
    // clock input's; true at a rising edge while the source is INPUT, a pulse.
    bool resetInput(float v);
    bool clockInput(float v);
    // Each sample, after its events: moves the position on; false, without moving, while MIDI Stop halts it.
    bool advance();

    // the position and the last pulse to 0
    void rewind() { pos_ = lastGrid_ = 0.0; }
    // The owner's loop: moves the position and the last pulse back by `length`.
    void wrap(double length) {
        pos_ -= length;
        lastGrid_ -= length;
    }

    // the tempo the free source runs at, and that a clock source assumes before two pulses
    void setBpm(double bpm) { bpm_ = bpm; }
    void setTempo(double factor) { tempo_ = factor; }  // on the free source's tempo
    void setSwing(double swing) { swing_ = swing; }    // 0 straight .. 0.5: every second 16th later, 50% to 75%
    void setPpqn(int ppqn) { ppqn_ = ppqn; }           // the clock input's pulses a quarter

    double position() const { return pos_; }
    double swung() const;  // the position, with swing: the time events play up to
    Source source() const { return source_; }
    double bpm() const;  // measured from the pulses, else the free tempo

private:
    void pulse(double interval);  // `interval` samples after the last; 0 unknown
    double tpp() const { return double(TICKS_PER_QUARTER) / (source_ == MIDI ? MIDI_PPQN : ppqn_); }  // per pulse

    double pos_ = 0.0;
    double lastGrid_ = 0.0;  // the last pulse's position; the position stays below it plus one pulse
    double rate_ = 0.0;      // ticks per sample between pulses; 0 until two pulses are measured
    double perSample_ = 0.0;  // ticks per sample at 1 bpm
    double sampleRate_ = 48000.0;
    double bpm_ = 120.0, tempo_ = 1.0, swing_ = 0.0;
    int ppqn_ = 4;
    Source source_ = FREE;  // the source the last block followed
    bool halted_ = false;   // MIDI Stop
    bool resetHigh_ = false, clockHigh_ = false;
    double sinceEdge_ = 0.0;  // samples since the clock input's last rising edge; 0 before the first

    // written by the MIDI input thread
    std::atomic<uint8_t> transport_{ NONE };
    std::atomic<uint32_t> midiPulses_{ 0 };
    std::atomic<double> midiInterval_{ 0.0 };  // seconds between the last two ticks; 0 after a gap
    double lastMidiTick_ = -1.0;               // MIDI input thread
    uint32_t pulsesSeen_ = 0, pulsesDue_ = 0;  // audio thread
};

}  // namespace ssp::engine
