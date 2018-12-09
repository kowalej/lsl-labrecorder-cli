#include <Windows.h>
#include <iostream>

class Process {
public:
	static PROCESS_INFORMATION launch_process(std::string app, std::string args);
	static bool check_if_process_active(PROCESS_INFORMATION &pi);
	static bool stop_process(PROCESS_INFORMATION &pi);
};