CC = gcc
CFLAGS = -O2 -Wall -Wextra -Iprotocols $(shell pkg-config --cflags wayland-client pangocairo)
LDFLAGS = $(shell pkg-config --libs wayland-client pangocairo) -lm

SRCS = ps5overlayd.c protocols/xdg-shell-protocol.c protocols/wlr-layer-shell-unstable-v1-protocol.c
OUT = ../ps5overlayd

all: $(OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $(OUT) $(SRCS) $(LDFLAGS)
	strip --strip-unneeded $(OUT)

clean:
	rm -f $(OUT)
