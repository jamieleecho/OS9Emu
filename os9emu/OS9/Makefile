# os9emu -- a TTY OS-9 Level 1 emulator
#
# Builds the emulator into ../../build/. Objects and generated dependency files
# live there too, so the source directory stays clean and the Xcode project
# (which compiles the same sources its own way) does not trip over them.
#
#     make            # build ../../build/os9emu
#     make debug      # same, with the address sanitizer, as os9emu-asan
#     make clean

TARGET   := os9emu
TOPDIR   := ../..
BUILDDIR := $(TOPDIR)/build
OBJDIR   := $(BUILDDIR)/obj

SOURCES  := $(wildcard *.cc)
OBJS     := $(SOURCES:%.cc=$(OBJDIR)/%.o)
DEPS     := $(OBJS:.o=.d)

# -Wno-unused-result: the 6809 core deliberately discards a few reads.
WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-unused-result
CXXFLAGS ?= -g -O2
CXXFLAGS += $(WARNINGS) -MMD -MP
LDFLAGS  ?= -g

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: %.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# A sanitizer build. Kept in its own directory so it never shares objects with
# the normal build -- mixing instrumented and uninstrumented objects links but
# misreports.
debug:
	$(MAKE) OBJDIR=$(BUILDDIR)/obj-asan \
	        BUILDDIR=$(BUILDDIR) TARGET=$(TARGET)-asan \
	        CXXFLAGS="-g -O0 -fsanitize=address -fno-omit-frame-pointer $(WARNINGS) -MMD -MP" \
	        LDFLAGS="-g -fsanitize=address"

install: $(BUILDDIR)/$(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(BUILDDIR)/$(TARGET) $(DESTDIR)/usr/local/bin

clean:
	rm -rf $(OBJDIR) $(BUILDDIR)/obj-asan \
	       $(BUILDDIR)/$(TARGET) $(BUILDDIR)/$(TARGET)-asan
	rm -f *.o $(TARGET)

.PHONY: all debug install clean

-include $(DEPS)
