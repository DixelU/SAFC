#include "app/app_state.h"
#include "app/runtime.h"
#include "app/update.h"

int main(int argc, char** argv)
{
	player = std::make_shared<simple_player>();
	player->init();

	editor = std::make_shared<midi_editor>();

	g_version_tuple = get_executable_version();

	std::ios_base::sync_with_stdio(false); //why not

	if (argc > 1)
		run_cli(argc, argv);
	else
		run_gui(argc, argv);

	return 0;
}
