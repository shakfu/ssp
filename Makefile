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
.PHONY: ssp configure buildroot release deploy deploy-mod clean help

help:
	@echo "make [ssp]              download buildroot if needed, configure, build all plugins"
	@echo "make configure          re-run cmake configure"
	@echo "make buildroot          download and extract the SSP buildroot into ./buildroot"
	@echo "make release            build, strip into releases/ssp/plugins, zip tb_plugins_ssp.zip"
	@echo "make deploy             build, copy all plugins to SSP_HOST"
	@echo "make deploy-mod MOD=x   build, copy one plugin to SSP_HOST"
	@echo "make clean              remove $(BUILD_DIR)"
	@echo "variables: SSP_BUILDROOT, SSP_HOST (root@192.168.0.150), JOBS ($(JOBS))"

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

configure: | $(BUILDROOT_DEP)
	cmake --preset "$(PRESET)"

ssp: $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) -j$(JOBS)

release: ssp
	scripts/release.ssp.sh

deploy: ssp
	scripts/copybuild.ssp.sh

deploy-mod: ssp
	scripts/copymod.ssp.sh $(MOD)

clean:
	rm -rf $(BUILD_DIR)
