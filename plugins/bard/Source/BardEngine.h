#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Bookmarks.h"
#include "ResumeTable.h"
#include "Room.h"
#include "Wsola.h"
#include "engine/Dsp.h"
#include "engine/Engine.h"
#include "engine/Station.h"
#include "engine/Stream.h"

namespace bard {

enum class Route { Stereo, Split, Random };
enum class Seq { Read, Recite, Wander };
// at a segment end: the sidecar's loop= (hold if it has none), hold, or loop
enum class LoopPolicy { File, Hold, Loop };

// Two spoken-word players. Each deck plays a book from a shelf, keeps its place across sessions,
// and moves through it by bookmarks read from a text file beside the audio. The worker thread does
// every scan, open, seek and file write; the audio thread only drains the open stream.
class BardEngine : public ssp::engine::Engine {
public:
    static constexpr int DECKS = 2;
    // inputs, per deck from in[deck * I_PER_DECK]; then the crossfade CV
    enum { I_BOOK, I_MARK, I_VOLUME, I_GATE, I_PER_DECK };
    enum { I_XFADE = DECKS * I_PER_DECK, I_MAX };
    enum { O_L, O_R, O_A, O_B, O_ENV_A, O_ENV_B, O_GATE_A, O_GATE_B, O_MAX };

    // Set from the audio thread before each process().
    struct Controls {
        float book = 0.0f, mark = 0.0f;  // selectors, 0..1 across the shelf and the marks
        float rate = 0.5f;               // 0.5x at 0, 1x at the centre, 2.5x at 1
        float keep = 0.0f;               // pitch keep: 0 varispeed .. 1 pitch held
        float volume = 1.0f;
        float shelf = 0.0f;              // selector, 0..1 across the shelves
        float position = 0.0f;           // a move jumps to this point of the book, or of the segment
        Seq seq = Seq::Read;
        LoopPolicy loop = LoopPolicy::File;
        int reroll = 0;                  // re-scatters a book's auto-marks
        float seam = 0.0f;               // fade-in after a jump, 0..500 ms
        float duck = 0.0f;               // how far this deck's voice ducks the other
        float release = 0.5f;            // duck release, 0 slow .. 1 fast
        float colour = 0.0f, colourMix = 0.0f;
        float room = 0.0f, roomMix = 0.0f;
        Room::Character character = Room::Character::Plate;
        bool play = false, back = false, next = false;  // rising edges act
    };

    BardEngine();
    ~BardEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    void idle() override;

    // audio thread
    Controls& controls(int d) { return ctl_[d]; }
    void setCrossfade(float x) { xfade_ = x; }
    void setRoute(Route r);

    // message thread: the folder of shelves
    void setRoot(const std::string& dir);
    std::string root() const;

    struct Info {
        int shelf = -1, shelves = 0, book = -1, books = 0, mark = -1, marks = 0;
        bool autoMarks = false, paused = true, error = false;
        double seconds = 0.0, length = 0.0;  // playhead and book length
        std::string shelfName, bookName;
    };
    Info info(int d) const;

    static constexpr uint32_t JUMP_BACK_S = 15;

private:
    // A stream opened at `start`, tagged with the worker's seek count.
    struct Cue {
        ssp::engine::Stream stream;
        uint32_t start = 0, gen = 0;
        float rate = 0.0f;  // source frames per second
    };

    struct Link {  // audio <-> worker, per deck
        std::atomic<Cue*> next{ nullptr }, retired{ nullptr };
        // audio -> worker
        std::atomic<uint32_t> pos{ 0 }, gen{ 0 };  // playhead in source frames, of cue `gen`
        std::atomic<uint32_t> plays{ 0 }, backs{ 0 }, nexts{ 0 };
        std::atomic<float> bookX{ 0 }, markX{ 0 }, shelfX{ 0 }, position{ 0 };
        std::atomic<int> seq{ 0 }, loop{ 0 }, reroll{ 0 };
        // worker -> audio
        std::atomic<uint32_t> segEnd{ 0 };
        std::atomic<bool> paused{ true };
        std::atomic<uint32_t> gates{ 0 };  // bookmark crossings, for the gate output
        // worker -> UI
        std::atomic<int> shelf{ -1 }, shelves{ 0 }, book{ -1 }, books{ 0 }, mark{ -1 }, marks{ 0 };
        std::atomic<bool> autoMarks{ false }, error{ false };
        std::atomic<uint32_t> frames{ 0 }, rate{ 0 };
    };

