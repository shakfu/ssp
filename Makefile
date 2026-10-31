# One-step cross build of the SSP plugins. See docs/BUILDING.md.

SHELL := bash
.SHELLFLAGS := -eo pipefail -c

BUILDROOT_URL := https://sw13072022.s3.us-west-1.amazonaws.com/arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz
BUILDROOT_DIR := buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot
BUILD_DIR := $(CURDIR)/build.cmake.ssp
PRESET := ssp toolchain
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
# the SSP over ssh, for deploy, the install targets, midisend and mididump
SSP_IP ?= 192.168.1.6
SSP_HOST ?= root@$(SSP_IP)

export BUILD_DIR SSP_HOST

# download the buildroot only when no external one is configured
ifeq ($(SSP_BUILDROOT)$(BUILDROOT),)
BUILDROOT_DEP := $(BUILDROOT_DIR)
endif

.DEFAULT_GOAL := ssp
.PHONY: ssp configure buildroot deps faust-kernels scsy-builtin release release-pdf release-notes publish clean-releases deploy deploy-mod install install-faust install-scsy install-sapf install-fsyn install-prdt install-brks install-presets midisend mididump test clean help

help:
	@echo "make [ssp]              download buildroot if needed, configure, build all plugins"
	@echo "make configure          re-run cmake configure"
	@echo "make buildroot          download and extract the SSP buildroot into ./buildroot"
	@echo "make deps               build Csound, ChucK, libfaust, libscsynth, FluidSynth and libmonome for the SSP, fetch teletype, sapf, alda, uSEQ and _core (then re-run make configure)"
	@echo "make faust-kernels      regenerate the Faust kernel headers (needs uv)"
	@echo "make scsy-builtin       regenerate scsy's built-in SynthDef header (needs uv)"
	@echo "make release            build, package releases/shakfu-ssp-plugins-$$(cat VERSION) and its zip, then release-notes"
	@echo "make release-pdf        as release, with the docs as PDFs rendered by quarto"
	@echo "make publish            upload the release zip and notes to GitHub (gh); needs the tag pushed"
	@echo "make clean-releases     delete releases/: every version's package, zip and notes"
	@echo "make release-notes      write CHANGELOG.md's section for VERSION to releases/shakfu-ssp-plugins-<VERSION>-notes.md"
	@echo "make deploy             build, copy all plugins to SSP_HOST"
	@echo "make deploy-mod MOD=x   build one plugin, copy it to SSP_HOST (Synthor stopped)"
	@echo "make install [MOD=x]    build, copy plugins to the mounted SD card (SSP_PLUGINS)"
	@echo "make install-faust      copy the Faust libraries and fstr examples to the card"
	@echo "make install-scsy       copy the UGen plugins and scsy examples to the card"
	@echo "make install-sapf       copy sapf's prelude and examples to the card"
	@echo "make install-fsyn       copy fsyn's soundfont, FluidR3_GM.sf2, to the card"
	@echo "make install-prdt       copy prdt's example patches and Pd's extra/ abstractions to the card"
	@echo "make install-brks       download _core's default sample banks, copy them to the card as brks's default"
	@echo "make midisend           build tools/midisend, copy it to SSP_HOST's /tmp and start it: a MIDI input for tests"
	@echo "make mididump           build tools/mididump, copy it to SSP_HOST's /tmp: prints a port's MIDI"
	@echo "make install-presets    check, then copy presets/ (or PRESETS=dir) to the card (SSP_PRESETS)"
	@echo "make test [MOD=x]       run the tools, scripts and plugin tests, or only plugin x's"
	@echo "make clean              remove $(BUILD_DIR)"
	@echo "variables: SSP_BUILDROOT, SSP_IP ($(SSP_IP)), SSP_HOST ($(SSP_HOST)), SSP_PLUGINS (/media/$$USER/BOOT/plugins), SSP_PRESETS (unset: the card), JOBS ($(JOBS))"

buildroot: $(BUILDROOT_DIR)

# extract into a staging dir so an interrupted download is not mistaken for a complete one
$(BUILDROOT_DIR):
	rm -rf buildroot/.part
	mkdir -p buildroot/.part
	curl -fSL $(BUILDROOT_URL) | tar xz -C buildroot/.part
	mkdir -p $(dir $@)
	mv buildroot/.part/$(notdir $(BUILDROOT_DIR)) $@
	rmdir buildroot/.part

$(BUILD_DIR)/Makefile: | $(BUILDROOT_DEP)
	cmake --preset "$(PRESET)"

# the plugins on these libraries and sources are configured only once they exist
deps: | $(BUILDROOT_DEP)
	scripts/build_deps.sh ssp
	scripts/fetch_teletype.sh
	scripts/fetch_sapf.sh
	scripts/fetch_alda.sh
	scripts/fetch_useq.sh
	scripts/fetch_core.sh ssp
	scripts/build_fluidsynth.sh ssp
	scripts/build_monome.sh ssp

