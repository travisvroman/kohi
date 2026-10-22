# Kohi.Plugin.Utils makefile

# Must be defined before including base makefile
ASSEMBLY_NAME=kohi.plugin.utils
ASSEMBLY_IS_SHARED=true
# Additional cross-platform defines/cflags/includes/ldflags
ADDL_DEFINES=
ADDL_CFLAGS=
ADDL_INCLUDES= -I../kohi.core/src -I../kohi.runtime/src -I../kohi.plugin.ui.kui/src
ADDL_LDFLAGS= -L../kohi.core/bin -L../kohi.runtime/bin -L../kohi.plugin.ui.kui/bin -lkohi.core -lkohi.runtime -lkohi.plugin.ui.kui

# Additional platform-specific defines/cflags/includes/ldflags
# Leave these blank if not using them.
# Win32
ADDL_WIN32_DEFINES=
ADDL_WIN32_CFLAGS=
ADDL_WIN32_INCLUDES= -I'$(ASSIMP)\include'
ADDL_WIN32_LDFLAGS= -L'$(ASSIMP)\lib\x64\' -lassimp-vc143-mt
# Linux
ADDL_LINUX_DEFINES=
ADDL_LINUX_CFLAGS=
ADDL_LINUX_INCLUDES= -I/usr/include/assimp/
ADDL_LINUX_LDFLAGS= -lm -lassimp -L/usr/lib/ -L/usr/lib/x86_64-linux-gnu/
# Darwin/macOS
ADDL_DARWIN_DEFINES=
ADDL_DARWIN_CFLAGS=
ADDL_DARWIN_INCLUDES= -I/opt/homebrew/opt/assimp/include
ADDL_DARWIN_LDFLAGS= -L/opt/homebrew/opt/assimp/lib -lassimp


# Include the base makefile, which actually does all the work.
-include ../Makefile.prefix
