DIR := $(subst /,\,${CURDIR})
BUILD_DIR := bin
OBJ_DIR := obj

ASSEMBLY := engine
EXTENSION := .dll
COMPILER_FLAGS := -g -MD -fdeclspec #-fPIC
INCLUDE_FLAGS := -Iengine\src -I$(VULKAN_SDK)\include
LINKER_FLAGS := -g -shared -luser32 -lvulkan-1 -L$(VULKAN_SDK)\Lib -L$(OBJ_DIR)\engine
DEFINES := -D_DEBUG -DKEXPORT -D_CRT_SECURE_NO_WARNINGS

# Make does not offer a recursive wildcard function, so here's one:
rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

SRC_FILES := $(call rwildcard,$(ASSEMBLY)/,*.c) # Get all .c files
DIRECTORIES := \$(ASSEMBLY)\src $(subst $(DIR),,$(shell dir $(ASSEMBLY)\src /S /AD /B | findstr /i src)) # Get all directories under src.
OBJ_FILES := $(SRC_FILES:%=$(OBJ_DIR)/%.o) # Get all compiled .c.o objects for engine

all: scaffold compile link

.PHONY: scaffold
scaffold: # create build directory
	@echo Scaffolding folder structure...
	-@setlocal enableextensions enabledelayedexpansion && mkdir $(addprefix $(OBJ_DIR), $(DIRECTORIES)) 2>NUL || cd .
	-@setlocal enableextensions enabledelayedexpansion && mkdir $(BUILD_DIR) 2>NUL || cd .
	@echo Done.

.PHONY: link
link: scaffold $(OBJ_FILES) # link
	@echo Linking $(ASSEMBLY)...
	@clang $(OBJ_FILES) -o $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION) $(LINKER_FLAGS)

.PHONY: compile
compile: #compile .c files
	@echo Compiling...

.PHONY: clean
clean: # clean build directory
	if exist $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION) del $(BUILD_DIR)\$(ASSEMBLY)$(EXTENSION)
	rmdir /s /q $(OBJ_DIR)\$(ASSEMBLY)

$(OBJ_DIR)/%.c.o: %.c # compile .c to .c.o object
	@echo   $<...
	@clang $< $(COMPILER_FLAGS) -c -o $@ $(DEFINES) $(INCLUDE_FLAGS)

-include $(OBJ_FILES:.o=.d)