    struct AudioDeck {
        Cue* cue = nullptr;
        uint32_t pos = 0;
        float ratio = 1.0f;  // source rate over the engine rate
        float phase = 0.0f, cur = 0.0f, next = 0.0f;
        bool primed = false;
        Wsola wsola;
        float rate = -1.0f, keep = -1.0f, resK = 1.0f;  // the rate chain, recomputed on a change
        float seamGain = 1.0f;
        float colour = -1.0f;
        ssp::engine::Biquad hp, lp;
        float drive = 1.0f;
        std::vector<float> roomMem;
        Room room;
        float roomSize = -1.0f;
        Room::Character character = Room::Character::Plate;
        float env = 0.0f, duckGain = 1.0f;
        bool playHigh = false, backHigh = false, nextHigh = false, gateHigh = false;
        uint32_t gates = 0;
        int gateOut = 0;  // samples left of the gate pulse
        float panL = 0.7071f, panR = 0.7071f;
    };

    struct WorkerDeck {
        int shelf = -2;  // -2: never scanned
        std::vector<ssp::engine::Station> books;
        int64_t rescanAt = 0;
        int open = -1;
        uint32_t frames = 0, srcRate = 48000;
        MarkList marks;
        int seg = -1;
        uint32_t segEnd = 0;
        bool loopSeg = false, paused = true;
        Seq seq = Seq::Read;
        LoopPolicy loop = LoopPolicy::File;
        int reroll = 0;
        uint32_t gen = 0, cueStart = 0;
        Cue* live = nullptr;  // the newest cue published
        int pendingBook = -1;
        int64_t pendingBookAt = 0;
        int pendingSeg = -1;
        int64_t pendingSegAt = 0;
        float markX = -1.0f;  // the selector position last acted on
        bool markMoved = false;
        float position = -1.0f;
        bool scrubbed = false;
        int64_t scrubAt = 0;
        bool req = false;
        uint32_t reqFrame = 0;
        uint32_t plays = 0, backs = 0, nexts = 0;
        int seenMark = -1;
        int64_t errUntil = 0;
    };

    // audio thread
    void adopt(int d);
    void render(int d, float* mono, int n);
    bool pull(int d, float& out);
    void updateFx(int d);

    // worker thread
    void work(int d, int64_t now);
    uint32_t playhead(int d) const;
    void scanShelf(int d, int64_t now);
    void applySelectors(int d, int64_t now);
    void openBook(int d, int book, int64_t now);
    bool seek(int d, uint32_t frame);
    void publish(int d, Cue* cue);
    void loadMarks(int d);
    void applyLoop(int d, bool resume);
    void enterSegment(int d, int mark);
    void advance(int d, int steps);
    void requestJump(int d, uint32_t frame);
    void loadLibrary();
    void saveResume(int64_t now);
    std::string resumeKey(int d) const;

    float sr_ = 48000.0f;
    int maxBlock_ = 1;
    Controls ctl_[DECKS];
    Link sh_[DECKS];
    AudioDeck au_[DECKS];
    WorkerDeck wk_[DECKS];
    std::vector<float> mono_[DECKS], voice_;
    float xfade_ = 0.5f;
    Route route_ = Route::Stereo;
    uint32_t rng_ = 0x9e3779b9u;

    // worker
    std::string libRoot_;  // the root shelfDirs_, cfg_ and resume_ came from
    std::vector<std::string> shelfDirs_;
    Config cfg_;
    ResumeTable resume_;
    bool resumeDirty_ = false, resumeWritable_ = true;
    int64_t resumeAt_ = 0;
    std::atomic<bool> rootChanged_{ true };

    mutable std::mutex lock_;  // root_ and the names below; never taken by the audio thread
    std::string root_;
    std::string shelfName_[DECKS], bookName_[DECKS];
};

}  // namespace bard
