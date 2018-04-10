
# This is a commodity fake Makefile that allows people to run the build from the
# project's root directory, instead of entering in the build/ directory first.

MAKEFLAGS += --no-print-directory

PREREQUISITES := toolchain build/CMakeCache.txt

all: $(PREREQUISITES)
	@$(MAKE) -C build

gtests: $(PREREQUISITES)
	@$(MAKE) -C build gtests

clean: $(PREREQUISITES)
	@$(MAKE) -C build clean

rebuild_img: $(PREREQUISITES)
	@rm -rf ./build/fatpart ./build/exos.img
	@$(MAKE) -C build

toolchain:
	$(error Before building exOS, you need to build the toolchain by running ./scripts/build_toolchain)

build/CMakeCache.txt:
	@echo No CMakeCache.txt found: running CMake first.
	@./scripts/cmake_run

.PHONY: all gtests clean
