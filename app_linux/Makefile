# ch570mon — htop 风格的 CH570 无线电流监测终端工具
#
# 依赖：ncurses 开发库（宽字符版优先，用于 UTF-8 块字符曲线）
#   Debian/Ubuntu:  sudo apt install libncursesw5-dev   (或 libncurses-dev)
#   Fedora:         sudo dnf install ncurses-devel
#   Arch:           sudo pacman -S ncurses

CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
TARGET   = ch570mon

# 优先 ncursesw（宽字符/UTF-8），回退 ncurses
NC_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null)
NC_CFL  := $(shell pkg-config --cflags ncursesw 2>/dev/null)
ifeq ($(strip $(NC_LIBS)),)
NC_LIBS := $(shell pkg-config --libs ncurses 2>/dev/null)
NC_CFL  := $(shell pkg-config --cflags ncurses 2>/dev/null)
endif
ifeq ($(strip $(NC_LIBS)),)
NC_LIBS := -lncursesw
endif

LDLIBS := $(NC_LIBS) -lm

all: $(TARGET)

$(TARGET): ch570mon.c
	$(CC) $(CFLAGS) $(NC_CFL) -o $@ ch570mon.c $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
