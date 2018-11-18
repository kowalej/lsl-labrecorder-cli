#include "process.h"

PROCESS_INFORMATION Process::launch_process(std::string app, std::string args) {
	// Prepare handles.
	STARTUPINFO si;
	PROCESS_INFORMATION pi; // The function returns this
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	LPSTR argsLP = const_cast<char *>(args.c_str());

	// Start the child process.
	if (!CreateProcess(app.c_str(), // app path
			"",	// Command line (needs to include app path as first argument. args
				   // seperated by whitepace)
			NULL,  // Process handle not inheritable
			NULL,  // Thread handle not inheritable
			FALSE, // Set handle inheritance to FALSE
			CREATE_NEW_CONSOLE,	 // Flags
			NULL,  // Use parent's environment block
			NULL,  // Use parent's starting directory
			&si,   // Pointer to STARTUPINFO structure
			&pi)   // Pointer to PROCESS_INFORMATION structure
	) {
		printf("CreateProcess failed (%d).\n", GetLastError());
		throw std::exception("Could not create child process");
	} else {
		std::cout << "Successfully launched " << app.c_str() << "." << std::endl;
	}

	// Return process handle
	return pi;
}

bool Process::check_if_process_active(PROCESS_INFORMATION &pi) {
	// Check if handle is closed.
	if (pi.hProcess == NULL) {
		printf("Process handle is closed or invalid (%d).\n");
		return FALSE;
	}

	// If handle open, check if process is active.
	DWORD lpExitCode = 0;
	if (GetExitCodeProcess(pi.hProcess, &lpExitCode) == 0) {
		printf("Cannot return exit code (%d).\n", GetLastError());
		throw std::exception("Cannot return exit code");
	} else {
		if (lpExitCode == STILL_ACTIVE) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
}

bool Process::stop_process(PROCESS_INFORMATION &pi) {
	// Check if handle is invalid or has allready been closed.
	if (pi.hProcess == NULL) {
		printf("Process handle invalid. Possibly allready been closed (%d).\n");
		return 0;
	}

	// Terminate Process
	if (!TerminateProcess(pi.hProcess, 1)) {
		printf("ExitProcess failed (%d).\n", GetLastError());
		return 0;
	}

	// Wait until child process exits.
	if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED) {
		printf("Wait for exit process failed(%d).\n", GetLastError());
		return 0;
	}

	// Close process and thread handles.
	if (!CloseHandle(pi.hProcess)) {
		printf("Cannot close process handle(%d).\n", GetLastError());
		return 0;
	} else {
		pi.hProcess = NULL;
	}

	if (!CloseHandle(pi.hThread)) {
		printf("Cannot close thread handle (%d).\n", GetLastError());
		return 0;
	} else {
		pi.hProcess = NULL;
	}
	return 1;
}
