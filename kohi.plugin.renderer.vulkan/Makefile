# Kohi.Plugin.Renderer.Vulkan makefile
SHELL := /bin/bash

# Must be defined before including base makefile
ASSEMBLY_NAME=kohi.plugin.renderer.vulkan
ASSEMBLY_IS_SHARED=true
# Additional cross-platform defines/cflags/includes/ldflags
ADDL_DEFINES=
ADDL_CFLAGS=
ADDL_INCLUDES= -I../kohi.core/src -I../kohi.runtime/src -I$(VULKAN_SDK)/include
ADDL_LDFLAGS= -L../kohi.core/bin -L../kohi.runtime/bin -lkohi.core -lkohi.runtime -lshaderc_shared -L$(VULKAN_SDK)/lib

# Additional platform-specific defines/cflags/includes/ldflags
# Leave these blank if not using them.
# Win32
ADDL_WIN32_DEFINES=
ADDL_WIN32_CFLAGS=
ADDL_WIN32_INCLUDES=
ADDL_WIN32_LDFLAGS= -L$(VULKAN_SDK)\Lib
# Linux
ADDL_LINUX_DEFINES=
ADDL_LINUX_CFLAGS=
ADDL_LINUX_INCLUDES=
ADDL_LINUX_LDFLAGS=
# Darwin/macOS
ADDL_DARWIN_DEFINES=
ADDL_DARWIN_CFLAGS=
ADDL_DARWIN_INCLUDES=
ADDL_DARWIN_LDFLAGS= -Wl,-rpath,/usr/local/lib


# Include the base makefile, which actually does all the work.
-include ../Makefile.prefix
