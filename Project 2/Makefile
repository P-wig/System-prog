CC       := gcc
CFLAGS   := -std=gnu99 -Wall -Wextra -g -Icommon/include
SRVFLAGS := -Iyashd/include -pthread
CLIFLAGS := -Iyash/include

BINDIR   := bin
OBJDIR   := build

SRV_SRCS := $(wildcard yashd/src/*.c)
CLI_SRCS := $(wildcard yash/src/*.c)
SRV_OBJS := $(patsubst yashd/src/%.c,$(OBJDIR)/yashd/%.o,$(SRV_SRCS))
CLI_OBJS := $(patsubst yash/src/%.c,$(OBJDIR)/yash/%.o,$(CLI_SRCS))
DEPS     := $(SRV_OBJS:.o=.d) $(CLI_OBJS:.o=.d)

.PHONY: all server client clean

all: server client

server: $(BINDIR)/yashd
client: $(BINDIR)/yash

$(BINDIR)/yashd: $(SRV_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(SRVFLAGS) -o $@ $^

$(BINDIR)/yash: $(CLI_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(CLIFLAGS) -o $@ $^

$(OBJDIR)/yashd/%.o: yashd/src/%.c | $(OBJDIR)/yashd
	$(CC) $(CFLAGS) $(SRVFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/yash/%.o: yash/src/%.c | $(OBJDIR)/yash
	$(CC) $(CFLAGS) $(CLIFLAGS) -MMD -MP -c $< -o $@

$(BINDIR) $(OBJDIR)/yashd $(OBJDIR)/yash:
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

-include $(DEPS)
