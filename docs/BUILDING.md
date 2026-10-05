# Building

The plugins are cross-compiled for the SSP's ARM CPU. The build is tested on Ubuntu 24.04 with clang 18 and CMake 3.28.

## Setup

```
git submodule update --init --recursive
sudo apt install cmake clang lld pkg-config curl zip \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxrender-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev
```

The X11, freetype and fontconfig headers are for `juceaide`, a tool JUCE builds for the host during configure.

## Build

```
make
```

On first run, `make` downloads the SSP buildroot (608 MB) into `./buildroot`. Set `SSP_BUILDROOT` to use an existing one. It then configures with the `ssp toolchain` preset (`xcSSP.cmake`) and builds into `build.cmake.ssp`.

Plugins land in `build.cmake.ssp/plugins/*/*_artefacts/Release/VST3/*.vst3/Contents/armv7l-linux/*.so`.

| target | action |
|-|-|
| `make` | build all plugins |
| `make configure` | re-run CMake configure |
| `make buildroot` | download the buildroot only |
| `make install [MOD=sfct]` | copy all plugins, or one, to the mounted SD card |
| `make deploy` | copy all plugins to the SSP over `scp` |
| `make deploy-mod MOD=sfct` | copy one plugin over `scp` |
| `make release` | strip into `releases/ssp/plugins`, package `ssp_plugins.zip` |
| `make test` | run the tests |
| `make clean` | remove `build.cmake.ssp` |

| variable | default |
|-|-|
| `SSP_BUILDROOT` | `./buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot` |
| `SSP_PLUGINS` | `/media/$USER/BOOT/plugins` |
| `SSP_HOST` | `root@192.168.0.150` |
| `JOBS` | number of CPUs |

## Tests

`make test` runs pytest through `uv`, so it needs `uv` as well as the packages above.

| tests | what they check |
|-|-|
| `tools/py2rack/tests` | preset encoding and decoding |
| `plugins/sfct/tests` | the sfct engine, built natively |
| `plugins/rack/tests` | rack's execution order, and `Track` under ThreadSanitizer |

The ThreadSanitizer test builds part of JUCE for the host into `build/rack-tsan`. The first run takes about a minute; later runs are incremental.

## Other hosts

Only the Linux build is tested. For cross-compiling on macOS, see Percussa's [SSP SDK topic](https://forum.percussa.com/t/creating-modules-for-the-ssp-aka-ssp-sdk-updated).
