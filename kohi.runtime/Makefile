# Kohi.Runtime makefile

# Must be defined before including base makefile
ASSEMBLY_NAME=kohi.runtime
ASSEMBLY_IS_SHARED=true
# Additional cross-platform defines/cflags/includes/ldflags
ADDL_DEFINES=
ADDL_CFLAGS=
ADDL_INCLUDES= -I../kohi.core/src
ADDL_LDFLAGS= -L../kohi.core/bin -L. -lkohi.core

# Additional platform-specific defines/cflags/includes/ldflags
# Leave these blank if not using them.
# Win32
ADDL_WIN32_DEFINES=
ADDL_WIN32_CFLAGS=
ADDL_WIN32_INCLUDES=
ADDL_WIN32_LDFLAGS=
# Linux
ADDL_LINUX_DEFINES=
ADDL_LINUX_CFLAGS=
ADDL_LINUX_INCLUDES=
ADDL_LINUX_LDFLAGS= -lm
# Darwin/macOS
ADDL_DARWIN_DEFINES=
ADDL_DARWIN_CFLAGS=
ADDL_DARWIN_INCLUDES=
ADDL_DARWIN_LDFLAGS=


# Include the base makefile, which actually does all the work.
-include ../Makefile.prefix
