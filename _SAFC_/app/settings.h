#pragma once

#include <cstdint>
#include "../WinRegWrappers.h"

namespace settings
{
extern std::int32_t background_id;
extern WinReg::RegKey regestry_access;
void on_settings();
void on_set_apply();
void change_is_fonted_var();
void apply_to_all();
void apply_fs_wheel(double new_val);
void apply_rel_wheel(double new_val);
void feedback_open();
}

void restore_reg_settings();
void on_other_settings();