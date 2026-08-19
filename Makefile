# Top-level Kohi makefile

# Run make setup to setup the build environment. This needs to be done once, unless a repo update calls for it.
# Run make build-debug to build debug versions of kohi, its test libraries, its plugins, and the testbed project.
# Run make build-release to build debug versions of kohi, its test libraries, its plugins, and the testbed project.
# Run make all-debug to build everything in debug mode. This doesn't usually need to be done, build-debug is typically enough after the first build.
# Run make all-release to build everything in release mode. This doesn't usually need to be done, build-debug is typically enough after the first build.
# Run make clean To clean everything. This should especially be done if changing from debug->release or vice versa.
#
# To build just a portion of things, you can target certain things to be built:
# kohi-debug: builds just utils, Core and Runtime.
# kohi-tests-debug: Build core and runtime tests (with core and runtime if needed)
# kohi-plugins-debug: Rebuilds all plugins, plus Kohi core/runtime if needed. Tests are skipped.
# testbed-debug: Builds testbed, plus kohi and plugins if needed. Tests are skipped.
#
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

.PHONY: all-debug all-release clean scaffold scaffold-win32 scaffold-linux scaffold-macos kohi-tools-debug copy-top-level copy-top-level-win32 copy-top-level-linux copy-top-level-macos tools-clean clean-top-level clean-top-level-win32 clean-top-level-linux clean-top-level-macos

.PHONY: utils-debug core-debug core-tests-debug runtime-debug runtime-tests-debug plugin-audio-openal-debug plugin-renderer-vulkan-debug plugin-renderer-ui-kui-debug plugin-renderer-utils-debug tools-debug testbed-klib-debug testbed-kapp-debug
.PHONY: kohi-debug kohi-tests-debug kohi-plugins-debug testbed-debug

.PHONY: utils-release core-release core-tests-release runtime-release runtime-tests-release plugin-audio-openal-release plugin-renderer-vulkan-release plugin-renderer-ui-kui-release plugin-renderer-utils-release tools-release testbed-klib-release testbed-kapp-release
.PHONY: kohi-release kohi-tests-release kohi-plugins-release testbed-release

.PHONY: utils-clean core-clean core-tests-clean runtime-clean runtime-tests-clean plugin-audio-openal-clean plugin-renderer-vulkan-clean plugin-renderer-ui-kui-clean plugin-renderer-utils-clean tools-clean testbed-klib-clean testbed-kapp-clean
.PHONY: kohi-clean kohi-tests-clean kohi-plugins-clean testbed-clean

.PHONY: setup getdeps getdeps-linux getdeps-win32 getdeps-macos build-debug build-release

# LEFTOFF: For some reason getdeps always runs... should only run during 'setup'

# Setup: This really just needs to be done once to setup the environment, etc. Unless repo updates demand it.
setup: getdeps scaffold utils-debug copy-utils

getdeps: getdeps-$(PLATFORM)

getdeps-linux:
	./get_deps_linux.sh

getdeps-macos:
	./get_deps_macos.sh

getdeps-win32:
	get_deps_win32.bat

# Scaffold folder structure needed for top-level operations.
scaffold: scaffold-$(PLATFORM)
.NOTPARALLEL: scaffold

scaffold-win32:
	@if not exist bin mkdir bin
	@if not exist misc mkdir misc

scaffold-linux scaffold-macos:
	@mkdir -p bin
	@mkdir -p misc

copy-utils: copy-utils-$(PLATFORM)

copy-utils-win32:
	copy utils\bin\* misc

ifeq ($(filter Linux Darwin,$(UNAME_S)),$(UNAME_S))
ifeq ($(wildcard utils/bin/versiongen),)
copy-utils-linux copy-utils-macos: utils-debug
endif
endif

copy-utils-linux copy-utils-macos:
	cp utils/bin/* misc

copy-top-level: copy-top-level-$(PLATFORM)
.NOTPARALLEL: copy-top-level

copy-top-level-win32: copy-utils
	copy kohi.core\bin\* bin
	copy kohi.core.tests\bin\* bin
	copy kohi.runtime\bin\* bin
	copy kohi.runtime.tests\bin\* bin
	copy kohi.plugin.audio.openal\bin\* bin
	copy kohi.plugin.renderer.vulkan\bin\* bin
	copy kohi.plugin.ui.kui\bin\* bin
	copy kohi.plugin.utils\bin\* bin
	copy kohi.tools\bin\* bin
	copy testbed.klib\bin\* bin
	copy testbed.kapp\bin\* bin
	copy "$(ASSIMP)\bin\x64\*assimp*.dll" bin

copy-top-level-linux copy-top-level-macos: copy-utils
	cp kohi.core/bin/* bin
	cp kohi.core.tests/bin/* bin
	cp kohi.runtime/bin/* bin
	cp kohi.runtime.tests/bin/* bin
	cp kohi.plugin.audio.openal/bin/* bin
	cp kohi.plugin.renderer.vulkan/bin/* bin
	cp kohi.plugin.ui.kui/bin/* bin
	cp kohi.plugin.utils/bin/* bin
	cp kohi.tools/bin/* bin
	cp testbed.klib/bin/* bin
	cp testbed.kapp/bin/* bin
	cp ./vendor/vulkan/1.*/x86_64/lib/libshaderc_shared.so bin
	cp ./vendor/vulkan/1.*/x86_64/lib/libshaderc_shared.so.1 bin

