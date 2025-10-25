#include <iostream>

#include "ui_entry.hpp"

#include "core/background_worker.h"

#ifdef DEV_TEST
// Include test headers
#else
// If not fall back to the actual software lol
#endif

int main(int argc, char* argv[])
{
	std::cout << "Hellooo From SAFC V2 :D\n";

	worker_singleton<struct file_meta_scanner>::instance(); // Initialize background worker
#ifdef DEV_TEST
	// Test your stuff here
#else
	ui_entry();
#endif
	return 0;
}