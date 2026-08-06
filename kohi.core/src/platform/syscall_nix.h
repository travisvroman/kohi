#ifndef KOHI_INCLUDE_SYSCALL_NIX_H
#define KOHI_INCLUDE_SYSCALL_NIX_H

#include "platform.h"

#if defined(KPLATFORM_LINUX) || defined(KPLATFORM_APPLE)
// Linux syscall codes
#	if defined(KPLATFORM_LINUX)
#		define PROT_READ 0x1
#		define PROT_WRITE 0x2

#		define MAP_PRIVATE 0x02
#		define MAP_ANONYMOUS 0x20

#		define SYS_write 1

#		define SYS_mmap 9
#		define SYS_munmap 11

#		define FD_STDIN 0
#		define FD_STDOUT 1
#		define FD_STDERR 2
#	endif

KAPI i64 syscall2 (i64 number, i64 arg0, i64 arg1);
KAPI i64 syscall3 (i64 number, i64 arg0, i64 arg1, i64 arg2);
KAPI i64 syscall4 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3);
KAPI i64 syscall5 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4);
KAPI i64 syscall6 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4, i64 arg5);

#endif
#endif
