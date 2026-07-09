#pragma once
#ifndef SAFGUIF_L_MIDI_EDITOR_VIEWER
#define SAFGUIF_L_MIDI_EDITOR_VIEWER

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/midi_editor.h"
#include <GL/freeglut.h>
#include <functional>
#include <map>
#include <tuple>
#include <cmath>

/**
 * Piano roll viewer for midi_editor
 * Renders the piano roll visualization using OpenGL.
 *
 * Layout: time on X, pitch on Y (low keys at the bottom). A vertical keyboard
 * strip on the left shares the same per-key lanes as the note area. An optional
 * FL-style velocity lane sits under the roll (resizable by dragging its divider).
 *
 * Coloring: per-(track,channel) hue from simple_player's rotated palette,
 * normalized to full saturation/brightness so tracks stay distinguishable.
 * FL-style focus: the active track in full color, other tracks in gray;
 * right-clicking a gray note makes its track active.
 *
 * Mouse:
 *  - wheel over notes      zoom time around the cursor
 *  - wheel over keyboard   zoom pitch around the cursor
 *  - right-drag notes      pan time
 *  - right-drag keyboard   scroll pitch
 *  - right-click note      pick that note's track
 *  - left (select)         rubber-band selection; drag inside it to move notes
 *  - left (draw)           drag out a new note in the active track
 *  - left (erase)          delete active-track notes under the cursor
 *  - velocity lane         left-drag paints velocities, right-drag draws a linear ramp;
 *                          applies to the selection when one is active
 */
struct midi_editor_viewer : public handleable_ui_part
{
        using tick_type = midi_editor::tick_type;
        using sgtick_type = midi_editor::sgtick_type;
        using piano_note = midi_editor::piano_note;
        using velocity_entry = midi_editor::recorded_velocity_op::entry;

        enum class tool_mode : std::uint8_t { select, draw, erase };

        float xpos, ypos;
        midi_editor* editor;

        struct view_data
        {
                float width = 400.f;
                float height = 250.f;
        } data;

        static constexpr float keyboard_width = 22.f;
        static constexpr float divider_height = 5.f;
        static constexpr float min_lane_height = 24.f;

        tool_mode current_tool = tool_mode::select;

        // Velocity lane under the roll
        bool velocity_lane_visible = true;
        float velocity_lane_height = 55.f;

        // Called after a right-click switches the active track
        std::function<void()> on_track_changed;
        // Note audition: (key, velocity, channel, on/off)
        std::function<void(std::uint8_t, std::uint8_t, std::uint8_t, bool)> note_audition;

        // Roll interaction state
        bool selecting = false;
        bool moving = false;
        bool drawing = false;
        bool erasing = false;
        bool right_down = false;
        bool panning = false;
        bool kb_scrolling = false;
        tick_type anchor_tick = 0;
        std::uint8_t anchor_key = 0;
        sgtick_type move_delta_ticks = 0;
        int move_delta_keys = 0;
        tick_type draw_start_tick = 0;
        tick_type draw_end_tick = 0;
        std::uint8_t draw_key = 60;
        float right_down_mx = 0.f, right_down_my = 0.f;
        float pan_last_mx = 0.f;
        float pan_accum_ticks = 0.f;   // fractional pan remainder, matters at deep zoom
        float kb_last_my = 0.f;
        float kb_accum_keys = 0.f;

        // Velocity lane interaction state
        bool lane_painting = false;
        bool lane_line = false;
        bool divider_dragging = false;
        tick_type lane_anchor_tick = 0;
        std::uint8_t lane_anchor_vel = 100;
        tick_type lane_last_tick = 0;
        float lane_cursor_x = 0.f, lane_cursor_y = 0.f; // line tool guide endpoint
        // Accumulated gesture: identity -> (old, new); committed as one undo entry
        std::map<std::tuple<tick_type, tick_type, std::uint8_t, std::uint8_t, std::uint8_t>, velocity_entry> gesture;

        // Audition state
        int audition_key = -1;
        std::uint8_t audition_channel = 0;

        midi_editor_viewer(float xpos, float ypos, midi_editor* ed = nullptr) :
                xpos(xpos),
                ypos(ypos),
                editor(ed)
        {
        }

        ~midi_editor_viewer() override = default;

        void set_editor(midi_editor* ed)
        {
                std::lock_guard<std::recursive_mutex> locker(lock);
                editor = ed;
                cancel_interactions();
        }

        void set_tool(tool_mode mode)
        {
                std::lock_guard<std::recursive_mutex> locker(lock);
                current_tool = mode;
                cancel_interactions();
        }

        tool_mode get_tool() const { return current_tool; }

        void toggle_velocity_lane()
        {
                std::lock_guard<std::recursive_mutex> locker(lock);
                velocity_lane_visible = !velocity_lane_visible;
                cancel_interactions();
        }

        void cancel_interactions()
        {
                selecting = moving = drawing = erasing = false;
                right_down = panning = kb_scrolling = false;
                lane_painting = lane_line = divider_dragging = false;
                move_delta_ticks = 0;
                move_delta_keys = 0;
                pan_accum_ticks = 0.f;
                kb_accum_keys = 0.f;
                gesture.clear();
                audition_stop();
        }

        // ========================================================================
        // Geometry
        // ========================================================================

        struct layout
        {
                float left, right, bottom, top;
                float lane_bottom, lane_top;   // velocity lane band (== bottom when hidden)
                float roll_bottom;             // piano roll starts above lane + divider
                float notes_x, notes_w;
                float roll_h;
        };