copy-top-level-lib: copy-top-level-lib-$(PLATFORM)
.NOTPARALLEL: copy-top-level-lib

copy-top-level-lib-win32:
	copy testbed.klib\bin\* bin


copy-top-level-lib-linux copy-top-level-lib-macos:
	cp testbed.klib/bin/* bin

# Debug builds
all-debug: setup build-debug
build-debug: kohi-debug kohi-tests-debug kohi-plugins-debug kohi-tools-debug testbed-debug copy-top-level
kohi-debug: core-debug runtime-debug 
kohi-tests-debug: kohi-debug core-tests-debug runtime-tests-debug
kohi-plugins-debug: kohi-debug plugin-audio-openal-debug plugin-renderer-vulkan-debug plugin-ui-kui-debug plugin-utils-debug
testbed-debug: kohi-debug kohi-plugins-debug testbed-klib-debug testbed-kapp-debug
testbed-lib-debug: testbed-klib-debug copy-top-level-lib

utils-debug:
	$(MAKE) -C utils all-debug

core-debug:
	$(MAKE) -C kohi.core all-debug

core-tests-debug:
	$(MAKE) -C kohi.core.tests all-debug

runtime-debug:
	$(MAKE) -C kohi.runtime all-debug

runtime-tests-debug:
	$(MAKE) -C kohi.runtime.tests all-debug

plugin-audio-openal-debug:
	$(MAKE) -C kohi.plugin.audio.openal all-debug

plugin-renderer-vulkan-debug:
	$(MAKE) -C kohi.plugin.renderer.vulkan all-debug

plugin-ui-kui-debug:
	$(MAKE) -C kohi.plugin.ui.kui all-debug

plugin-utils-debug:
	$(MAKE) -C kohi.plugin.utils all-debug

kohi-tools-debug:
	$(MAKE) -C kohi.tools all-debug

testbed-klib-debug:
	$(MAKE) -C testbed.klib all-debug

testbed-kapp-debug:
	$(MAKE) -C testbed.kapp all-debug

# Release builds 
all-release: setup build-release
build-release: kohi-release kohi-tests-release kohi-plugins-release kohi-tools-release testbed-release copy-top-level
kohi-release: core-release runtime-release 
kohi-tests-release: kohi-release core-tests-release runtime-tests-release
kohi-plugins-release: kohi-release plugin-audio-openal-release plugin-renderer-vulkan-release plugin-renderer-ui-kui-release plugin-renderer-utils-release
testbed-release: kohi-release kohi-plugins-release testbed-klib-release testbed-kapp-release

utils-release:
	$(MAKE) -C utils all-release

core-release:
	$(MAKE) -C kohi.core all-release

core-tests-release:
	$(MAKE) -C kohi.core.tests all-release

runtime-release:
	$(MAKE) -C kohi.runtime all-release

runtime-tests-release:
	$(MAKE) -C kohi.runtime.tests all-release

plugin-audio-openal-release:
	$(MAKE) -C kohi.plugin.audio.openal all-release

plugin-renderer-vulkan-release:
	$(MAKE) -C kohi.plugin.renderer.vulkan all-release

plugin-renderer-ui-kui-release:
	$(MAKE) -C kohi.plugin.ui.kui all-release

plugin-renderer-utils-release:
	$(MAKE) -C kohi.plugin.utils all-release

kohi-tools-release:
	$(MAKE) -C kohi.tools all-release

testbed-klib-release:
	$(MAKE) -C testbed.klib all-release

testbed-kapp-release:
	$(MAKE) -C testbed.kapp all-release

# Clean "builds"
clean: utils-clean kohi-clean kohi-tests-clean kohi-plugins-clean tools-clean testbed-clean clean-top-level
kohi-clean: utils-clean core-clean runtime-clean clean-top-level
kohi-tests-clean: core-tests-clean runtime-tests-clean clean-top-level
kohi-plugins-clean: utils-clean kohi-clean plugin-audio-openal-clean plugin-renderer-vulkan-clean plugin-renderer-ui-kui-clean plugin-renderer-utils-clean clean-top-level
testbed-clean: utils-clean testbed-klib-clean testbed-kapp-clean clean-top-level

clean-top-level: clean-top-level-$(PLATFORM)
.NOTPARALLEL: clean-top-level

clean-top-level-win32:
	if exist bin del /s /q bin\*.*
	if exist obj del /s /q obj\*.*

clean-top-level-linux clean-top-level-macos:
	rm -rf bin/*
	rm -rf obj/*

utils-clean:
	$(MAKE) -C utils clean

core-clean:
	$(MAKE) -C kohi.core clean

core-tests-clean:
	$(MAKE) -C kohi.core.tests clean

runtime-clean:
	$(MAKE) -C kohi.runtime clean

runtime-tests-clean:
	$(MAKE) -C kohi.runtime.tests clean

plugin-audio-openal-clean:
	$(MAKE) -C kohi.plugin.audio.openal clean

plugin-renderer-vulkan-clean:
	$(MAKE) -C kohi.plugin.renderer.vulkan clean

plugin-renderer-ui-kui-clean:
	$(MAKE) -C kohi.plugin.ui.kui clean

plugin-renderer-utils-clean:
	$(MAKE) -C kohi.plugin.utils clean

tools-clean:
	$(MAKE) -C kohi.tools clean

testbed-klib-clean:
	$(MAKE) -C testbed.klib clean

testbed-kapp-clean:
	$(MAKE) -C testbed.kapp clean


