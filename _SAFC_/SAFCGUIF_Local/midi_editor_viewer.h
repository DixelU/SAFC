#pragma once
#ifndef SAFGUIF_L_MIDI_EDITOR_VIEWER
#define SAFGUIF_L_MIDI_EDITOR_VIEWER

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/midi_editor.h"
#include <GL/freeglut.h>

/**
 * Piano roll viewer for midi_editor
 * Renders the piano roll visualization using OpenGL.
 *
 * Layout: time on X, pitch on Y (low keys at the bottom).
 * A vertical keyboard strip on the left shares the same per-key lanes
 * as the note area, so keys and notes always line up.
 *
 * Mouse: left-drag selects notes, right-drag pans, wheel zooms around the cursor.
 */
struct midi_editor_viewer : public handleable_ui_part
{
    using tick_type = midi_editor::tick_type;
    using sgtick_type = midi_editor::sgtick_type;

    float xpos, ypos;
    midi_editor* editor;

    struct view_data {
        float width = 400.f;
        float height = 250.f;
    } data;

    static constexpr float keyboard_width = 22.f;

    // Mouse interaction state
    bool selecting = false;
    bool panning = false;
    tick_type selection_anchor_tick = 0;
    std::uint8_t selection_anchor_key = 0;
    float pan_last_mx = 0.f;

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
    }

    void draw() override
    {
        std::lock_guard<std::recursive_mutex> locker(lock);

        const float left = xpos - data.width * 0.5f;
        const float right = xpos + data.width * 0.5f;
        const float bottom = ypos - data.height * 0.5f;
        const float top = ypos + data.height * 0.5f;

        // Widget background (visible even before a file is loaded)
        glColor4ub(0x12, 0x12, 0x16, 0xFF);
        glBegin(GL_QUADS);
        glVertex2f(left, bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left, top);
        glEnd();

        if (!editor || !editor->is_file_loaded())
            return;

        const auto view_start = editor->get_view_start_tick();
        const auto view_duration = editor->get_view_duration_ticks();
        const auto key_low = editor->get_view_key_low();
        const auto key_high = editor->get_view_key_high();
        const auto ppqn = editor->get_ppqn();

        if (!view_duration || key_low > key_high)
            return;

        const float notes_x = left + keyboard_width;
        const float notes_w = data.width - keyboard_width;
        const float key_height = data.height / float(key_high - key_low + 1);

        if (notes_w <= 0.f || key_height <= 0.f)
            return;

        draw_lanes(notes_x, notes_w, bottom, key_low, key_high, key_height);
        draw_time_grid(notes_x, notes_w, bottom, top, view_start, view_duration, ppqn);
        draw_keyboard(left, bottom, key_low, key_high, key_height);
        draw_notes(notes_x, notes_w, bottom, view_start, view_duration, key_low, key_high, key_height);

        const auto sel = editor->get_selection();
        if (sel.is_active())
            draw_selection(sel, notes_x, notes_w, bottom, view_start, view_duration, key_low, key_high, key_height);
    }

    // Horizontal per-key lanes: white-key lanes slightly lighter, line on every C
    void draw_lanes(float notes_x, float notes_w, float bottom,
                    std::uint8_t key_low, std::uint8_t key_high, float key_height)
    {
        glBegin(GL_QUADS);
        for (std::uint16_t key = key_low; key <= key_high; ++key)
        {
            if (is_white_key(std::uint8_t(key)))
                glColor4ub(0x24, 0x24, 0x2A, 0xFF);
            else
                glColor4ub(0x1A, 0x1A, 0x1F, 0xFF);

            const float y = bottom + (key - key_low) * key_height;
            glVertex2f(notes_x, y);
            glVertex2f(notes_x + notes_w, y);
            glVertex2f(notes_x + notes_w, y + key_height);
            glVertex2f(notes_x, y + key_height);
        }
        glEnd();

        glColor4ub(0x50, 0x50, 0x58, 0xFF);
        glLineWidth(1.f);
        glBegin(GL_LINES);
        for (std::uint16_t key = key_low; key <= key_high; ++key)
        {
            if (key % 12 == 0) // line under every C
            {
                const float y = bottom + (key - key_low) * key_height;
                glVertex2f(notes_x, y);
                glVertex2f(notes_x + notes_w, y);
            }
        }
        glEnd();
    }

    void draw_time_grid(float notes_x, float notes_w, float bottom, float top,
                        tick_type view_start, tick_type view_duration, std::uint16_t ppqn)
    {
        if (!ppqn)
            return;

        // Widen the step until grid lines are at least a few units apart
        tick_type step = ppqn;
        while (step * notes_w / float(view_duration) < 8.f && step < view_duration)
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

            const float x = notes_x + float(tick - view_start) / float(view_duration) * notes_w;
            glVertex2f(x, bottom);
            glVertex2f(x, top);
        }
        glEnd();
    }

    // Vertical keyboard strip sharing the note lanes, so keys align with notes
    void draw_keyboard(float left, float bottom,
                       std::uint8_t key_low, std::uint8_t key_high, float key_height)
    {
        glBegin(GL_QUADS);
        for (std::uint16_t key = key_low; key <= key_high; ++key)
        {
            if (is_white_key(std::uint8_t(key)))
                glColor3ub(0xE8, 0xE8, 0xE8);
            else
                glColor3ub(0x20, 0x20, 0x20);

            const float y = bottom + (key - key_low) * key_height;
            glVertex2f(left, y);
            glVertex2f(left + keyboard_width, y);
            glVertex2f(left + keyboard_width, y + key_height);
            glVertex2f(left, y + key_height);
        }
        glEnd();

        // Separators between adjacent white keys (B/C and E/F) + C markers
        glColor3ub(0x60, 0x60, 0x60);
        glLineWidth(1.f);
        glBegin(GL_LINES);
        for (std::uint16_t key = key_low; key <= key_high; ++key)
        {
            const auto note_in_octave = key % 12;
            if (note_in_octave == 0 || note_in_octave == 5) // C and F
            {
                const float y = bottom + (key - key_low) * key_height;
                glVertex2f(left, y);
                glVertex2f(left + keyboard_width, y);
            }
        }
        glEnd();

        // Border between keyboard and note area
        glColor3ub(0x00, 0x00, 0x00);
        glBegin(GL_LINES);
        glVertex2f(left + keyboard_width, bottom);
        glVertex2f(left + keyboard_width, bottom + data.height);
        glEnd();
    }

    void draw_notes(float notes_x, float notes_w, float bottom,
                    tick_type view_start, tick_type view_duration,
                    std::uint8_t key_low, std::uint8_t key_high, float key_height)
    {
        const auto notes = editor->get_notes_in_range(
            view_start, view_start + view_duration, key_low, key_high);

        const float area_right = notes_x + notes_w;

        for (const auto& note : notes)
        {
            // Clamp partially visible notes into the note area
            float x_start = note.start_tick > view_start
                ? notes_x + float(note.start_tick - view_start) / float(view_duration) * notes_w
                : notes_x;
            float x_end = note.end_tick < view_start + view_duration
                ? notes_x + float(note.end_tick - view_start) / float(view_duration) * notes_w
                : area_right;

            if (x_end - x_start < 1.f)
                x_end = x_start + 1.f;
            if (x_start >= area_right)
                continue;
            x_end = std::min(x_end, area_right);

            const float y_bottom = bottom + (note.key - key_low) * key_height;
            const float y_top = y_bottom + key_height;

            // Brightness follows velocity
            const std::uint8_t r = 0x5F + note.velocity;
            const std::uint8_t g = 0x20 + note.velocity / 2;
            const std::uint8_t b = 0x8F + note.velocity / 4;

            glColor3ub(r, g, b);
            glBegin(GL_QUADS);
            glVertex2f(x_start, y_bottom);
            glVertex2f(x_end, y_bottom);
            glVertex2f(x_end, y_top);
            glVertex2f(x_start, y_top);
            glEnd();

            // Border only when the note is wide enough to matter
            if (x_end - x_start > 2.f)
            {
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

    void draw_selection(const midi_editor::selection& sel,
                        float notes_x, float notes_w, float bottom,
                        tick_type view_start, tick_type view_duration,
                        std::uint8_t key_low, std::uint8_t key_high, float key_height)
    {
        const tick_type view_end = view_start + view_duration;
        if (sel.end_tick <= view_start || sel.begin_tick >= view_end ||
            sel.key_end < key_low || sel.key_begin > key_high)
            return;

        const float area_right = notes_x + notes_w;
        const float area_top = bottom + data.height;

        const float x_start = sel.begin_tick > view_start
            ? notes_x + float(sel.begin_tick - view_start) / float(view_duration) * notes_w
            : notes_x;
        const float x_end = sel.end_tick < view_end
            ? notes_x + float(sel.end_tick - view_start) / float(view_duration) * notes_w
            : area_right;

        const float y_bottom = sel.key_begin > key_low
            ? bottom + (sel.key_begin - key_low) * key_height
            : bottom;
        const float y_top = sel.key_end < key_high
            ? bottom + (sel.key_end - key_low + 1) * key_height
            : area_top;

        glColor4ub(0xFF, 0xFF, 0xFF, 0x30);
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
    }

    static bool is_white_key(std::uint8_t key)
    {
        constexpr bool white_keys[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
        return white_keys[key % 12];
    }

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

    [[nodiscard]] bool mouse_handler(float mx, float my, char button, char state) override
    {
        std::lock_guard<std::recursive_mutex> locker(lock);

        if (!editor || !editor->is_file_loaded())
            return false;

        const float left = xpos - data.width * 0.5f;
        const float bottom = ypos - data.height * 0.5f;
        const float notes_x = left + keyboard_width;
        const float notes_w = data.width - keyboard_width;

        const auto view_start = editor->get_view_start_tick();
        const auto view_duration = editor->get_view_duration_ticks();
        const auto key_low = editor->get_view_key_low();
        const auto key_high = editor->get_view_key_high();

        if (!view_duration || notes_w <= 0.f || key_low > key_high)
            return false;

        const float key_height = data.height / float(key_high - key_low + 1);

        const bool inside = mx >= left && mx <= left + data.width &&
                            my >= bottom && my <= bottom + data.height;

        const auto tick_at = [&](float x) -> sgtick_type
        {
            const float rel = (x - notes_x) / notes_w;
            return sgtick_type(view_start) + sgtick_type(rel * float(view_duration));
        };
        const auto key_at = [&](float y) -> int
        {
            return int(key_low) + int((y - bottom) / key_height);
        };

        // Wheel: zoom around the cursor position
        if (inside && state == -1 && (button == 2 || button == 3))
        {
            constexpr float factor = 1.25f;
            const auto anchor = std::max<sgtick_type>(0, tick_at(std::max(mx, notes_x)));
            const double scale = (button == 2) ? 1.0 / factor : factor;
            const auto new_duration = std::max<tick_type>(16, tick_type(double(view_duration) * scale));

            const double rel = double(anchor - sgtick_type(view_start)) / double(view_duration);
            const auto new_start = anchor - sgtick_type(rel * double(new_duration));
            editor->set_view_range(tick_type(std::max<sgtick_type>(0, new_start)), new_duration);
            return true;
        }

        // Left press in the note area: start a selection drag
        if (!selecting && !panning && button == -1 && state == -1 && inside && mx >= notes_x)
        {
            selecting = true;
            selection_anchor_tick = tick_type(std::max<sgtick_type>(0, tick_at(mx)));
            selection_anchor_key = std::uint8_t(std::clamp(key_at(my), 0, 127));
            editor->set_selection(selection_anchor_tick, selection_anchor_tick,
                                  selection_anchor_key, selection_anchor_key);
            return true;
        }

        if (selecting)
        {
            const auto cur_tick = tick_type(std::max<sgtick_type>(0,
                tick_at(std::clamp(mx, notes_x, notes_x + notes_w))));
            const auto cur_key = std::uint8_t(std::clamp(
                key_at(std::clamp(my, bottom, bottom + data.height)), 0, 127));

            const auto t0 = std::min(selection_anchor_tick, cur_tick);
            const auto t1 = std::max(selection_anchor_tick, cur_tick);
            const auto k0 = std::min(selection_anchor_key, cur_key);
            const auto k1 = std::max(selection_anchor_key, cur_key);
            editor->set_selection(t0, t1, k0, k1);

            if (button == -1 && state == 1)
            {
                selecting = false;
                if (t0 == t1) // plain click: drop the empty selection
                    editor->clear_selection();
            }
            return true;
        }

        // Right press in the note area: start panning
        if (!panning && button == 1 && state == -1 && inside && mx >= notes_x)
        {
            panning = true;
            pan_last_mx = mx;
            return true;
        }

        if (panning)
        {
            if (button == 1 && state == 1)
            {
                panning = false;
                return true;
            }

            const float dx = mx - pan_last_mx;
            pan_last_mx = mx;
            const auto delta_ticks = sgtick_type(-dx / notes_w * float(view_duration));
            const auto new_start = sgtick_type(view_start) + delta_ticks;
            editor->set_view_range(tick_type(std::max<sgtick_type>(0, new_start)), view_duration);
            return true;
        }

        return false;
    }

    // Getters for rescale
    float get_width() const { return data.width; }
    float get_height() const { return data.height; }
};

#endif // SAFGUIF_L_MIDI_EDITOR_VIEWER