        layout compute_layout() const
        {
                layout l;
                l.left = xpos - data.width * 0.5f;
                l.right = xpos + data.width * 0.5f;
                l.bottom = ypos - data.height * 0.5f;
                l.top = ypos + data.height * 0.5f;

                const float lane_h = velocity_lane_visible
                        ? std::clamp(velocity_lane_height, min_lane_height, data.height * 0.6f)
                        : 0.f;
                l.lane_bottom = l.bottom;
                l.lane_top = l.bottom + lane_h;
                l.roll_bottom = velocity_lane_visible ? l.lane_top + divider_height : l.bottom;

                l.notes_x = l.left + keyboard_width;
                l.notes_w = data.width - keyboard_width;
                l.roll_h = l.top - l.roll_bottom;
                return l;
        }

        // ========================================================================
        // Colors
        // ========================================================================

        static std::uint32_t rotate31(std::uint32_t color, std::uint32_t shift)
        {
                constexpr int base = 31;
                auto rem = shift % base;
                return color << (base - rem) | color >> rem;
        }

        static void hsv_to_rgb(float h, float s, float v,
                std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
        {
                h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f) / 60.f;
                const int i = int(h);
                const float f = h - i;
                const float p = v * (1.f - s);
                const float q = v * (1.f - s * f);
                const float t = v * (1.f - s * (1.f - f));
                float rf, gf, bf;
                switch (i % 6)
                {
                        case 0: rf = v; gf = t; bf = p; break;
                        case 1: rf = q; gf = v; bf = p; break;
                        case 2: rf = p; gf = v; bf = t; break;
                        case 3: rf = p; gf = q; bf = v; break;
                        case 4: rf = t; gf = p; bf = v; break;
                        default: rf = v; gf = p; bf = q; break;
                }
                r = std::uint8_t(rf * 255.f);
                g = std::uint8_t(gf * 255.f);
                b = std::uint8_t(bf * 255.f);
        }

        /**
         * Hue per (track, channel): derived from simple_player's rotated palette,
         * but rendered at full saturation/brightness so no track looks muddy.
         */
        static float track_hue(std::uint8_t track, std::uint8_t channel)
        {
                const auto id = (std::uint32_t(track) << 4) | (channel & 0x0F);
                const auto c = rotate31(0xFF7F008F, id);
                const float r = float((c >> 24) & 0xFF), g = float((c >> 16) & 0xFF), b = float((c >> 8) & 0xFF);
                const float mx = std::max({r, g, b}), mn = std::min({r, g, b});

                if (mx - mn < 24.f) // near-gray rotation: spread by golden angle instead
                        return std::fmod(id * 137.508f, 360.f);

                float h;
                if (mx == r)      h = 60.f * std::fmod((g - b) / (mx - mn), 6.f);
                else if (mx == g) h = 60.f * ((b - r) / (mx - mn) + 2.f);
                else              h = 60.f * ((r - g) / (mx - mn) + 4.f);
                return h;
        }

