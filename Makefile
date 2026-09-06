# os9emu -- a TTY OS-9 Level 1 emulator
#
#     make              build build/os9emu
#     make os9root      build the OS-9 root from ../nitros9 into ~/OS9
#     make test         run the golden-output tests
#     make survey       run every installed command and report how it fared
#     make check        os9root + test
#     make debug        build the sanitizer binary
#     make clean        remove build products
#
# The emulator alone needs nothing but a C++ compiler. Building the OS-9 root
# needs lwasm, lwlink and ToolShed's os9 on PATH; ./coco-dev provides them in a
# container if the host has none.

EMUDIR   := os9emu/OS9
BUILDDIR := build
EMU      := $(BUILDDIR)/os9emu

OS9ROOT  ?= $(HOME)/OS9
NITROS9DIR ?= $(CURDIR)/../nitros9

export OS9ROOT NITROS9DIR

all: $(EMU)

$(EMU): FORCE
	@$(MAKE) --no-print-directory -C $(EMUDIR)

debug: FORCE
	@$(MAKE) --no-print-directory -C $(EMUDIR) debug

# Build the Level 1 commands, Basic09 and the C compiler out of NitrOS-9 and
# install them where the emulator will look for them.
os9root:
	@scripts/os9root.sh

os9root-rebuild:
	@scripts/os9root.sh --rebuild

test: $(EMU)
	@tests/run.sh

# Regenerate the golden files. Read the diff before committing the result --
# this happily records a regression as the new expected output.
test-update: $(EMU)
	@tests/run.sh --update

survey: $(EMU)
	@scripts/survey.sh

check: os9root test

clean:
	@$(MAKE) --no-print-directory -C $(EMUDIR) clean
	rm -rf $(BUILDDIR)

.PHONY: all debug os9root os9root-rebuild test test-update survey check clean FORCE
FORCE:
