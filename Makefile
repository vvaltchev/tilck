
# This is a commodity fake Makefile that allows people to run the build from the
# project's root directory, instead of entering in the build/ directory first.

MAKEFLAGS += --no-print-directory

TCROOT_PARENT ?= ./
TCROOT ?= $(TCROOT_PARENT)/toolchain4
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
