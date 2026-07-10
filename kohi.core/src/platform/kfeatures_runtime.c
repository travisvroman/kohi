#include "kfeatures_runtime.h"
#include "defines.h"
#include "platform/platform.h"

#if defined(__x86_64__) || defined(_M_X64)

#	if defined(_MSC_VER)
#		include <intrin.h>
#	else
#		include <cpuid.h>
#	endif

void detect_x86_features (kcpu_feature_flag_bits *flags) {
	u32 eax, ebx, ecx, edx;

#	if defined(_MSC_VER)
	int regs[4];
	__cpuid(regs, 1);
	ecx = regs[2];
	edx = regs[3];
#	else
	__cpuid(1, eax, ebx, ecx, edx);
#	endif

	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSE_BIT, (edx >> 25) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSE2_BIT, (edx >> 26) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSE3_BIT, (ecx >> 0) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSSE3_BIT, (ecx >> 9) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSE41_BIT, (ecx >> 19) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_SSE42_BIT, (ecx >> 20) & 1);
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_AVX_BIT, (ecx >> 28) & 1);

#	if defined(_MSC_VER)
	__cpuid(regs, 7);
	ebx = regs[1];
	eax = ebx;
	if (eax) {
		// Intentional no-op
	}
#	else
	__cpuid_count(7, 0, eax, ebx, ecx, edx);
#	endif

	FLAG_SET(*flags, KCPU_FEATURE_FLAG_AVX2_BIT, (ebx >> 5) & 1);
}
#endif

#if defined(__linux__) && defined(__aarch64__)
#	include <sys/auxv.h>

void detect_arm_featurees (kcpu_feature_flags *flags) {
	FLAG_SET(*flags, KCPU_FEATURE_FLAG_NEON_BIT, true); // Mandatory on AArch64
}
#else
void detect_arm_features (kcpu_feature_flag_bits *flags) {
}
#endif
