#all: dplay.c
#	cc dplay.c wav_parser.c -lasound -o dplay
#
#clean:
#	rm -f dplay

TARGET:=dplay
SRCS = dplay.c wav_parser.c
#OBJS := $(SRCS:.c=.o)
OBJS = $(addsuffix .o,$(basename $(SRCS)))
LDFLAGS+=-lasound

%.o : %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

.PHONY: all

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)
