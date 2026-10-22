# Kohi.Core makefile

# Must be defined before including base makefile
ASSEMBLY_NAME=kohi.core
ASSEMBLY_IS_SHARED=true
# Additional cross-platform defines/cflags/includes/ldflags
ADDL_DEFINES=
ADDL_CFLAGS=
ADDL_INCLUDES=
ADDL_LDFLAGS=

# Additional platform-specific defines/cflags/includes/ldflags
# Leave these blank if not using them.
# Win32
ADDL_WIN32_DEFINES=
ADDL_WIN32_CFLAGS=
ADDL_WIN32_INCLUDES=
ADDL_WIN32_LDFLAGS= -luser32 -lgdi32 -lwinmm
# Linux
ADDL_LINUX_DEFINES=
ADDL_LINUX_CFLAGS=
ADDL_LINUX_INCLUDES= $(shell pkg-config --cflags dbus-1)
ADDL_LINUX_LDFLAGS= -lxcb -lX11 -lX11-xcb -lxkbcommon -lxcb-xkb -lm -ldl -L/usr/X11R6/lib
# Darwin/macOS
ADDL_DARWIN_DEFINES=
ADDL_DARWIN_CFLAGS=
ADDL_DARWIN_INCLUDES=
ADDL_DARWIN_LDFLAGS= -lobjc -framework AppKit -framework QuartzCore -framework DiskArbitration -framework CoreFoundation -framework UniformTypeIdentifiers


# Include the base makefile, which actually does all the work.
-include ../Makefile.prefix
