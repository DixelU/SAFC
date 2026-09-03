#pragma once
#ifndef SAFC_PLAYER_VIDEO_RENDER_UI
#define SAFC_PLAYER_VIDEO_RENDER_UI

void load_player_video_render_settings();
void initialize_player_video_render_window(
	unsigned background_color, unsigned header_color, unsigned border_color);
void open_player_video_render_settings();
void shutdown_player_video_render();

#endif