        static void note_fill_color(std::uint8_t track, std::uint8_t channel,
                std::uint8_t velocity, bool selected,
                std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
        {
                const float hue = track_hue(track, channel);
                const float v = (selected ? 0.80f : 0.55f) + 0.45f * velocity / 127.f;
                const float s = selected ? 0.45f : 0.85f;
                hsv_to_rgb(hue, s, std::min(v, 1.f), r, g, b);
        }

        static void inactive_note_color(std::uint8_t track, std::uint8_t channel,
                std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
        {
                std::uint8_t cr, cg, cb;
                hsv_to_rgb(track_hue(track, channel), 0.85f, 1.f, cr, cg, cb);
                const std::uint8_t luma = std::uint8_t((cr * 2 + cg * 3 + cb) / 6);
                r = g = b = std::uint8_t(0x28 + luma / 4); // dark desaturated gray
        }

        // ========================================================================
        // Drawing
        // ========================================================================

        void draw() override
        {
                std::lock_guard<std::recursive_mutex> locker(lock);

                const auto l = compute_layout();

                // Widget background (visible even before a file is loaded)
                glColor4ub(0x12, 0x12, 0x16, 0xFF);
                glBegin(GL_QUADS);
                glVertex2f(l.left, l.bottom);
                glVertex2f(l.right, l.bottom);
                glVertex2f(l.right, l.top);
                glVertex2f(l.left, l.top);
                glEnd();

                if (!editor || !editor->is_file_loaded())
                        return;

                const auto view_start = editor->get_view_start_tick();
                const auto view_duration = editor->get_view_duration_ticks();
                const auto key_low = editor->get_view_key_low();
                const auto key_high = editor->get_view_key_high();
                const auto ppqn = editor->get_ppqn();

                if (!view_duration || key_low > key_high || l.notes_w <= 0.f || l.roll_h <= 0.f)
                        return;

                const float key_height = l.roll_h / float(key_high - key_low + 1);
                if (key_height <= 0.f)
                        return;

                draw_key_lanes(l, key_low, key_high, key_height);
                draw_time_grid(l, view_start, view_duration, ppqn);
                draw_keyboard(l, key_low, key_high, key_height);
                draw_notes(l, view_start, view_duration, key_low, key_high, key_height);

                const auto sel = editor->get_selection();
                if (sel.is_active())
                        draw_selection(sel, l, view_start, view_duration, key_low, key_high, key_height);

                if (drawing)
                        draw_pending_note(l, view_start, view_duration, key_low, key_high, key_height);

                if (velocity_lane_visible)
                        draw_velocity_lane(l, view_start, view_duration, sel);
        }

        void draw_key_lanes(const layout& l, std::uint8_t key_low, std::uint8_t key_high, float key_height)
        {
                glBegin(GL_QUADS);
                for (std::uint16_t key = key_low; key <= key_high; ++key)
                {
                        if (is_white_key(std::uint8_t(key)))
                                glColor4ub(0x24, 0x24, 0x2A, 0xFF);
                        else
                                glColor4ub(0x1A, 0x1A, 0x1F, 0xFF);

                        const float y = l.roll_bottom + (key - key_low) * key_height;
                        glVertex2f(l.notes_x, y);
                        glVertex2f(l.notes_x + l.notes_w, y);
                        glVertex2f(l.notes_x + l.notes_w, y + key_height);
                        glVertex2f(l.notes_x, y + key_height);
                }
                glEnd();

                glColor4ub(0x50, 0x50, 0x58, 0xFF);
                glLineWidth(1.f);
                glBegin(GL_LINES);
                for (std::uint16_t key = key_low; key <= key_high; ++key)
                {
                        if (key % 12 == 0) // line under every C
                        {
                                const float y = l.roll_bottom + (key - key_low) * key_height;
                                glVertex2f(l.notes_x, y);
                                glVertex2f(l.notes_x + l.notes_w, y);
                        }
                }
                glEnd();
        }

        void draw_time_grid(const layout& l, tick_type view_start, tick_type view_duration, std::uint16_t ppqn)
        {
                if (!ppqn)
                        return;

                tick_type step = ppqn;
                while (step * l.notes_w / float(view_duration) < 8.f && step < view_duration)
                        step *= 2;

                glLineWidth(1.f);
                glBegin(GL_LINES);
                const tick_type first = ((view_start + step - 1) / step) * step;
                for (tick_type tick = first; tick < view_start + view_duration; tick += step)
                {
                        const bool is_bar = (tick % (tick_type(ppqn) * 4)) == 0;
                        if (is_bar)
                                glColor4ub(0x58, 0x58, 0x60, 0xFF);
                        else
                                glColor4ub(0x34, 0x34, 0x3A, 0xFF);

                        const float x = l.notes_x + float(tick - view_start) / float(view_duration) * l.notes_w;
                        glVertex2f(x, l.roll_bottom);
                        glVertex2f(x, l.top);
                }
                glEnd();
        }

        void draw_keyboard(const layout& l, std::uint8_t key_low, std::uint8_t key_high, float key_height)
        {
                glBegin(GL_QUADS);
                for (std::uint16_t key = key_low; key <= key_high; ++key)
                {
                        if (is_white_key(std::uint8_t(key)))
                                glColor3ub(0xE8, 0xE8, 0xE8);
                        else
                                glColor3ub(0x20, 0x20, 0x20);

                        const float y = l.roll_bottom + (key - key_low) * key_height;
                        glVertex2f(l.left, y);
                        glVertex2f(l.left + keyboard_width, y);
                        glVertex2f(l.left + keyboard_width, y + key_height);
                        glVertex2f(l.left, y + key_height);
                }
                glEnd();

                glColor3ub(0x60, 0x60, 0x60);
                glLineWidth(1.f);
                glBegin(GL_LINES);
                for (std::uint16_t key = key_low; key <= key_high; ++key)
                {
                        const auto note_in_octave = key % 12;
                        if (note_in_octave == 0 || note_in_octave == 5) // white-key seams: B/C and E/F
                        {
                                const float y = l.roll_bottom + (key - key_low) * key_height;
                                glVertex2f(l.left, y);
                                glVertex2f(l.left + keyboard_width, y);
                        }
                }
                glEnd();

                glColor3ub(0x00, 0x00, 0x00);
                glBegin(GL_LINES);
                glVertex2f(l.left + keyboard_width, l.roll_bottom);
                glVertex2f(l.left + keyboard_width, l.top);
                glEnd();
        }

        void draw_notes(const layout& l, tick_type view_start, tick_type view_duration,
                std::uint8_t key_low, std::uint8_t key_high, float key_height)
        {
                const auto notes = editor->get_notes_in_range(
                        view_start, view_start + view_duration, key_low, key_high);
                const auto active_track = editor->get_active_track();
                const auto sel = editor->get_selection();

                // Inactive tracks first (gray), active track on top (full color)
                for (int pass = 0; pass < 2; ++pass)
                {
                        const bool active_pass = (pass == 1);
                        for (const auto& note : notes)
                        {
                                if ((note.track_index == active_track) != active_pass)
                                        continue;

                                float x_start, x_end, y_bottom, y_top;
                                if (!note_rect(note, l, view_start, view_duration, key_low, key_height,
                                        x_start, x_end, y_bottom, y_top))
                                        continue;

                                std::uint8_t r, g, b;
                                const bool selected = active_pass && sel.is_active() && sel.intersects(note);
                                if (active_pass)
                                        note_fill_color(note.track_index, note.channel, note.velocity, selected, r, g, b);
                                else
                                        inactive_note_color(note.track_index, note.channel, r, g, b);

                                glColor3ub(r, g, b);
                                glBegin(GL_QUADS);
                                glVertex2f(x_start, y_bottom);
                                glVertex2f(x_end, y_bottom);
                                glVertex2f(x_end, y_top);
                                glVertex2f(x_start, y_top);
                                glEnd();

                                if (x_end - x_start > 2.f)
                                {
                                        if (selected)
                                                glColor3ub(0xFF, 0xFF, 0xFF);
                                        else
                                                glColor3ub(0x00, 0x00, 0x00);
                                        glLineWidth(1.f);
                                        glBegin(GL_LINE_LOOP);
                                        glVertex2f(x_start, y_bottom);
                                        glVertex2f(x_end, y_bottom);
                                        glVertex2f(x_end, y_top);
                                        glVertex2f(x_start, y_top);
                                        glEnd();
                                }
                        }
                }
        }

        bool note_rect(const piano_note& note, const layout& l,
                tick_type view_start, tick_type view_duration,
                std::uint8_t key_low, float key_height,
                float& x_start, float& x_end, float& y_bottom, float& y_top) const
        {
                const float area_right = l.notes_x + l.notes_w;

                x_start = note.start_tick > view_start
                        ? l.notes_x + float(note.start_tick - view_start) / float(view_duration) * l.notes_w
                        : l.notes_x;
                x_end = note.end_tick < view_start + view_duration
                        ? l.notes_x + float(note.end_tick - view_start) / float(view_duration) * l.notes_w
                        : area_right;

                if (x_end - x_start < 1.f)
                        x_end = x_start + 1.f;
                if (x_start >= area_right)
                        return false;
                x_end = std::min(x_end, area_right);

                y_bottom = l.roll_bottom + (note.key - key_low) * key_height;
                y_top = y_bottom + key_height;
                return true;
        }

        void draw_selection(const midi_editor::selection& sel, const layout& l,
                tick_type view_start, tick_type view_duration,
                std::uint8_t key_low, std::uint8_t key_high, float key_height)
        {
                float x_start, x_end, y_bottom, y_top;
                if (!selection_rect(sel.begin_tick, sel.end_tick, sel.key_begin, sel.key_end,
                        l, view_start, view_duration, key_low, key_high, key_height,
                        x_start, x_end, y_bottom, y_top))
                        return;

                glColor4ub(0xFF, 0xFF, 0xFF, 0x28);
                glBegin(GL_QUADS);
                glVertex2f(x_start, y_bottom);
                glVertex2f(x_end, y_bottom);
                glVertex2f(x_end, y_top);
                glVertex2f(x_start, y_top);
                glEnd();

                glColor4ub(0xFF, 0xFF, 0xFF, 0xB0);
                glLineWidth(1.f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(x_start, y_bottom);
                glVertex2f(x_end, y_bottom);
                glVertex2f(x_end, y_top);
                glVertex2f(x_start, y_top);
                glEnd();

                // Ghost of the selection at its pending position while dragging it
                if (moving && (move_delta_ticks || move_delta_keys))
                {
                        const auto shift = [&](tick_type t) -> tick_type
                        {
                                auto v = sgtick_type(t) + move_delta_ticks;
                                return v > 0 ? tick_type(v) : 0;
                        };
                        const int kb = std::clamp(int(sel.key_begin) + move_delta_keys, 0, 127);
                        const int ke = std::clamp(int(sel.key_end) + move_delta_keys, 0, 127);

                        if (selection_rect(shift(sel.begin_tick), shift(sel.end_tick),
                                std::uint8_t(kb), std::uint8_t(ke),
                                l, view_start, view_duration, key_low, key_high, key_height,
                                x_start, x_end, y_bottom, y_top))
                        {
                                glColor4ub(0x7F, 0xCF, 0xFF, 0xB0);
                                glLineWidth(1.f);
                                glBegin(GL_LINE_LOOP);
                                glVertex2f(x_start, y_bottom);
                                glVertex2f(x_end, y_bottom);
                                glVertex2f(x_end, y_top);
                                glVertex2f(x_start, y_top);
                                glEnd();
                        }
                }
        }

        bool selection_rect(tick_type begin_tick, tick_type end_tick,
                std::uint8_t sel_key_begin, std::uint8_t sel_key_end,
                const layout& l, tick_type view_start, tick_type view_duration,
                std::uint8_t key_low, std::uint8_t key_high, float key_height,
                float& x_start, float& x_end, float& y_bottom, float& y_top) const
        {
                const tick_type view_end = view_start + view_duration;
                if (end_tick <= view_start || begin_tick >= view_end ||
                        sel_key_end < key_low || sel_key_begin > key_high)
                        return false;

                const float area_right = l.notes_x + l.notes_w;

                x_start = begin_tick > view_start
                        ? l.notes_x + float(begin_tick - view_start) / float(view_duration) * l.notes_w
                        : l.notes_x;
                x_end = end_tick < view_end
                        ? l.notes_x + float(end_tick - view_start) / float(view_duration) * l.notes_w
                        : area_right;

                y_bottom = sel_key_begin > key_low
                        ? l.roll_bottom + (sel_key_begin - key_low) * key_height
                        : l.roll_bottom;
                y_top = sel_key_end < key_high
                        ? l.roll_bottom + (sel_key_end - key_low + 1) * key_height
                        : l.top;
                return true;
        }

        void draw_pending_note(const layout& l, tick_type view_start, tick_type view_duration,
                std::uint8_t key_low, std::uint8_t key_high, float key_height)
        {
                if (draw_key < key_low || draw_key > key_high)
                        return;

                piano_note pending(draw_start_tick, draw_end_tick, draw_key, 100);
                pending.track_index = editor->get_active_track();
                pending.channel = editor->get_active_track_channel();

                float x_start, x_end, y_bottom, y_top;
                if (!note_rect(pending, l, view_start, view_duration, key_low, key_height,
                        x_start, x_end, y_bottom, y_top))
                        return;

                std::uint8_t r, g, b;
                note_fill_color(pending.track_index, pending.channel, pending.velocity, false, r, g, b);

                glColor4ub(r, g, b, 0xA0);
                glBegin(GL_QUADS);
                glVertex2f(x_start, y_bottom);
                glVertex2f(x_end, y_bottom);
                glVertex2f(x_end, y_top);
                glVertex2f(x_start, y_top);
                glEnd();

                glColor3ub(0xFF, 0xFF, 0xFF);
                glLineWidth(1.f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(x_start, y_bottom);
                glVertex2f(x_end, y_bottom);
                glVertex2f(x_end, y_top);
                glVertex2f(x_start, y_top);
                glEnd();
        }

        void draw_velocity_lane(const layout& l, tick_type view_start, tick_type view_duration,
                const midi_editor::selection& sel)
        {
                const float lane_h = l.lane_top - l.lane_bottom;
                if (lane_h <= 4.f)
                        return;

                // Lane background + divider
                glColor4ub(0x0D, 0x0D, 0x11, 0xFF);
                glBegin(GL_QUADS);
                glVertex2f(l.left, l.lane_bottom);
                glVertex2f(l.right, l.lane_bottom);
                glVertex2f(l.right, l.lane_top);
                glVertex2f(l.left, l.lane_top);
                glEnd();

                glColor4ub(0x3A, 0x3A, 0x44, 0xFF);
                glBegin(GL_QUADS);
                glVertex2f(l.left, l.lane_top);
                glVertex2f(l.right, l.lane_top);
                glVertex2f(l.right, l.lane_top + divider_height);
                glVertex2f(l.left, l.lane_top + divider_height);
                glEnd();

                // Velocity bars of the active track's visible notes
                const auto notes = editor->get_notes_in_range(view_start, view_start + view_duration);
                const auto active_track = editor->get_active_track();
                const bool has_selection = sel.is_active();
                const float usable_h = lane_h - 3.f;

                glBegin(GL_QUADS);
                for (const auto& note : notes)
                {
                        if (note.track_index != active_track || note.start_tick < view_start)
                                continue;

                        const float x = l.notes_x + float(note.start_tick - view_start) / float(view_duration) * l.notes_w;
                        if (x < l.notes_x || x > l.notes_x + l.notes_w - 1.f)
                                continue;

                        const bool selected = has_selection && sel.intersects(note);
                        std::uint8_t r, g, b;
                        note_fill_color(note.track_index, note.channel, note.velocity, selected, r, g, b);
                        if (has_selection && !selected)
                        {
                                r /= 3; g /= 3; b /= 3; // dim non-selected bars while a selection is active
                        }

                        const float h = 2.f + usable_h * note.velocity / 127.f;
                        glColor3ub(r, g, b);
                        glVertex2f(x, l.lane_bottom + 1.f);
                        glVertex2f(x + 2.f, l.lane_bottom + 1.f);
                        glVertex2f(x + 2.f, l.lane_bottom + 1.f + h);
                        glVertex2f(x, l.lane_bottom + 1.f + h);
                }
                glEnd();

                // Line tool guide
                if (lane_line)
                {
                        const float ax = l.notes_x + float(lane_anchor_tick > view_start ? lane_anchor_tick - view_start : 0)
                                / float(view_duration) * l.notes_w;
                        const float ay = l.lane_bottom + 1.f + (lane_h - 3.f) * lane_anchor_vel / 127.f;

                        glColor4ub(0xFF, 0xFF, 0xFF, 0xC0);
                        glLineWidth(1.f);
                        glBegin(GL_LINES);
                        glVertex2f(ax, ay);
                        glVertex2f(lane_cursor_x, lane_cursor_y);
                        glEnd();
                }
        }

        static bool is_white_key(std::uint8_t key)
        {
                constexpr bool white_keys[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
                return white_keys[key % 12];
        }

        // ========================================================================
        // Widget plumbing
        // ========================================================================

        void safe_move(float dx, float dy) override
        {
                std::lock_guard<std::recursive_mutex> locker(lock);
                xpos += dx;
                ypos += dy;
        }

        void safe_change_position(float new_x, float new_y) override
        {
                std::lock_guard<std::recursive_mutex> locker(lock);
                new_x -= xpos;
                new_y -= ypos;
                safe_move(new_x, new_y);
        }

        void safe_change_position_argumented(std::uint8_t, float, float) override
        {
                return;
        }

        void keyboard_handler(char) override
        {
                return;
        }

        void safe_string_replace(std::string) override
        {
                return;
        }

        // ========================================================================
        // Audition helpers
        // ========================================================================

        void audition_start(std::uint8_t key, std::uint8_t velocity, std::uint8_t channel)
        {
                if (!note_audition)
                        return;
                if (audition_key == int(key) && audition_channel == channel)
                        return;
                audition_stop();
                note_audition(key, velocity, channel, true);
                audition_key = key;
                audition_channel = channel;
        }

        void audition_stop()
        {
                if (audition_key >= 0 && note_audition)
                        note_audition(std::uint8_t(audition_key), 0, audition_channel, false);
                audition_key = -1;
        }

        // ========================================================================
        // Velocity lane gesture helpers
        // ========================================================================

        std::uint8_t lane_velocity_at(float my, const layout& l) const
        {
                const float lane_h = l.lane_top - l.lane_bottom;
                const float rel = (std::clamp(my, l.lane_bottom, l.lane_top) - l.lane_bottom) / std::max(lane_h, 1.f);
                return std::uint8_t(std::clamp(int(rel * 127.f + 0.5f), 1, 127));
        }

        void record_velocity_change(const piano_note& ident, std::uint8_t new_vel)
        {
                std::uint8_t old_vel = 0;
                if (!editor->set_note_velocity_transient(ident, new_vel, old_vel))
                        return;

                const auto key = std::make_tuple(ident.start_tick, ident.end_tick,
                        ident.key, ident.channel, ident.track_index);
                auto it = gesture.find(key);
                if (it == gesture.end())
                        gesture[key] = velocity_entry{ident, old_vel, new_vel};
                else
                        it->second.new_velocity = new_vel;
        }

        // Set velocity for active-track notes starting within [t0, t1]; honors the selection
        void lane_apply_flat(tick_type t0, tick_type t1, std::uint8_t vel,
                const midi_editor::selection& sel)
        {
                if (t0 > t1)
                        std::swap(t0, t1);

                const auto active_track = editor->get_active_track();
                const auto notes = editor->get_notes_in_range(t0, t1 + 1);
                for (const auto& note : notes)
                {
                        if (note.track_index != active_track ||
                                note.start_tick < t0 || note.start_tick > t1)
                                continue;
                        if (sel.is_active() && !sel.intersects(note))
                                continue;
                        record_velocity_change(note, vel);
                }
        }

        // Linear ramp between (tick_a, vel_a) and (tick_b, vel_b)
        void lane_apply_ramp(tick_type tick_a, std::uint8_t vel_a,
                tick_type tick_b, std::uint8_t vel_b,
                const midi_editor::selection& sel)
        {
                auto t0 = tick_a, t1 = tick_b;
                if (t0 > t1)
                        std::swap(t0, t1);

                const auto active_track = editor->get_active_track();
                const auto notes = editor->get_notes_in_range(t0, t1 + 1);
                for (const auto& note : notes)
                {
                        if (note.track_index != active_track ||
                                note.start_tick < t0 || note.start_tick > t1)
                                continue;
                        if (sel.is_active() && !sel.intersects(note))
                                continue;

                        float f = 0.f;
                        if (tick_b != tick_a)
                                f = float(sgtick_type(note.start_tick) - sgtick_type(tick_a)) /
                                float(sgtick_type(tick_b) - sgtick_type(tick_a));
                        const int vel = int(vel_a + (float(vel_b) - float(vel_a)) * std::clamp(f, 0.f, 1.f) + 0.5f);
                        record_velocity_change(note, std::uint8_t(std::clamp(vel, 1, 127)));
                }
        }

        void commit_lane_gesture()
        {
                if (gesture.empty())
                        return;
                std::vector<velocity_entry> entries;
                entries.reserve(gesture.size());
                for (auto& [_, entry] : gesture)
                        entries.push_back(entry);
                gesture.clear();
                editor->commit_velocity_gesture(std::move(entries));
        }

        // ========================================================================
        // Mouse interaction
        // ========================================================================

        [[nodiscard]] bool mouse_handler(float mx, float my, char button, char state) override
        {
                std::lock_guard<std::recursive_mutex> locker(lock);

                if (!editor || !editor->is_file_loaded())
                        return false;

                const auto l = compute_layout();

                const auto view_start = editor->get_view_start_tick();
                const auto view_duration = editor->get_view_duration_ticks();
                const auto key_low = editor->get_view_key_low();
                const auto key_high = editor->get_view_key_high();
                const auto ppqn = editor->get_ppqn();

                if (!view_duration || l.notes_w <= 0.f || l.roll_h <= 0.f || key_low > key_high)
                        return false;

                const float key_height = l.roll_h / float(key_high - key_low + 1);

                const bool inside = mx >= l.left && mx <= l.right && my >= l.bottom && my <= l.top;
                const bool in_roll = inside && my >= l.roll_bottom;
                const bool in_notes_area = in_roll && mx >= l.notes_x;
                const bool in_keyboard = in_roll && mx < l.notes_x;
                const bool on_divider = velocity_lane_visible && inside && mx >= l.notes_x &&
                        my >= l.lane_top - 2.f && my <= l.lane_top + divider_height + 2.f;
                const bool in_lane = velocity_lane_visible && inside && mx >= l.notes_x &&
                        my < l.lane_top - 2.f;

                const auto tick_at = [&](float x) -> sgtick_type
                {
                        const float rel = (x - l.notes_x) / l.notes_w;
                        return sgtick_type(view_start) + sgtick_type(rel * float(view_duration));
                };
                const auto tick_at_clamped = [&](float x) -> tick_type
                {
                        return tick_type(std::max<sgtick_type>(0, tick_at(std::clamp(x, l.notes_x, l.notes_x + l.notes_w))));
                };
                const auto key_at = [&](float y) -> std::uint8_t
                {
                        const int k = int(key_low) + int((std::clamp(y, l.roll_bottom, l.top) - l.roll_bottom) / key_height);
                        return std::uint8_t(std::clamp(k, 0, 127));
                };
                const tick_type snap = std::max<tick_type>(1, tick_type(ppqn) / 4);

                const bool left_press = (button == -1 && state == -1);
                const bool left_release = (button == -1 && state == 1);
                const bool right_press = (button == 1 && state == -1);
                const bool right_release = (button == 1 && state == 1);

                // ---- Divider drag (lane resize) ----
                if (divider_dragging)
                {
                        velocity_lane_height = std::clamp(my - l.bottom, min_lane_height, data.height * 0.6f);
                        if (left_release)
                                divider_dragging = false;
                        return true;
                }

                // ---- Velocity lane gestures ----
                if (lane_painting || lane_line)
                {
                        const auto sel = editor->get_selection();
                        const auto cur_tick = tick_at_clamped(mx);
                        const auto cur_vel = lane_velocity_at(my, l);

                        if (lane_painting)
                        {
                                lane_apply_flat(lane_last_tick, cur_tick, cur_vel, sel);
                                lane_last_tick = cur_tick;
                                if (left_release)
                                {
                                        lane_painting = false;
                                        commit_lane_gesture();
                                }
                                return true;
                        }

                        // line tool
                        lane_cursor_x = std::clamp(mx, l.notes_x, l.notes_x + l.notes_w);
                        lane_cursor_y = std::clamp(my, l.lane_bottom, l.lane_top);
                        if (right_release)
                        {
                                lane_line = false;
                                lane_apply_ramp(lane_anchor_tick, lane_anchor_vel, cur_tick, cur_vel, sel);
                                commit_lane_gesture();
                        }
                        return true;
                }

                // ---- Keyboard vertical scroll (right-drag) ----
                if (kb_scrolling)
                {
                        const float dy = my - kb_last_my;
                        kb_last_my = my;

                        const float dkeys_f = -dy / key_height + kb_accum_keys;
                        const int dkeys = int(dkeys_f);
                        kb_accum_keys = dkeys_f - float(dkeys);

                        if (dkeys)
                        {
                                const int span = int(key_high) - int(key_low);
                                int new_low = std::clamp(int(key_low) + dkeys, 0, 127 - span);
                                editor->set_view_keys(std::uint8_t(new_low), std::uint8_t(new_low + span));
                        }
                        if (right_release)
                                kb_scrolling = false;
                        return true;
                }

                // ---- Wheel ----
                if (state == -1 && (button == 2 || button == 3))
                {
                        const bool zoom_in = (button == 2);

                        if (in_keyboard || (in_roll && !in_notes_area))
                        {
                                // Vertical zoom around the cursor key
                                const int span = int(key_high) - int(key_low) + 1;
                                int new_span = zoom_in ? std::max(12, int(span / 1.25f))
                                        : std::min(128, int(span * 1.25f) + 1);
                                if (new_span != span)
                                {
                                        const int anchor = key_at(my);
                                        const float rel = float(anchor - key_low) / float(span);
                                        int new_low = std::clamp(anchor - int(rel * new_span + 0.5f), 0, 128 - new_span);
                                        editor->set_view_keys(std::uint8_t(new_low), std::uint8_t(new_low + new_span - 1));
                                }
                                return true;
                        }

                        if (in_notes_area || in_lane)
                        {
                                // Horizontal zoom around the cursor tick
                                constexpr float factor = 1.25f;
                                const auto anchor = std::max<sgtick_type>(0, tick_at(std::max(mx, l.notes_x)));
                                const double scale = zoom_in ? 1.0 / factor : factor;
                                const auto new_duration = std::max<tick_type>(16, tick_type(double(view_duration) * scale));

                                const double rel = double(anchor - sgtick_type(view_start)) / double(view_duration);
                                const auto new_start = anchor - sgtick_type(rel * double(new_duration));
                                editor->set_view_range(tick_type(std::max<sgtick_type>(0, new_start)), new_duration);
                                return true;
                        }

                        return false;
                }

                // ---- Lane / divider presses ----
                if (left_press && on_divider)
                {
                        divider_dragging = true;
                        return true;
                }
                if (in_lane && (left_press || right_press))
                {
                        const auto cur_tick = tick_at_clamped(mx);
                        const auto cur_vel = lane_velocity_at(my, l);
                        gesture.clear();

                        if (left_press)
                        {
                                lane_painting = true;
                                lane_last_tick = cur_tick;
                                // A plain click gets a few pixels of tolerance around the cursor
                                const auto tol = tick_type(2.f / l.notes_w * float(view_duration)) + 1;
                                lane_apply_flat(cur_tick > tol ? cur_tick - tol : 0, cur_tick + tol,
                                        cur_vel, editor->get_selection());
                        }
                        else
                        {
                                lane_line = true;
                                lane_anchor_tick = cur_tick;
                                lane_anchor_vel = cur_vel;
                                lane_cursor_x = std::clamp(mx, l.notes_x, l.notes_x + l.notes_w);
                                lane_cursor_y = std::clamp(my, l.lane_bottom, l.lane_top);
                        }
                        return true;
                }

                // ---- Keyboard right-drag scroll ----
                if (right_press && in_keyboard)
                {
                        kb_scrolling = true;
                        kb_last_my = my;
                        kb_accum_keys = 0.f;
                        return true;
                }

                // ---- Right button on the roll: pan (drag) or track pick (click) ----
                if (!right_down && right_press && in_notes_area)
                {
                        right_down = true;
                        panning = false;
                        right_down_mx = mx;
                        right_down_my = my;
                        pan_last_mx = mx;
                        pan_accum_ticks = 0.f;
                        return true;
                }

                if (right_down)
                {
                        if (right_release)
                        {
                                if (!panning)
                                {
                                        // Plain right-click: switch to the track of the note under the cursor.
                                        // Tolerance of ~3 pixels / 1 semitone forgives near-misses.
                                        const auto tick_tolerance = tick_type(3.f / l.notes_w * float(view_duration)) + 1;
                                        piano_note picked;
                                        if (editor->find_note_at(tick_at_clamped(mx), key_at(my), picked, tick_tolerance, 1) &&
                                                picked.track_index != editor->get_active_track())
                                        {
                                                editor->set_active_track(picked.track_index);
                                                if (on_track_changed)
                                                        on_track_changed();
                                        }
                                }
                                right_down = false;
                                panning = false;
                                return true;
                        }

                        if (!panning && (std::fabs(mx - right_down_mx) > 2.f || std::fabs(my - right_down_my) > 2.f))
                                panning = true;

                        if (panning)
                        {
                                const float dx = mx - pan_last_mx;
                                pan_last_mx = mx;

                                // Accumulate fractional ticks so panning works at deep zoom levels
                                const float delta_f = -dx / l.notes_w * float(view_duration) + pan_accum_ticks;
                                const auto delta_i = sgtick_type(delta_f);
                                pan_accum_ticks = delta_f - float(delta_i);

                                auto new_start = sgtick_type(view_start) + delta_i;
                                if (new_start < 0)
                                {
                                        new_start = 0;
                                        pan_accum_ticks = 0.f;
                                }
                                editor->set_view_range(tick_type(new_start), view_duration);
                        }
                        return true;
                }

                // ---- Left button on the roll, by tool ----
                if (!selecting && !moving && !drawing && !erasing)
                {
                        if (!left_press || !in_notes_area)
                                return false;

                        const auto tick = tick_at_clamped(mx);
                        const auto key = key_at(my);

                        switch (current_tool)
                        {
                                case tool_mode::select:
                                {
                                        const auto sel = editor->get_selection();
                                        if (sel.is_active() && sel.contains(tick, key))
                                        {
                                                moving = true;
                                                anchor_tick = tick;
                                                anchor_key = key;
                                                move_delta_ticks = 0;
                                                move_delta_keys = 0;
                                        }
                                        else
                                        {
                                                selecting = true;
                                                anchor_tick = tick;
                                                anchor_key = key;
                                                editor->set_selection(tick, tick, key, key, editor->get_active_track());
                                        }

                                        // Audition the clicked note, if any
                                        piano_note under;
                                        if (editor->find_note_at(tick, key, under))
                                                audition_start(under.key, under.velocity, under.channel);
                                        return true;
                                }
                                case tool_mode::draw:
                                {
                                        drawing = true;
                                        draw_start_tick = (tick / snap) * snap;
                                        draw_end_tick = draw_start_tick + snap;
                                        draw_key = key;
                                        audition_start(key, 100, editor->get_active_track_channel());
                                        return true;
                                }
                                case tool_mode::erase:
                                {
                                        erasing = true;
                                        editor->erase_note_at(tick, key, editor->get_active_track());
                                        return true;
                                }
                        }
                        return false;
                }

                // ---- Ongoing left drags on the roll ----
                const auto cur_tick = tick_at_clamped(mx);
                const auto cur_key = key_at(my);

                if (selecting)
                {
                        const auto t0 = std::min(anchor_tick, cur_tick);
                        const auto t1 = std::max(anchor_tick, cur_tick);
                        const auto k0 = std::min(anchor_key, cur_key);
                        const auto k1 = std::max(anchor_key, cur_key);
                        editor->set_selection(t0, t1, k0, k1, editor->get_active_track());

                        if (left_release)
                        {
                                selecting = false;
                                audition_stop();
                                if (t0 == t1) // plain click: drop the empty selection
                                        editor->clear_selection();
                        }
                        return true;
                }

                if (moving)
                {
                        const auto prev_keys = move_delta_keys;
                        move_delta_ticks = sgtick_type(cur_tick) - sgtick_type(anchor_tick);
                        move_delta_keys = int(cur_key) - int(anchor_key);

                        if (move_delta_keys != prev_keys)
                                audition_start(cur_key, 100, editor->get_active_track_channel());

                        if (left_release)
                        {
                                if (move_delta_ticks || move_delta_keys)
                                        editor->move_selected_notes(move_delta_ticks, move_delta_keys);
                                moving = false;
                                move_delta_ticks = 0;
                                move_delta_keys = 0;
                                audition_stop();
                        }
                        return true;
                }

                if (drawing)
                {
                        if (cur_key != draw_key)
                                audition_start(cur_key, 100, editor->get_active_track_channel());
                        draw_key = cur_key;
                        const auto end_snapped = ((cur_tick + snap) / snap) * snap;
                        draw_end_tick = std::max(draw_start_tick + snap, end_snapped);

                        if (left_release)
                        {
                                drawing = false;
                                audition_stop();
                                editor->insert_note_active_track(draw_start_tick, draw_end_tick, draw_key, 100);
                        }
                        return true;
                }

                if (erasing)
                {
                        editor->erase_note_at(cur_tick, cur_key, editor->get_active_track());
                        if (left_release)
                                erasing = false;
                        return true;
                }

                return false;
        }

        // Getters for rescale
        float get_width() const { return data.width; }
        float get_height() const { return data.height; }
};

#endif // SAFGUIF_L_MIDI_EDITOR_VIEWER
