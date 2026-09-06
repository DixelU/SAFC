#pragma once

#include <string>

struct single_midi_info_collector;

namespace props_and_sets
{
extern std::string* PPQN;
extern std::string* OFFSET;
extern std::string* TEMPO;
extern int current_id, cat_id, vm_id, pm_id;
extern bool human_readible;
extern single_midi_info_collector* smic_ptr;
extern std::string csv_delim;

void open_file_properties(int id);
void initialize_collecting();
void on_apply_settings();
void on_apply_bs2a();
void on_pitch_map();

namespace SMIC
{
void load_time_map();
void enable_pg();
void enable_tg();
void switch_personal_use();
void export_tg();
void export_all();
void differentiate_ticks();
void integrate_time();
}

namespace cut_and_transpose
{
void on_cat();
void on_reset();
void on_cdt128();
void on_0_127_to_128_255();
void on_copy();
void on_paste();
void on_delete();
}

namespace volume_map
{
void on_vol_map();
void on_degree_shape();
void on_simplify();
void on_trace();
void on_set_mode_change();
void on_erase();
void on_delete();
}
}