# Building

The plugins are cross-compiled for the SSP's ARM CPU. The build is tested on Ubuntu 24.04 with clang 18 and CMake 3.28.

## Setup

```text
git submodule update --init   # not --recursive: ssp-sdk nests a JUCE clone the build does not use
sudo apt install cmake clang lld pkg-config curl zip flex bison libasound2-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxrender-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev llvm-dev
```

Install [uv](https://docs.astral.sh/uv/) as well: `make deps` and `make test` run Python through it.

`llvm-dev` is for the host tests of the faust plugin; the SSP build uses the buildroot's LLVM.

The X11, freetype and fontconfig headers are for `juceaide`, a tool JUCE builds for the host during configure. flex, bison and the ALSA headers are for ChucK.

## Build

```text
make deps   # once: the libraries and sources nine plugins need (about 3 minutes)
make        # every plugin
```

The first `make deps` or `make` downloads the SSP buildroot (608 MB) into `./buildroot`. Set `SSP_BUILDROOT` to use an existing one. `make` configures with the `ssp toolchain` preset (`xcSSP.cmake`) and builds into `build.cmake.ssp`.

Without `make deps`, `make` skips 9 plugins: `csnd`, `chuk`, `fstr`, `scsy`, `ttyp`, `sapf`, `alda`, `brks` and `fsyn`. The `make deps` row below lists what it fetches and builds. If you run `make deps` after a first `make`, run `make configure` too.

Plugins land in `build.cmake.ssp/plugins/*/*_artefacts/Release/VST3/*.vst3/Contents/armv7l-linux/*.so`.

### Build targets

| target | action |
|-|-|
| `make` | build all plugins |
| `make configure` | re-run CMake configure |
| `make buildroot` | download the buildroot only |
| `make deps` | build libsndfile, Csound, ChucK, libfaust, libscsynth and the SC UGens for the SSP (`scripts/build_deps.sh ssp`), fetch the teletype sources (`scripts/fetch_teletype.sh`; builds ragel 6.10 if it is missing), fetch sapf and alda (`scripts/fetch_sapf.sh`, `scripts/fetch_alda.sh`), fetch _core and generate `brks`'s engine (`scripts/fetch_core.sh ssp`), and build FluidSynth and fetch its soundfonts (`scripts/build_fluidsynth.sh ssp`) |

### Install on the card

`make install` needs the card mounted on this computer; `make deploy` is its ssh form. The `install-*` targets copy to the mounted card, else to `SSP_HOST` over ssh. Over ssh each copy is synced and checked by md5 (`scripts/ssp-card.sh`).

| target | action |
|-|-|
| `make install [MOD=sfct]` | copy all plugins, or one, to the mounted card |
| `make install-faust` | copy the Faust libraries and `fstr` examples to the card's `BOOT/faust` |
| `make install-scsy` | copy the UGen plugins and `scsy` examples to the card |
| `make install-sapf` | copy sapf's prelude and examples to the card's `BOOT/sapf` |
| `make install-fsyn` | copy `FluidR3_GM.sf2` to the card's `BOOT/fsyn` |
| `make install-brks` | download _core's default sample banks (127 MB, checked against SHA-256) and copy them to the card's `BOOT/brks/default` |
| `make install-presets [PRESETS=dir]` | check JSON presets with `py2rack`, then copy `presets/` or `dir` to the card's `BOOT/rack_presets`, or to `SSP_PRESETS` when set |

### Over ssh

These reach the SSP at `SSP_HOST` ([Network access](#network-access)).

| target | action |
|-|-|
| `make deploy` | build, then copy all plugins to the SSP over `scp` |
| `make deploy-mod MOD=sfct` | build one plugin, copy it over `scp` and check its md5. It refuses while Synthor runs, since replacing a loaded plugin can crash it. Stop Synthor with `ssh $SSP_HOST 'kill -9 $(pidof SYNTHOR)'`; it then stays down until the SSP restarts |
| `make midisend` | build `tools/midisend`, copy it to `SSP_HOST`'s `/tmp` and start it: a MIDI input for tests |
| `make mididump` | build `tools/mididump` and copy it to `SSP_HOST`'s `/tmp`: prints the MIDI a port sends ([DEVELOPING.md](DEVELOPING.md#tools)) |

### Release

| target | action |
|-|-|
| `make release` | package `releases/shakfu-ssp-plugins-<VERSION>/`, laid out as the SD card's BOOT partition, and its zip, then runs `release-notes`; the version is in `VERSION` |
| `make release-pdf` | as `make release`, with the docs rendered to PDF by `quarto render --to pdf`; needs quarto and a LaTeX engine (`quarto install tinytex`) |
| `make release-notes` | write `CHANGELOG.md`'s `## [<VERSION>]` section to `releases/shakfu-ssp-plugins-<VERSION>-notes.md`, for the release body |
| `make publish` | create the GitHub release for tag `<VERSION>` with `gh`, attaching the zip and using the notes as its body. It refuses unless the tree is clean, the tag is `HEAD` and pushed, the zip is newer than `HEAD`, and no release exists; it never commits, tags or pushes |
| `make clean-releases` | delete `releases/`, with every version's package, zip and notes; `make release` replaces only its own version's |

### Maintenance

| target | action |
|-|-|
| `make test` | run the tools, scripts and plugin tests |
| `make faust-kernels` | regenerate the committed Faust kernel headers (needs `uv`) |
| `make scsy-builtin` | regenerate `scsy`'s committed built-in SynthDef header (needs `uv`) |
| `make clean` | remove `build.cmake.ssp` |

### Variables

| variable | default |
|-|-|
| `SSP_BUILDROOT` | `./buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot` |
| `SSP_PLUGINS` | `/media/$USER/BOOT/plugins` |
| `SSP_PRESETS` | unset: the card's `BOOT/rack_presets`. Set, a local directory, written with sudo where needed |
| `SSP_IP` | `192.168.1.6` |
| `SSP_HOST` | `root@$(SSP_IP)` |
| `JOBS` | number of CPUs |

### Network access

`make deploy`, `deploy-mod`, `midisend` and `mididump` use ssh, as do the `install-*` targets without a mounted card. The SSP has no Ethernet port; a USB-Ethernet adapter works in either USB port.

- The SSP comes up at `192.168.1.6/24`, static, when the adapter appears as `eth0` (`/etc/network/interfaces`, `/etc/udev/rules.d/90-usb-ethernet.rules` on rootfs). It runs no DHCP client or server.

- Give the host a static address on the same subnet, e.g. `192.168.1.200/24` with no gateway.

- Log in as `root` with a key. If the root password is unknown, add the key from the mounted card:

  ```text
  sudo mkdir -p /media/$USER/rootfs/root/.ssh
  sudo sh -c "cat $HOME/.ssh/id_rsa.pub >> /media/$USER/rootfs/root/.ssh/authorized_keys"
  sudo chmod 700 /media/$USER/rootfs/root/.ssh
  sudo chmod 600 /media/$USER/rootfs/root/.ssh/authorized_keys
  sudo chown -R 0:0 /media/$USER/rootfs/root/.ssh
  ```

## Tests

`make test` runs pytest through `uv`, so it needs `uv` as well as the packages above.

| tests | what they check |
|-|-|
| `tools/py2rack/tests` | preset encoding and decoding |
| `plugins/sfct/tests` | the sfct engine, built natively |
| `plugins/rack/tests` | rack's execution order; `Track` under ThreadSanitizer; JSON `matrix` load and save |
| `plugins/radio/tests`, `plugins/csound/tests`, `plugins/chuck/tests` | each engine, built natively, and under ThreadSanitizer |
| `plugins/common/tests` | every engine plugin, `sfct` and the `examples/` plugins, built for the host and driven through the SSP API; the shared keys, text and MIDI code |
| `plugins/*/tests` | each other plugin's engine, built natively |

The `Track` tests build part of JUCE for the host into `build/rack-host`. The first run takes about a minute; later runs are incremental.

The csound and chuck tests build the libraries for the host into `build/deps/host` on first run (network, about a minute). `plugins/common/tests` then builds the plugins into `build/plugins-host` (about four minutes the first time). See [docs/dev/engines.md](dev/engines.md).

## Other hosts

Only the Linux build is tested. For cross-compiling on macOS, see Percussa's [SSP SDK topic](https://forum.percussa.com/t/creating-modules-for-the-ssp-aka-ssp-sdk-updated).
