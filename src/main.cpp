#include <iostream>

#include "ui_entry.hpp"

#ifdef DEV_TEST
// Include test headers
#else
// If not fall back to the actual software lol
#endif

int main(int argc, char* argv[])
{
	std::cout << "Hellooo From SAFC V2 :D\n";
#ifdef DEV_TEST
	// Test your stuff here
#else
	ui_entry();
#endif
	return 0;
}