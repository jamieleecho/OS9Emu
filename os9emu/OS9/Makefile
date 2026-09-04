TARGET=os9emu
CPPFLAGS=-g -I/sw/include/root
LDFLAGS=-g
LDLIBS=
SOURCES=$(wildcard *.cc)
OBJS=$(SOURCES:.cc=.o)


$(TARGET): $(OBJS)
	g++ $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cc
	g++ $(CPPFLAGS) -c $< -o $@

install:
	cp $(TARGET) /usr/local/bin

clean:
	rm -f $(TARGET) $(OBJS)