# kernel headers are committed, so the build does not need cyfaust
faust-kernels:
	scripts/faust_kernel.sh plugins/chorus/Source/chorus.dsp plugins/chorus/Source/ChorusKernel.h chorus
	scripts/faust_kernel.sh examples/tremolo/Source/tremolo.dsp examples/tremolo/Source/TremoloKernel.h tremolo

# the header is committed, so the build does not need nanosynth
scsy-builtin:
	uv run --quiet --with nanosynth==0.4.0 python scripts/scsy_builtin.py

configure: | $(BUILDROOT_DEP)
	cmake --preset "$(PRESET)"

ssp: $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) -j$(JOBS)

release: ssp
	python3 scripts/release.py
	$(MAKE) --no-print-directory release-notes

# every version's package, zip and notes; make release replaces only its own version's files
clean-releases:
	rm -rf "$(CURDIR)/releases"

# uploads the zip and notes as the GitHub release for VERSION's tag; checks first, never tags or pushes
publish:
	scripts/publish.sh

# as release, with each README and CHANGELOG rendered to PDF; needs quarto and a LaTeX engine
release-pdf: ssp
	python3 scripts/release.py --pdf
	$(MAKE) --no-print-directory release-notes

# CHANGELOG.md's section for VERSION, as the GitHub release body; needs no build
release-notes:
	mkdir -p releases
	python3 scripts/release_notes.py "$$(cat VERSION)" -o "releases/shakfu-ssp-plugins-$$(cat VERSION)-notes.md"

deploy: ssp
	scripts/copybuild.ssp.sh

# builds only MOD's target; refuses while Synthor runs
deploy-mod: $(BUILD_DIR)/Makefile
	scripts/copymod.ssp.sh $(MOD)

install: ssp
	scripts/install.sh $(MOD)

install-faust:
	scripts/install-faust.sh

install-scsy:
	scripts/install-scsy.sh

install-sapf:
	scripts/install-sapf.sh

install-fsyn:
	scripts/install-fsyn.sh

install-prdt:
	scripts/install-prdt.sh

install-brks:
	scripts/install-brks.sh

# a MIDI input on the SSP that plays MIDI messages written to /tmp/midisend over ssh; /tmp is RAM, so a
# reboot removes it
MIDISEND_BR := $(or $(SSP_BUILDROOT),$(BUILDROOT),$(CURDIR)/$(BUILDROOT_DIR))
MIDISEND_SYSROOT := $(MIDISEND_BR)/arm-rockchip-linux-gnueabihf/sysroot
MIDISEND_GCC := $(MIDISEND_BR)/lib/gcc/arm-rockchip-linux-gnueabihf/8.4.0
MIDI_TOOL_CC := clang --target=arm-linux-gnueabihf --sysroot=$(MIDISEND_SYSROOT) -mcpu=cortex-a17 -mfloat-abi=hard -O2 \
	-fuse-ld=lld -L$(MIDISEND_SYSROOT)/lib -B$(MIDISEND_SYSROOT)/lib -L$(MIDISEND_GCC) -B$(MIDISEND_GCC)

midisend: | $(BUILDROOT_DEP)
	mkdir -p build/midisend
	$(MIDI_TOOL_CC) tools/midisend/midisend.c -lasound -o build/midisend/midisend
	scp -O -q build/midisend/midisend $(SSP_HOST):/tmp/midisend-bin
	ssh $(SSP_HOST) 'killall midisend-bin 2>/dev/null; nohup /tmp/midisend-bin /tmp/midisend > /tmp/midisend.log 2>&1 & sleep 1; cat /tmp/midisend.log'

# prints the MIDI of the SSP's ports whose names contain an argument (tools/mididump/mididump.c)
mididump: | $(BUILDROOT_DEP)
	mkdir -p build/mididump
	$(MIDI_TOOL_CC) tools/mididump/mididump.c -lasound -o build/mididump/mididump
	scp -O -q build/mididump/mididump $(SSP_HOST):/tmp/mididump

install-presets:
	scripts/install-presets.sh $(PRESETS)

# MOD is a module's name (scsy) or its folder (scsynth)
MOD_TESTS = $(sort $(wildcard plugins/$(MOD)/tests $(patsubst %/CMakeLists.txt,%/tests,$(shell grep -liE '^\s*PLUGIN_CODE\s+$(MOD)\b' plugins/*/CMakeLists.txt))))

test:
	uv run --quiet --with pytest pytest $(if $(MOD),$(or $(MOD_TESTS),$(error no tests for MOD=$(MOD))),tools scripts $(wildcard plugins/*/tests)) -q

clean:
	rm -rf $(BUILD_DIR)
