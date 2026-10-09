# Writing a C++ plugin

This guide builds `svca`, a stereo VCA, on the engine layer. The finished plugin is in [`examples/svca`](../examples/svca); `make test` builds it for the host and checks its output. [DEVELOPING.md](DEVELOPING.md) describes the layers and tools it uses.

## Quick start

```
scripts/new_plugin.py my_amp mamp --description "mono amp"
```

This copies `examples/svca` to `plugins/my_amp` as the module `mamp`, renames it throughout, adds README and CHANGELOG stubs, and adds the folder to `plugins/CMakeLists.txt`. The copy builds as it is. It prints the remaining steps: the test, the manifest, the build. The arguments are the folder, which is also the C++ namespace, and the four-character name. The script refuses a name another plugin uses.

The rest of this guide explains what the copy contains.

## The example

`svca` has three inputs and two outputs:

| Jack | What |
|-|-|
| In L, In R | audio |
| Level CV | adds to Level; 1.0 (5 V) is a gain of 1 |
| Out L, Out R | the inputs times Level plus Level CV, clamped to 0..2 |

## 1. Pick a name

The name has at most four characters, and the uid is the same name in capitals: `svca`/`SVCA`. Synthor does not list a plugin whose uid does not spell its name. Check that the name is free: `ls plugins` and the module list on your card.

Create `plugins/<dir>/` with a `Source/` folder. The rest of this guide uses `svca`.

## 2. The engine

`Source/SvcaEngine.h` holds the DSP. It derives from `ssp::engine::Engine` and does not include JUCE, so a test can build it with a plain compiler.

```cpp
class SvcaEngine : public ssp::engine::Engine {
public:
    enum { I_L, I_R, I_CV, I_MAX };  // the order of PluginProcessor's input buses
    enum { O_L, O_R, O_MAX };        // and of its output buses

    // audio thread: set by PluginProcessor::control before each process()
    float level = 1.0f;

    void prepare(float sampleRate, int) override {
        smooth_ = 1.0f - std::exp(-1.0f / (0.005f * sampleRate));  // 5 ms
    }

    void process(const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; i++) {
            // the CV adds to Level: 1.0 (5 V) is one unit of gain
            float target = std::clamp(level + in[I_CV][i], 0.0f, 2.0f);
            gain_ += (target - gain_) * smooth_;
            out[O_L][i] = in[I_L][i] * gain_;
            out[O_R][i] = in[I_R][i] * gain_;
        }
    }

private:
    float gain_ = 0.0f, smooth_ = 1.0f;
};
```

- `prepare(sampleRate, maxBlock)` runs on the message thread before audio starts, and again when the rate changes. Allocate here.
- `process(in, out, n)` runs on the audio thread. `in` and `out` hold one buffer per jack, in bus order; `n` is at most `maxBlock`. Do not block, lock or allocate.
- `idle()`, not used here, runs on a worker thread every 10 ms. File reads and other slow work go there.

An SSP signal of 1.0 is 5 V. A V/oct input reads 0.2 per volt.

## 3. The processor

`Source/PluginProcessor.h` declares `PluginProcessor`; `SSPApi.h` requires that name. It derives from `ssp::engine::EngineProcessor`.

```cpp
class PluginProcessor : public ssp::engine::EngineProcessor {
public:
    PluginProcessor();

    const String getName() const override { return JucePlugin_Name; }
    AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

protected:
    void control(const float* const* in, int n) override;

private:
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::vector<ssp::engine::ParamPage> pages();
    svca::SvcaEngine& vca() { return static_cast<svca::SvcaEngine&>(engine()); }

    RangedAudioParameter& level_;
};
```

`Source/PluginProcessor.cpp` fills it in, in four parts.

**Parameters.** Each is a `ssp::BaseFloatParameter`: id, name, min, max, default, and an optional step. The id is what presets and rack store, so do not rename it after release.

```cpp
PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<svca::SvcaEngine>()),
      level_(*vts().getParameter("level")) {
    init();
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    params.add(std::make_unique<ssp::BaseFloatParameter>("level", "Level", 0.0f, 2.0f, 1.0f));
    return params;
}
```

**Jacks.** One mono bus per jack. The bus names are the jack names in Synthor and in rack's matrix. `getBusesProperties` is static because `SSPApi.h` reads the names before an instance exists.

```cpp
PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "In L", "In R", "Level CV" }) props.addBus(true, name, AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}
```

**Control.** `control()` runs on the audio thread before each `Engine::process`. It copies parameter values into the engine. `in` is the copy of the inputs the engine will see, for a plugin that reads a gate or a clock here.

```cpp
void PluginProcessor::control(const float* const*, int) {
    vca().level = level_.convertFrom0to1(level_.getValue());
}
```

**Editor.** A `ParamPage` is a title, a colour and up to four controls, one per encoder. Each control has a coarse and a fine step in the parameter's units: turning an encoder moves it by the coarse step, and turning it while held moves it by the fine step.

```cpp
std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    return { { "vca", Colours::orange, { { &level_, 0.05f, 0.005f } } } };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new ssp::engine::EngineEditor(*this, pages(), {}, {}), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
```

`useCompactUI()` is true inside rack, which shows the compact editor. The `EngineEditor` arguments after the pages are:

- the parameters for soft keys 1 to 8, or none;
- the folder the Load browser opens at.

