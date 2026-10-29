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
	 # 2. Extract Visual Studio directory
    VS_RAW := $(shell "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    
    # 3. Find the newest MSVC tool version directory
    MSVC_VER := $(shell dir /B /O:N "$(VS_RAW)\VC\Tools\MSVC" | findstr /R "^[0-9]")
    MSVC_DIR := $(VS_RAW)\VC\Tools\MSVC\$(lastword $(MSVC_VER))
    MSVC_INC := $(MSVC_DIR)\include
    # Target x64 libraries (change to \x86 if building a 32-bit binary)
    MSVC_LIB := $(MSVC_DIR)\lib\x64

    # 4. Pull the official Windows SDK location out of the Registry
    SDK_RAW  := $(shell reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v "KitsRoot10" 2>NUL | findstr KitsRoot10)
    SDK_DIR  := $(wordlist 3, $(words $(SDK_RAW)), $(SDK_RAW))
    
    # 5. Extract the newest SDK release iteration
    SDK_VER  := $(shell dir /B /O:N "$(SDK_DIR)\Include" | findstr /R "^10\.")
    SDK_BASE := $(SDK_DIR)Include\$(lastword $(SDK_VER))
    # Target SDK library base
    SDK_LIB_BASE := $(SDK_DIR)Lib\$(lastword $(SDK_VER))

    # 6. Build Include Flags
    SYS_INCLUDES := -isystem "$(MSVC_INC)" \
                    -isystem "$(SDK_BASE)\ucrt" \
                    -isystem "$(SDK_BASE)\shared" \
                    -isystem "$(SDK_BASE)\um"

    # 7. Build Linker Flags (Passes paths down through Clang to lld-link)
    SYS_LDFLAGS  := -Xlinker /libpath:"$(MSVC_LIB)" \
                    -Xlinker /libpath:"$(SDK_LIB_BASE)\ucrt\x64" \
                    -Xlinker /libpath:"$(SDK_LIB_BASE)\um\x64"
else ifeq ($(UNAME_S),Linux)
	PLATFORM := linux
	SYS_INCLUDES :=
    SYS_LDFLAGS  :=
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
	SYS_INCLUDES :=
    SYS_LDFLAGS  :=
else
    $(error Unsupported platform)
	SYS_INCLUDES :=
    SYS_LDFLAGS  :=
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

link-win32: LDFLAGS += -fdeclspec -Wno-cast-function-type-mismatch -luser32 -lgdi32 -lwinmm -Xlinker /INCREMENTAL $(SYS_LDFLAGS)
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
	clang src/ktime.c $(CFLAGS) -c -o obj/ktime.c.o $(DEFINES) $(INCLUDE_FLAGS) $(SYS_INCLUDES)

compile-common:
	clang src/versiongen.c $(CFLAGS) -c -o obj/versiongen.c.o $(DEFINES) $(INCLUDE_FLAGS) $(SYS_INCLUDES)
	clang src/cfgen.c $(CFLAGS) -c -o obj/cfgen.c.o $(DEFINES) $(INCLUDE_FLAGS) $(SYS_INCLUDES)

