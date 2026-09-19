
# This is a commodity fake Makefile that allows people to run the build from the
# project's root directory, instead of entering in the build/ directory first.

MAKEFLAGS += --no-print-directory

TCROOT_PARENT ?= ./

# Which toolchain generation to look for. Single source of truth in
# other/toolchain_conf, shared with the bash bootstrap, the Ruby
# package manager and CMake: a generation bump moves every install
# path, and a consumer left behind looks for a directory that is not
# built any more.
TC_NAME := $(shell sed -n 's/^TOOLCHAIN_DIR_NAME=//p' other/toolchain_conf)
ifeq ($(TC_NAME),)
$(error TOOLCHAIN_DIR_NAME missing from other/toolchain_conf)
endif

TCROOT ?= $(TCROOT_PARENT)/$(TC_NAME)
PREREQUISITES := $(TCROOT) build/CMakeCache.txt
BUILD_DIR = build

all: $(PREREQUISITES)
	@$(MAKE) -C $(BUILD_DIR)

gtests: $(PREREQUISITES)
	@$(MAKE) -C $(BUILD_DIR) gtests

clean: $(PREREQUISITES)
	@$(MAKE) -C $(BUILD_DIR) clean

# Rem is a shortcut for rebuild_img
rem: $(PREREQUISITES)
	@rm -rf ./$(BUILD_DIR)/fatpart ./$(BUILD_DIR)/tilck.img
	@$(MAKE) -C $(BUILD_DIR)

rebuild_img: $(PREREQUISITES)
	@rm -rf ./$(BUILD_DIR)/fatpart ./$(BUILD_DIR)/tilck.img
	@$(MAKE) -C $(BUILD_DIR)

config: $(PREREQUISITES)
	@./$(BUILD_DIR)/run_config

menuconfig: $(PREREQUISITES)
	@./$(BUILD_DIR)/run_config

$(TCROOT):
	$(error Before building Tilck, you need to build the toolchain by running ./scripts/build_toolchain)

$(BUILD_DIR)/CMakeCache.txt:
	@echo No CMakeCache.txt found: running CMake first.
	@./scripts/cmake_run

.PHONY: all gtests clean
