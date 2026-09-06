#include "app_state.h"
#include "runtime.h"
#include "ui.h"
#include "settings.h"
#include "file_properties.h"
#include "playback_source.h"
#include "editor.h"
#include "window_layout.h"

namespace
{
void on_timer(int v);
void gl_display()
{
	lfont_symbols_info::initialise_font(default_font_name);

	glClear(GL_COLOR_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	if (firstboot)
	{
		firstboot = 0;

		init(false);
		if (april_fool)
		{
			global_window_handler->throw_alert("Today is a special day! ( -w-)\nToday you'll have new background\n(-w- )", "1st of April!", special_signs::draw_wait, true, 0xFF00FFFF, 20);
			(*global_window_handler)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		if (years_old >= 0)
		{
			global_window_handler->throw_alert("Interesting fact: today is exactly " + std::to_string(years_old) + " years since first SAFC release.\n(o w o  )", "SAFC birthday", special_signs::draw_wait, 1, 0xFF7F3FFF, 50);
			(*global_window_handler)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		animation_is_active = !animation_is_active;
		on_timer(0);
	}

	if (years_old >= 0 || settings::background_id == 100)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 1, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glColor4f(1, 1, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(1, 0.9f, 0.8f, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0.8f, 0.9f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (april_fool || settings::background_id == 69)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glColor4f(0, 1, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(1, 0.5f, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0, 0.5f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (month_beginning || settings::background_id == 42)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0.5f, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0, 0.5f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (settings::background_id < 4)
	{
		glBegin(GL_QUADS);
		glColor4f(0.05f, 0.05f, 0.10f, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0.05f, 0.1f, 0.25f, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else
	{
		glBegin(GL_QUADS);
		glColor4f(0.00f, 0.2f, 0.4f, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}

	glRotatef(dumb_rotation_angle, 0, 0, 1);

	if (global_window_handler)
		global_window_handler->draw();
	if (drag_over)
		special_signs::draw_file_sign(0, 0, 50, 0xFFFFFFFF, 0);
	glRotatef(-dumb_rotation_angle, 0, 0, 1);

	glutSwapBuffers();
	++timer_v;
}

void gl_init()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D((0 - internal_range) * (wind_x / window_base_width), internal_range * (wind_x / window_base_width), (0 - internal_range) * (wind_y / window_base_height), internal_range * (wind_y / window_base_height));
}

void gl_close()
{
	if (application_shutting_down.exchange(true, std::memory_order_acq_rel))
		return;

	settings::regestry_access.Close();
	compressed_player_cancel.store(true, std::memory_order_release);
	if (props_and_sets::smic_ptr)
		props_and_sets::smic_ptr->request_stop();

	// These may be inside network I/O or editor file I/O. Give
	// them cancellation before any UI object they can reference is released.
	worker_singleton<struct version_check>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct midi_file_list>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct editor_load>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct editor_save>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);

	// Retire UI polling before tearing down the state it reads.
	worker_singleton<struct syncore_status_watcher>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct player_watcher>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct compressed_player_watcher>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct info_collection_watcher>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct merge_ri_stage>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct merge_ri_stage_cleanup>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct merge_global_cleanup>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);

	shutdown_player_video_render();

	if (player)
		player->shutdown();

	// Playback workers are joined after the player has received its terminal
	// stop, so an active paused run cannot keep process teardown alive.
	worker_singleton<struct player_thread>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct editor_playback>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct midi_out_selct>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct info_collection>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);
	worker_singleton<struct merge>::instance().shutdown(
		dixelu::background_worker_shutdown::cancel);

	lfont_symbols_info::destroy_font();
	if (hWnd && hDc)
		ReleaseDC(hWnd, hDc);
	hDc = nullptr;
	hWnd = nullptr;
}

void on_timer(int v)
{
	glutTimerFunc(15, on_timer, 0);
	update_editor_playback_status();
	if (animation_is_active)
	{
		gl_display();
		++timer_v;
	}
}

void on_resize(int x, int y)
{
	wind_x = x;
	wind_y = y;
	gl_init();
	glViewport(0, 0, x, y);
	if (global_window_handler)
	{
		auto SMRP = (*global_window_handler)["SMRP_CONTAINER"];
		SMRP->safe_change_position_argumented(0, 0, 0);
		SMRP->not_safe_resize_centered(internal_range * 3 * (wind_y / window_base_height) + 2 * moveable_window::window_header_size, internal_range * 3 * (wind_x / window_base_width));

		if (simplayer_maximised)
			apply_simplayer_maximised_layout();

		if (midieditor_maximised)
			apply_midieditor_maximised_layout();
	}
}
} // namespace

void rotate_view(float& x, float& y)
{
	float t = x * cos(rotation_angle()) + y * sin(rotation_angle());
	y = 0.f - x * sin(rotation_angle()) + y * cos(rotation_angle());
	x = t;
}

void absolute_to_actual_coords(int ix, int iy, float& x, float& y)
{
	float wx = wind_x, wy = wind_y;
	x = ((float)(ix - wx * 0.5f)) / (0.5f * (wx / (internal_range * (wind_x / window_base_width))));
	y = ((float)(0 - iy + wy * 0.5f)) / (0.5f * (wy / (internal_range * (wind_y / window_base_height))));
	rotate_view(x, y);
}

namespace
{
void gl_motion(int ix, int iy)
{
	float fx, fy;
	absolute_to_actual_coords(ix, iy, fx, fy);
	mouse_x_position = fx;
	mouse_y_position = fy;
	if (global_window_handler)
		global_window_handler->mouse_handler(fx, fy, 0, 0);
}

void gl_key(std::uint8_t k, int x, int y)
{
	if (global_window_handler)
		global_window_handler->keyboard_handler(k);

	if (k == 27)
	{
		gl_close();
		exit(0);
	}
}

void gl_click(int butt, int state, int x, int y)
{
	float fx, fy;
	char button;
	absolute_to_actual_coords(x, y, fx, fy);
	button = butt - 1;

	if (state == GLUT_DOWN)
		state = -1;
	else if (state == GLUT_UP)
		state = 1;

	if (global_window_handler)
		global_window_handler->mouse_handler(fx, fy, button, static_cast<char>(state));
}

void gl_drag(int x, int y)
{
	//gl_click(88, MOUSE_DRAG_EVENT, x, y);
	gl_motion(x, y);
}

void gl_special_key(int Key, int x, int y)
{
	auto modif = glutGetModifiers();
	// Modified arrows use dedicated non-ASCII values because the legacy plain
	// arrow codes 1-4 collide with Ctrl+A..Ctrl+D.
	if ((modif & GLUT_ACTIVE_CTRL) && !(modif & GLUT_ACTIVE_ALT))
	{
		if (Key == GLUT_KEY_DOWN && global_window_handler)
			global_window_handler->keyboard_handler(keyboard_key::ctrl_arrow_down);
		else if (Key == GLUT_KEY_UP && global_window_handler)
			global_window_handler->keyboard_handler(keyboard_key::ctrl_arrow_up);
	}
	else if (!(modif & GLUT_ACTIVE_ALT))
	{
		switch (Key)
		{
			case GLUT_KEY_DOWN:		if (global_window_handler) global_window_handler->keyboard_handler(1);
				break;
			case GLUT_KEY_UP:		if (global_window_handler) global_window_handler->keyboard_handler(2);
				break;
			case GLUT_KEY_LEFT:		if (global_window_handler) global_window_handler->keyboard_handler(3);
				break;
			case GLUT_KEY_RIGHT:		if (global_window_handler) global_window_handler->keyboard_handler(4);
				break;
			case GLUT_KEY_HOME:		if (global_window_handler) global_window_handler->keyboard_handler(5);
				break;
			case GLUT_KEY_END:		if (global_window_handler) global_window_handler->keyboard_handler(6);
				break;

			case GLUT_KEY_F5:		if (global_window_handler) init();
				break;
		}
	}
	if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_DOWN)
	{
		internal_range *= 1.1f;
		on_resize(wind_x, wind_y);

		init();
	}
	else if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_UP)
	{
		internal_range /= 1.1f;
		on_resize(wind_x, wind_y);

		init();
	}
}

void gl_exit(int a)
{
}

}

void run_gui(int argc, char** argv)
{
	restore_reg_settings();
	initialise_system_text_styles(is_fonted);

	_wremove(L"_s");
	_wremove(L"_f");
	_wremove(L"_g");

#ifdef _DEBUG
	ShowWindow(GetConsoleWindow(), SW_SHOW);
#else // _DEBUG
	ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
	// ShowWindow(GetConsoleWindow(), SW_SHOW);

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	//srand(1);
	//srand(clock());
	//cout << to_string((std::uint16_t)0) << endl;

	srand(collect_time_data());
	__glutInitWithExit(&argc, argv, gl_exit);
	//cout << argv[0] << endl;
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_ALPHA | GLUT_MULTISAMPLE);
	glutInitWindowSize(window_base_width, window_base_height);
	//glutInitWindowPosition(50, 0);
	glutCreateWindow(window_title);

	hWnd = FindWindowA(NULL, window_title);
	hDc = GetDC(hWnd);
	lfont_symbols_info::initialise_font(default_font_name, true);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);//_MINUS_SRC_ALPHA
	glEnable(GL_BLEND);
	//glutSetOption(GLUT_MULTISAMPLE, 4);

	//auto vendor = glGetString(GL_VENDOR);
	//auto renderer = glGetString(GL_RENDERER);
	//auto version = glGetString(GL_VERSION);
	//printf("%s - %s.\nVersion %s\n", vendor, renderer, version);

	//glEnable(GL_POLYGON_SMOOTH);//laggy af
	//glEnable(GL_LINE_SMOOTH);//GL_POLYGON_SMOOTH
	//glEnable(GL_POINT_SMOOTH);

	//glShadeModel(GL_SMOOTH);
	//glEnable(GLUT_MULTISAMPLE);
	//glutSetOption(GLUT_MULTISAMPLE, 8);

	glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);//GL_FASTEST//GL_NICEST
	glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);

	glutCloseFunc(gl_close);
	glutMouseFunc(gl_click);
	glutReshapeFunc(on_resize);
	glutSpecialFunc(gl_special_key);
	glutMotionFunc(gl_drag);
	glutPassiveMotionFunc(gl_motion);
	glutKeyboardFunc(gl_key);
	glutDisplayFunc(gl_display);
	gl_init();
	glutMainLoop();
}
