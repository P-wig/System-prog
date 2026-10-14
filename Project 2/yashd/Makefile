CC       := gcc
CFLAGS   := -std=gnu99 -Wall -Wextra -g -Ishared/include
SRVFLAGS := -Iserver/include -pthread
CLIFLAGS := -Iclient/include

OBJDIR   := build

SRV_SRCS := $(wildcard server/src/*.c)
CLI_SRCS := $(wildcard client/src/*.c)
SRV_OBJS := $(patsubst server/src/%.c,$(OBJDIR)/server/%.o,$(SRV_SRCS))
CLI_OBJS := $(patsubst client/src/%.c,$(OBJDIR)/client/%.o,$(CLI_SRCS))
DEPS     := $(SRV_OBJS:.o=.d) $(CLI_OBJS:.o=.d)

.PHONY: all clean

all: yashd yash

yashd: $(SRV_OBJS)
	$(CC) $(CFLAGS) $(SRVFLAGS) -o $@ $^

yash: $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CLIFLAGS) -o $@ $^

$(OBJDIR)/server/%.o: server/src/%.c | $(OBJDIR)/server
	$(CC) $(CFLAGS) $(SRVFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/client/%.o: client/src/%.c | $(OBJDIR)/client
	$(CC) $(CFLAGS) $(CLIFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/server $(OBJDIR)/client:
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) yashd yash

-include $(DEPS)
