#pragma once

#include <string>
#include <vector>

void add_files(const std::vector<std::wstring>& filenames);
void on_add();
void on_rem();
void on_rem_all();
void on_submit_global_ppqn();
void on_global_ppqn();
void on_submit_global_offset();
void on_global_offset();
void on_submit_global_tempo();
void on_global_tempo();
void on_resolve();
void on_rem_vol_maps();
void on_rem_cats();
void on_rem_pitch_maps();
void on_rem_all_modules();