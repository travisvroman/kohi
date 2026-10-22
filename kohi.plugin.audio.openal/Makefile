# Kohi.Plugin.Audio.OpenAL makefile

# Must be defined before including base makefile
ASSEMBLY_NAME=kohi.plugin.audio.openal
ASSEMBLY_IS_SHARED=true
# Additional cross-platform defines/cflags/includes/ldflags
ADDL_DEFINES=
ADDL_CFLAGS=
ADDL_INCLUDES= -I../kohi.core/src -I../kohi.runtime/src
ADDL_LDFLAGS= -L../kohi.core/bin -L../kohi.runtime/bin -lkohi.core -lkohi.runtime

# Additional platform-specific defines/cflags/includes/ldflags
# Leave these blank if not using them.
# Win32
ADDL_WIN32_DEFINES=
ADDL_WIN32_CFLAGS=
ADDL_WIN32_INCLUDES= -I'C:\Program Files (x86)\OpenAL 1.1 SDK\include'
ADDL_WIN32_LDFLAGS= -L"C:\Program Files (x86)\OpenAL 1.1 SDK\libs\Win64" -lopenal32
# Linux
ADDL_LINUX_DEFINES=
ADDL_LINUX_CFLAGS=
ADDL_LINUX_INCLUDES=
ADDL_LINUX_LDFLAGS= -lopenal
# Darwin/macOS
ADDL_DARWIN_DEFINES=
ADDL_DARWIN_CFLAGS=
ADDL_DARWIN_INCLUDES=
ADDL_DARWIN_LDFLAGS= -framework OpenAL 


# Include the base makefile, which actually does all the work.
-include ../Makefile.prefix
