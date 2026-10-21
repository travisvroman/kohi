#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

static double filetime_to_seconds (FILETIME ft) {
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	// FILETIME is in 100-nanosecond units
	return (double)u.QuadPart * 1e-7;
}

int main (int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: time <command> [args...]\n");
		return 1;
	}

	// Build command line
	char cmdline[32768] = {0};
	for (int i = 1; i < argc; ++i) {
		if (i > 1)
			strcat(cmdline, " ");
		strcat(cmdline, argv[i]);
	}

	STARTUPINFOA si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);

	LARGE_INTEGER freq, start, end;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	if (!CreateProcessA(
			NULL,
			cmdline,
			NULL, NULL,
			FALSE,
			0,
			NULL, NULL,
			&si,
			&pi)) {
		fprintf(stderr, "failed to start process (%lu)\n", GetLastError());
		return 1;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	QueryPerformanceCounter(&end);

	FILETIME create, exit, kernel, user;
	GetProcessTimes(pi.hProcess, &create, &exit, &kernel, &user);

	double real = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
	double user_s = filetime_to_seconds(user);
	double sys_s = filetime_to_seconds(kernel);

	DWORD exit_code = 0;
	GetExitCodeProcess(pi.hProcess, &exit_code);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	fprintf(stderr, "\n");
	fprintf(stderr, "real %.3fs\n", real);
	fprintf(stderr, "user %.3fs\n", user_s);
	fprintf(stderr, "sys  %.3fs\n", sys_s);

	return (int)exit_code;
}