Soft key 5 is always Load. `svca` loads nothing and ignores it. A plugin that loads files subclasses `EngineEditor` and overrides `loaded()`; see `plugins/radio/Source/PluginEditor.h`.

## 4. The SSP entry points

`Source/SSPApi.cpp` exports the four C functions Synthor calls. Only the colour and the category change from plugin to plugin.

```cpp
static const Colour colour(240, 160, 40);

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginDescriptor* createDescriptor() {
    auto desc = new Percussa::SSP::PluginDescriptor;
    SSP_defaultDescriptor(desc);
    desc->colour = colour.getARGB();
    return desc;
}

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginInterface* createInstance() {
    return new SSP_PluginInterface(new PluginProcessor());
}

extern "C" __attribute__((visibility("default"))) bool apiExtensions() {
    return true;
}

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginDescriptor* createExtendedDescriptor() {
    auto desc = new SSPExtendedApi::PluginDescriptor;
    SSP_defaultDescriptor(desc);
    desc->colour = colour.getARGB();
    desc->supportCompactUI_ = true;  // rack can host it
    desc->categories_.push_back(CAT_UTILITY);
    return desc;
}
```

The categories are the `CAT_` macros in `plugins/common/SSPApi.h`.

## 5. CMake

`CMakeLists.txt` names the plugin and lists its sources. The project name is the CMake target, and the build puts the plugin in `<PROJECT>_artefacts`.

```cmake
cmake_minimum_required(VERSION 3.15)
project(SVCA VERSION 0.1.0)

set(COMMON "${PROJECT_SOURCE_DIR}/../../plugins/common")
include_directories("${COMMON}")

juce_add_plugin(SVCA
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "stereo VCA"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE SVCA
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "svca")

include(${COMMON}/CMakeLists.txt)
include(${COMMON}/engine/CMakeLists.txt)

target_sources(SVCA
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        )
```

The rest of the file, compile definitions and the JUCE link, is the same in every plugin; copy it from [`examples/svca/CMakeLists.txt`](../examples/svca/CMakeLists.txt).

- `PRODUCT_NAME` is the four-character name; `PLUGIN_CODE` is the same in capitals.
- `DESCRIPTION` appears on the editor's title line and in the release's plugin table.
- `SSP_VERSION_FROM_PROJECT=1` shows the `project()` version on screen.
- Add `${STREAM_SRC}` to read audio files, `${FAUST_SRC}` for a Faust kernel, `${SCRIPT_SRC}` for a script host. `plugins/common/engine/CMakeLists.txt` lists them.

Add the folder to `plugins/CMakeLists.txt`:

```cmake
add_subdirectory(svca)
```

## 6. Build and install

```
make                    # cross build for the SSP
make install MOD=svca   # copy svca.so to the mounted card's BOOT/plugins
```

Restart the SSP. `svca` appears in Synthor's module list.

## 7. Test

Two kinds of test, both run by `make test`.

**The engine, natively.** Build the engine with a plain compiler and check its output. Most plugins do this in `plugins/<dir>/tests/engine_test.cpp`, driven by a pytest file; see `plugins/chorus/tests`.

**The plugin, through the SSP API.** `plugins/common/tests/test_engine_plugins.py` builds each plugin for the host and drives it with `plugin_host`. Add the plugin to `PRODUCTS` (folder name to product name), then add a test:

```python
def test_svca(plugins):
    # Level 0.5 is a gain of 0.5; a CV of 0.25 adds 0.25
    levels, _ = run(plugins, "svca", "set", "level", 0.5, "prepare", 48000, 128, "in", 0, 0.4, "in", 1, -0.2,
                    "run", 0.1, 128, "level", 0, "level", 1)
    assert levels[0][1] == pytest.approx(0.2, abs=1e-4)
    assert levels[1][1] == pytest.approx(-0.1, abs=1e-4)
```

`plugin_host`'s commands:

| Command | What |
|-|-|
| `set ID VALUE` | writes a parameter's plain value into the state, as a preset does |
| `prepare RATE BLOCK` | prepares the plugin |
| `in CH VALUE` | holds input CH (from 0) at a constant |
| `run SECONDS BLOCK` | processes that much audio in blocks of BLOCK |
| `level CH` | prints output CH's mean absolute level over the last block, and its last sample |
| `state` | prints the saved state |
| `load FILE` | restores a state saved to FILE |
| `wait SECONDS` | sleeps, so the worker thread can finish a load |
| `channels` | prints the jack names |
| `render FILE` | draws the editor into FILE, 1600x480 BGRA |
| `button N V`, `encoder N V` | presses (1) or releases (0) button N; turns encoder N by V. Run `render /dev/null` first: the editor handles them |

`test_local_manifest_matches_the_plugins` then fails, because rack's preset checker does not know the plugin yet. Rewrite the manifest:

```
UPDATE_MANIFEST=1 uv run --with pytest pytest plugins/common/tests -k manifest
```

This writes `tools/py2rack/modules-local.json`, the jack and parameter names py2rack checks presets against.

## 8. Document it

Add `plugins/<dir>/README.md` and `CHANGELOG.md`, and a row to the table in the root `README.md`. `make release` copies each plugin's README and CHANGELOG into the package and lists the plugin from its `CMakeLists.txt`.
