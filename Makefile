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
# The Level 2 command set builds into a root of its own, so the two can be
# compared without either disturbing the other:
#
#     make os9root-l2   build the Level 2 root into ~/OS9L2
#     make test-l2      the golden-output tests against it
#     make survey-l2    the survey against it
#
# The emulator alone needs nothing but a C++ compiler. Building the OS-9 root
# needs lwasm, lwlink and ToolShed's os9 on PATH; ./coco-dev provides them in a
# container if the host has none.

EMUDIR   := os9emu/OS9
BUILDDIR := build
EMU      := $(BUILDDIR)/os9emu

OS9ROOT    ?= $(HOME)/OS9
OS9ROOT_L2 ?= $(HOME)/OS9L2
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

# The Level 2 "coco3" port, in its own root. Most of its modules are the same
# sources as Level 1's; the ones that differ ask the kernel for a copy of its
# tables rather than reading them out of the direct page, which is a question
# a hosted emulator can answer. See "Level 2" in CLAUDE.md.
os9root-l2 test-l2 survey-l2: OS9ROOT := $(OS9ROOT_L2)

os9root-l2:
	@scripts/os9root.sh --level 2

test-l2: $(EMU)
	@tests/run.sh

survey-l2: $(EMU)
	@scripts/survey.sh

check: os9root test

clean:
	@$(MAKE) --no-print-directory -C $(EMUDIR) clean
	rm -rf $(BUILDDIR)

.PHONY: all debug os9root os9root-rebuild os9root-l2 test test-update test-l2 \
	survey survey-l2 check clean FORCE
FORCE:
