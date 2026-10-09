# One-step cross build of the SSP plugins. See docs/BUILDING.md.

SHELL := bash
.SHELLFLAGS := -eo pipefail -c

BUILDROOT_URL := https://sw13072022.s3.us-west-1.amazonaws.com/arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz
BUILDROOT_DIR := buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot
BUILD_DIR := $(CURDIR)/build.cmake.ssp
PRESET := ssp toolchain
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

export BUILD_DIR SSP_HOST

# download the buildroot only when no external one is configured
ifeq ($(SSP_BUILDROOT)$(BUILDROOT),)
BUILDROOT_DEP := $(BUILDROOT_DIR)
endif

.DEFAULT_GOAL := ssp
.PHONY: ssp configure buildroot deps faust-kernels release release-pdf release-notes publish clean-releases deploy deploy-mod install install-faust install-presets test clean help

help:
	@echo "make [ssp]              download buildroot if needed, configure, build all plugins"
	@echo "make configure          re-run cmake configure"
	@echo "make buildroot          download and extract the SSP buildroot into ./buildroot"
	@echo "make deps               build Csound, ChucK and libfaust for the SSP (then re-run make configure)"
	@echo "make faust-kernels      regenerate the Faust kernel headers (needs uv)"
	@echo "make release            build, package releases/shakfu-ssp-plugins-$$(cat VERSION) and its zip, then release-notes"
	@echo "make release-pdf        as release, with the docs as PDFs rendered by quarto"
	@echo "make publish            upload the release zip and notes to GitHub (gh); needs the tag pushed"
	@echo "make clean-releases     delete releases/: every version's package, zip and notes"
	@echo "make release-notes      write CHANGELOG.md's section for VERSION to releases/shakfu-ssp-plugins-<VERSION>-notes.md"
	@echo "make deploy             build, copy all plugins to SSP_HOST"
	@echo "make deploy-mod MOD=x   build, copy one plugin to SSP_HOST"
	@echo "make install [MOD=x]    build, copy plugins to the mounted SD card (SSP_PLUGINS)"
	@echo "make install-faust      copy the Faust libraries and fstr examples to the card"
	@echo "make install-presets     check, then copy presets/ (or PRESETS=dir) to the card (SSP_PRESETS)"
	@echo "make test               run the py2rack and plugin tests"
	@echo "make clean              remove $(BUILD_DIR)"
	@echo "variables: SSP_BUILDROOT, SSP_HOST (root@192.168.0.150), SSP_PLUGINS (/media/$$USER/BOOT/plugins), SSP_PRESETS (/media/$$USER/BOOT/rack_presets), JOBS ($(JOBS))"

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

# the csound and chuck plugins are configured only once their libraries exist
deps: | $(BUILDROOT_DEP)
	scripts/build_deps.sh ssp

# kernel headers are committed, so the build does not need cyfaust
faust-kernels:
	scripts/faust_kernel.sh plugins/chorus/Source/chorus.dsp plugins/chorus/Source/ChorusKernel.h chorus
	scripts/faust_kernel.sh examples/tremolo/Source/tremolo.dsp examples/tremolo/Source/TremoloKernel.h tremolo

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

deploy-mod: ssp
	scripts/copymod.ssp.sh $(MOD)

install: ssp
	scripts/install.sh $(MOD)

install-faust:
	scripts/install-faust.sh

install-presets:
	scripts/install-presets.sh $(PRESETS)

test:
	uv run --quiet --with pytest pytest tools scripts $(wildcard plugins/*/tests) -q

clean:
	rm -rf $(BUILD_DIR)
