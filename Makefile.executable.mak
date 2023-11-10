BUILD_DIR := bin
OBJ_DIR := obj

# NOTE: ASSEMBLY must be set on calling this makefile

DEFINES := -DKIMPORT

# Detect OS and architecture.
ifeq ($(OS),Windows_NT)
    # WIN32
	BUILD_PLATFORM := windows
	EXTENSION := .exe
	COMPILER_FLAGS := -Wall -Werror -Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -fdeclspec -Wstrict-prototypes
	INCLUDE_FLAGS := -I$(ASSEMBLY)\src $(ADDL_INC_FLAGS)
	LINKER_FLAGS := -L$(BUILD_DIR) $(ADDL_LINK_FLAGS)
	DEFINES += -D_CRT_SECURE_NO_WARNINGS

# Make does not offer a recursive wildcard function, and Windows needs one, so here it is:
	rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
	DIR := $(subst /,\,${CURDIR})
	
	# .c files
	SRC_FILES := $(call rwildcard,$(ASSEMBLY)/,*.c)
	# directories with .h files
	DIRECTORIES := \$(ASSEMBLY)\src $(subst $(DIR),,$(shell dir $(ASSEMBLY)\src /S /AD /B | findstr /i src)) 
	OBJ_FILES := $(SRC_FILES:%=$(OBJ_DIR)/%.o)
    ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
        # AMD64
    else
        ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
            # AMD64
        endif
        ifeq ($(PROCESSOR_ARCHITECTURE),x86)
            # IA32
        endif
    endif
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        # LINUX
		BUILD_PLATFORM := linux
		EXTENSION := 
		# NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say
		# otherwise are exported (i.e. via KAPI).
		COMPILER_FLAGS :=-fvisibility=hidden -fpic -Wall -Werror -Wvla -Wno-missing-braces -fdeclspec
		INCLUDE_FLAGS := -I./$(ASSEMBLY)/src $(ADDL_INC_FLAGS)
		# NOTE: --no-undefined and --no-allow-shlib-undefined ensure that symbols linking against are resolved.
		# These are linux-specific, as the default behaviour is the opposite of this, allowing code to compile 
		# here that would not on other platforms from not being exported (i.e. Windows)
		# Discovered the solution here for this: https://github.com/ziglang/zig/issues/8180
		LINKER_FLAGS :=-Wl,--no-undefined,--no-allow-shlib-undefined -L./$(BUILD_DIR) $(ADDL_LINK_FLAGS) -Wl,-rpath,.
		# .c files
		SRC_FILES := $(shell find $(ASSEMBLY) -name *.c)
		# directories with .h files
		DIRECTORIES := $(shell find $(ASSEMBLY) -type d)
		OBJ_FILES := $(SRC_FILES:%=$(OBJ_DIR)/%.o)
    endif
    ifeq ($(UNAME_S),Darwin)
        # OSX
		BUILD_PLATFORM := macos
		EXTENSION := 
		# NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say
		# otherwise are exported (i.e. via KAPI).
		COMPILER_FLAGS :=-fvisibility=hidden -Wall -Werror -Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -fdeclspec -fPIC
		INCLUDE_FLAGS := -I./$(ASSEMBLY)/src $(ADDL_INC_FLAGS)
		# NOTE: Equivalent of the linux version above, this ensures that symbols linking against are resolved.
		# Discovered this here: https://stackoverflow.com/questions/26971333/what-is-clangs-equivalent-to-no-undefined-gcc-flag
		LINKER_FLAGS :=-Wl,-undefined,error -L./$(BUILD_DIR) $(ADDL_LINK_FLAGS) -Wl,-rpath,.
		# .c files
		SRC_FILES := $(shell find $(ASSEMBLY) -name *.c)
		# directories with .h files
		DIRECTORIES := $(shell find $(ASSEMBLY) -type d)
		OBJ_FILES := $(SRC_FILES:%=$(OBJ_DIR)/%.o)
    endif
    UNAME_P := $(shell uname -p)
    ifeq ($(UNAME_P),x86_64)
        # AMD64
    endif
    ifneq ($(filter %86,$(UNAME_P)),)
        # IA32
    endif
    ifneq ($(filter arm%,$(UNAME_P)),)
        # ARM
    endif
endif

# Defaults to debug unless release is specified.
ifeq ($(TARGET),release)
# release
DEFINES += -DKRELEASE
COMPILER_FLAGS += -MD -O2
else
# debug
DEFINES += -D_DEBUG
COMPILER_FLAGS += -g -MD -O0
LINKER_FLAGS += -g
endif

all: scaffold compile link gen_compile_flags

.PHONY: scaffold
scaffold: # create build directory
ifeq ($(BUILD_PLATFORM),windows)
	-@setlocal enableextensions enabledelayedexpansion && mkdir $(addprefix $(OBJ_DIR), $(DIRECTORIES)) 2>NUL || cd .
	-@setlocal enableextensions enabledelayedexpansion && mkdir $(BUILD_DIR) 2>NUL || cd .
else
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(addprefix $(OBJ_DIR)/,$(DIRECTORIES))
endif


.PHONY: link
link: scaffold $(OBJ_FILES) # link
	@echo Linking "$(ASSEMBLY)"...
ifeq ($(BUILD_PLATFORM),windows)
	@clang $(OBJ_FILES) -o $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION) $(LINKER_FLAGS)
else
	@clang $(OBJ_FILES) -o $(BUILD_DIR)/$(ASSEMBLY)$(EXTENSION) $(LINKER_FLAGS)
endif

.PHONY: compile
compile:
	@echo --- Performing "$(ASSEMBLY)" $(TARGET) build ---
-include $(OBJ_FILES:.o=.d)

.PHONY: clean
clean: # clean build directory
	@echo --- Cleaning "$(ASSEMBLY)" ---
ifeq ($(BUILD_PLATFORM),windows)
	@if exist $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION) del $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION)
# Windows builds include a lot of files... get them all.
	@if exist $(BUILD_DIR)\$(ASSEMBLY).* del $(BUILD_DIR)\$(ASSEMBLY).*
	@if exist $(OBJ_DIR)\$(ASSEMBLY) rmdir /s /q $(OBJ_DIR)\$(ASSEMBLY)
else
	@rm -rf $(BUILD_DIR)/$(ASSEMBLY)$(EXTENSION)
	@rm -rf $(OBJ_DIR)/$(ASSEMBLY)
endif

# compile .c to .o object for windows, linux and mac
$(OBJ_DIR)/%.c.o: %.c 
	@echo   $<...
	@clang $< $(COMPILER_FLAGS) -c -o $@ $(DEFINES) $(INCLUDE_FLAGS)

-include $(OBJ_FILES:.o=.d)

.PHONY: gen_compile_flags
gen_compile_flags:
ifeq ($(BUILD_PLATFORM),windows)
	$(shell powershell \"$(INCLUDE_FLAGS) $(DEFINES)\".replace('-I', '-I..\').replace(' ', \"`n\").replace('-I..\C:', '-IC:') > $(ASSEMBLY)/compile_flags.txt)
else
	@echo $(INCLUDE_FLAGS) $(DEFINES) | tr " " "\n" | sed "s/\-I\.\//\-I\.\.\//g" > $(ASSEMBLY)/compile_flags.txt
endif
