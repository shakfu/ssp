# Building

The plugins are cross-compiled for the SSP's ARM CPU. The build is tested on Ubuntu 24.04 with clang 18 and CMake 3.28.

## Setup

```
git submodule update --init --recursive
sudo apt install cmake clang lld pkg-config curl zip flex bison libasound2-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxrender-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev llvm-dev
```

`llvm-dev` is for the host tests of the faust plugin; the SSP build uses the buildroot's LLVM.

The X11, freetype and fontconfig headers are for `juceaide`, a tool JUCE builds for the host during configure. flex, bison and the ALSA headers are for ChucK.

## Build

```
make
```

On first run, `make` downloads the SSP buildroot (608 MB) into `./buildroot`. Set `SSP_BUILDROOT` to use an existing one. It then configures with the `ssp toolchain` preset (`xcSSP.cmake`) and builds into `build.cmake.ssp`.

The csound, chuck and faust plugins link Csound, ChucK and libfaust, which `make deps` downloads and cross-builds into `build/deps/ssp` (about three minutes). Without them, `make` builds the other plugins and skips these three. Run `make configure` after `make deps`.

Plugins land in `build.cmake.ssp/plugins/*/*_artefacts/Release/VST3/*.vst3/Contents/armv7l-linux/*.so`.

| target | action |
|-|-|
| `make` | build all plugins |
| `make configure` | re-run CMake configure |
| `make buildroot` | download the buildroot only |
| `make deps` | build libsndfile, Csound, ChucK and libfaust for the SSP (`scripts/build_deps.sh ssp`) |
| `make install [MOD=sfct]` | copy all plugins, or one, to the mounted SD card |
| `make install-faust` | copy the Faust libraries and `fstr` examples to the card's `BOOT/faust` |
| `make install-presets [PRESETS=dir]` | check JSON presets with `py2rack`, then copy `presets/` or `dir` to the card |
| `make deploy` | copy all plugins to the SSP over `scp` |
| `make deploy-mod MOD=sfct` | copy one plugin over `scp` |
| `make release` | strip into `releases/ssp/plugins`, package `ssp_plugins.zip` |
| `make test` | run the tests |
| `make clean` | remove `build.cmake.ssp` |

| variable | default |
|-|-|
| `SSP_BUILDROOT` | `./buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot` |
| `SSP_PLUGINS` | `/media/$USER/BOOT/plugins` |
| `SSP_PRESETS` | `/media/$USER/BOOT/rack_presets` |
| `SSP_HOST` | `root@192.168.0.150` |
| `JOBS` | number of CPUs |

`install-presets` copies to BOOT, which needs no `sudo`. It uses `sudo` only for a destination that is not writable, such as one on rootfs.

## Tests

`make test` runs pytest through `uv`, so it needs `uv` as well as the packages above.

| tests | what they check |
|-|-|
| `tools/py2rack/tests` | preset encoding and decoding |
| `plugins/sfct/tests` | the sfct engine, built natively |
| `plugins/rack/tests` | rack's execution order; `Track` under ThreadSanitizer; JSON `matrix` load and save |
| `plugins/radio/tests`, `plugins/csound/tests`, `plugins/chuck/tests` | each engine, built natively, and under ThreadSanitizer |
| `plugins/common/tests` | radio, csound and chuck built for the host and driven through the SSP API |

The `Track` tests build part of JUCE for the host into `build/rack-host`. The first run takes about a minute; later runs are incremental.

The csound and chuck tests build the libraries for the host into `build/deps/host` on first run (network, about a minute). `plugins/common/tests` then builds three plugins into `build/plugins-host` (about four minutes the first time). See [docs/dev/engines.md](dev/engines.md).

## Other hosts

Only the Linux build is tested. For cross-compiling on macOS, see Percussa's [SSP SDK topic](https://forum.percussa.com/t/creating-modules-for-the-ssp-aka-ssp-sdk-updated).
