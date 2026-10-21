# Kohi Utils makefile

# All compiler flags/rules used across the board.
CFLAGS =-std=c99 -Wall -Wextra -Werror -Wno-error=deprecated-declarations -Wno-error=unused-function 
CFLAGS +=-Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -Wstrict-prototypes -Wno-unused-parameter 
CFLAGS +=-Wno-missing-field-initializers -Wno-tautological-compare 
# NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say otherwise are exported (i.e. via KAPI).
CFLAGS +=-fvisibility=hidden

# Base linker flags
LDFLAGS = -Lbin -Llib

# Base include flags
INCLUDE_FLAGS = -Isrc

DEFINES =-DKIMPORT

VG_ASSEMBLY_NAME =versiongen
CFG_ASSEMBLY_NAME =cfgen
KTIME_ASSEMBLY_NAME=ktime

VG_OUTPUT_NAME =
CFG_OUTPUT_NAME =
KTIME_OUTPUT_NAME =

UNAME_S := 
ifneq ($(OS),Windows_NT)
	UNAME_S = $(shell uname -s)
endif
ifeq ($(OS),Windows_NT)
	PLATFORM := win32
else ifeq ($(UNAME_S),Linux)
	PLATFORM := linux
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
else
    $(error Unsupported platform)
endif

# =========================================================
# BEGIN TARGET RECIPES
# =========================================================
.PHONY: all-release all-debug flags-debug flags-release 
.PHONY: genversion genversion-win32 genversion-linux genversion-macos 
.PHONY: scaffold scaffold-win32 scaffold-linux scaffold-macos 
.PHONY: link link-win32 link-linux link-macos

all-release: scaffold flags-release compile link
all-debug: scaffold flags-debug compile link

flags-debug: DEFINES += -D_DEBUG
flags-debug: CFLAGS += -g -MD -O0 -fno-omit-frame-pointer
flags-debug: LDFLAGS += -g
flags-debug: link compile

flags-release: DEFINES += -DKOHI_RELEASE
flags-release: CFLAGS += -MD -O2
flags-release: LDFLAGS += 
flags-release: link compile

scaffold: scaffold-$(PLATFORM)

.NOTPARALLEL: scaffold

scaffold-win32:
	@if not exist obj mkdir obj
	@if not exist bin mkdir bin

scaffold-linux scaffold-macos:
	@mkdir -p bin
	@mkdir -p obj

.PHONY: clean
clean:    clean-$(PLATFORM)

clean-win32:
	if exist bin del /s /q bin\*.*
	if exist obj del /s /q obj\*.*

clean-linux clean-macos:
	@rm -rf bin/*
	@rm -rf obj/*


link: link-$(PLATFORM)

link-win32: LDFLAGS += -fdeclspec -Wno-cast-function-type-mismatch -luser32 -lgdi32 -lwinmm -Xlinker /INCREMENTAL 
link-win32: DEFINES += -D_CRT_SECURE_NO_WARNINGS -DUNICODE
link-win32: VG_OUTPUT_NAME = bin\$(VG_ASSEMBLY_NAME).exe
link-win32: CFG_OUTPUT_NAME = bin\$(CFG_ASSEMBLY_NAME).exe
link-win32: KTIME_OUTPUT_NAME = bin\$(KTIME_ASSEMBLY_NAME).exe

# 		NOTE: --no-undefined and --no-allow-shlib-undefined ensure that symbols linking against are resolved.
# 		These are linux-specific, as the default behaviour is the opposite of this, allowing code to compile 
# 		here that would not on other platforms from not being exported (i.e. Windows)
# 		Discovered the solution here for this: https://github.com/ziglang/zig/issues/8180
link-linux: CFLAGS += -Wno-cast-function-type-mismatch -fPIC
link-linux: LDFLAGS += -lxcb -lX11 -lX11-xcb -lxkbcommon -lxcb-xkb -lm -ldl -L/usr/X11R6/lib -Wl,--no-undefined,--no-allow-shlib-undefined,-rpath='$$ORIGIN'
link-linux: VG_OUTPUT_NAME = bin/$(VG_ASSEMBLY_NAME)
link-linux: CFG_OUTPUT_NAME = bin/$(CFG_ASSEMBLY_NAME)
link-linux: KTIME_OUTPUT_NAME = bin/$(KTIME_ASSEMBLY_NAME)

link-macos: LDFLAGS += -lobjc -framework AppKit -framework QuartzCore -framework DiskArbitration -framework CoreFoundation
link-macos: VG_OUTPUT_NAME = bin/$(VG_ASSEMBLY_NAME)
link-macos: CFG_OUTPUT_NAME = bin/$(CFG_ASSEMBLY_NAME)
link-macos: KTIME_OUTPUT_NAME = bin/$(KTIME_ASSEMBLY_NAME)

link-win32 link-linux link-macos: link-common-vg link-common-cfg
link-win32: link-ktime-win32

link-common-vg: obj/versiongen.c.o
	clang obj/versiongen.c.o -o $(VG_OUTPUT_NAME) $(LDFLAGS)

link-common-cfg: obj/cfgen.c.o
	clang obj/cfgen.c.o -o $(CFG_OUTPUT_NAME) $(LDFLAGS)

link-ktime-win32: obj/ktime.c.o
	clang obj/ktime.c.o -o $(KTIME_OUTPUT_NAME) $(LDFLAGS)

obj/versiongen.c.o obj/cfgen.c.o: compile

.PHONY: compile
compile: compile-$(PLATFORM)

compile-macos compile-linux: compile-common

compile-win32: compile-common
	clang src/ktime.c $(CFLAGS) -c -o obj/ktime.c.o $(DEFINES) $(INCLUDE_FLAGS)

compile-common:
	clang src/versiongen.c $(CFLAGS) -c -o obj/versiongen.c.o $(DEFINES) $(INCLUDE_FLAGS)
	clang src/cfgen.c $(CFLAGS) -c -o obj/cfgen.c.o $(DEFINES) $(INCLUDE_FLAGS)

