# Kohi.Core.Tests makefile

ASSEMBLY_NAME=kohi.core.tests

# All compiler flags/rules used across the board.
CFLAGS =-std=gnu11 -Wall -Wextra -Werror -Wno-error=deprecated-declarations -Wno-error=unused-function 
CFLAGS +=-Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -Wstrict-prototypes -Wno-unused-parameter 
CFLAGS +=-Wno-missing-field-initializers -Wno-tautological-compare 
# NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say otherwise are exported (i.e. via KAPI).
CFLAGS +=-fvisibility=hidden

# Base linker flags
LDFLAGS = -Lbin -Llib -L../kohi.core/bin -lkohi.core 

# Base include flags
INCLUDE_FLAGS = -Isrc -I../kohi.core/src 

DEFINES =-DKEXPORT

OUTPUT_NAME =

BUILD_TYPE = U


UNAME_S := 
ifneq ($(OS),Windows_NT)
	UNAME_S = $(shell uname -s)
endif
ifeq ($(OS),Windows_NT)
	PLATFORM := win32

# 	Make does not offer a recursive wildcard function, and Windows needs one, so here it is:
	rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
	DIR := $(subst /,\,${CURDIR})
	SRC_FILES := $(call rwildcard,src,*.c)
	DIRECTORIES := \src $(subst $(DIR),,$(shell dir src /S /AD /B | findstr /i src)) 
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
else ifeq ($(UNAME_S),Linux)
	PLATFORM := linux
	SRC_FILES := $(shell find src -name "*.c")
	DIRECTORIES := $(shell find src -type d)
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
	SRC_FILES := $(shell find src -type f \( -name "*.c" -o -name "*.m" \))
	DIRECTORIES := $(shell find src -type d)
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
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

all-release: scaffold gen_compile_flags-release flags-release compile link genversion
all-debug: scaffold gen_compile_flags-debug flags-debug compile link genversion

flags-debug: DEFINES += -D_DEBUG
flags-debug: CFLAGS += -g -MD -O0 -fno-omit-frame-pointer
flags-debug: LDFLAGS += -g
flags-debug: BUILD_TYPE=D
flags-debug: link compile genversion

flags-release: DEFINES += -DKOHI_RELEASE
flags-release: CFLAGS += -MD -O2
flags-release: LDFLAGS += 
flags-release: BUILD_TYPE=R
flags-release: link compile genversion

scaffold: scaffold-$(PLATFORM)

.NOTPARALLEL: scaffold

scaffold-win32:
	-@mkdir $(addprefix obj\\, $(DIRECTORIES)) 2>nul
	@if not exist bin mkdir bin

scaffold-linux scaffold-macos:
	@mkdir -p bin
	@mkdir -p $(addprefix obj/,$(DIRECTORIES))
	

genversion: genversion-$(PLATFORM)

genversion-win32:
	@..\misc\versiongen.exe version.txt -outfile=src\$(ASSEMBLY_NAME)_version.h -build_type=$(BUILD_TYPE)

genversion-linux genversion-macos:
	@../misc/versiongen version.txt -outfile=src/$(ASSEMBLY_NAME)_version.h -build_type=$(BUILD_TYPE)


.PHONY: clean
clean:    clean-$(PLATFORM)

clean-win32:
	if exist bin del /s /q bin\*.*
	if exist obj del /s /q obj\*.*

clean-linux clean-macos:
	@rm -rf bin/*
	@rm -rf obj/*


link: link-$(PLATFORM)

 
link-win32: LDFLAGS += -fdeclspec -Wno-cast-function-type-mismatch -Xlinker /INCREMENTAL 
link-win32: DEFINES += -D_CRT_SECURE_NO_WARNINGS -DUNICODE
link-win32: OUTPUT_NAME = bin\$(ASSEMBLY_NAME).exe

# 		NOTE: --no-undefined and --no-allow-shlib-undefined ensure that symbols linking against are resolved.
# 		These are linux-specific, as the default behaviour is the opposite of this, allowing code to compile 
# 		here that would not on other platforms from not being exported (i.e. Windows)
# 		Discovered the solution here for this: https://github.com/ziglang/zig/issues/8180
link-linux: CFLAGS += -Wno-cast-function-type-mismatch -fPIC
link-linux: LDFLAGS += -lm -Wl,-rpath='$$ORIGIN'
link-linux: OUTPUT_NAME = bin/$(ASSEMBLY_NAME)

link-macos: LDFLAGS += 
link-macos: OUTPUT_NAME = bin/$(ASSEMBLY_NAME)

link-win32 link-linux link-macos: link-common

link-common: $(OBJ_FILES)
	@clang $(OBJ_FILES) -o $(OUTPUT_NAME) $(LDFLAGS)

.PHONY: compile
compile:
-include $(OBJ_FILES:.o=.d)


# compile .c to .o object for windows, linux and mac
obj/%.c.o: %.c 
	@echo   $<...
	@clang $< $(CFLAGS) -c -o $@ $(DEFINES) $(INCLUDE_FLAGS)

# compile .m to .o object only for macos
obj/%.m.o: %.m
	@echo   $<...
	@clang $< $(CFLAGS) -c -o $@ $(DEFINES) $(INCLUDE_FLAGS)

-include $(OBJ_FILES:.o=.d)

.PHONY: gen_compile_flags-debug
gen_compile_flags-debug: DEFINES += -D_DEBUG
gen_compile_flags-debug: gen_compile_flags

.PHONY: gen_compile_flags-release
gen_compile_flags-release: DEFINES += -DKOHI_RELEASE
gen_compile_flags-release: gen_compile_flags

.PHONY: gen_compile_flags
gen_compile_flags:
ifeq ($(BUILD_PLATFORM),windows)
	@..\misc\cfgen -outfile=compile_flags.txt $(INCLUDE_FLAGS) $(DEFINES) -ferror-limit=0
else
	@../misc/cfgen -outfile=compile_flags.txt $(INCLUDE_FLAGS) $(DEFINES) -ferror-limit=0
endif
