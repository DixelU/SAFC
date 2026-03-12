#pragma once
#ifndef SAFGUIF_L_MIDI_EDITOR_VIEWER
#define SAFGUIF_L_MIDI_EDITOR_VIEWER

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/midi_editor.h"
#include <GL/freeglut.h>

/**
 * Piano roll viewer for midi_editor
 * Renders the piano roll visualization using OpenGL
 */
struct midi_editor_viewer : public handleable_ui_part
{
    using tick_type = midi_editor::tick_type;
    
    float xpos, ypos;
    midi_editor* editor;
    
    // View settings
    float keyboard_height;
    float black_key_height;
    float black_key_margin;
    float zoom_level;
    
    // Geometry data (similar to simple_player::draw_data)
    struct view_data {
        float width = 400.f;
        float height = 250.f;
    } data;
    
    // Colors
    constexpr static std::uint32_t white_key_color = 0xFFFFFFFF;
    constexpr static std::uint32_t black_key_color = 0xFF000000;
    constexpr static std::uint32_t note_base_color = 0xFF7F008F;
    constexpr static std::uint32_t selection_color = 0x40FFFFFF;
    constexpr static std::uint32_t grid_color = 0x30FFFFFF;
    constexpr static std::uint32_t playback_cursor_color = 0xFFFF0000;
    
    midi_editor_viewer(float xpos, float ypos, midi_editor* ed = nullptr) :
        xpos(xpos),
        ypos(ypos),
        editor(ed),
        keyboard_height(40.f),
        black_key_height(25.f),
        black_key_margin(1.5f),
        zoom_level(1.0f)
    {
        data.width = 400.f;
        data.height = 250.f;
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
        
        if (!editor || !editor->is_file_loaded())
            return;

        // Get view parameters from editor
        auto view_start = editor->get_view_start_tick();
        auto view_duration = editor->get_view_duration_ticks();
        auto key_low = editor->get_view_key_low();
        auto key_high = editor->get_view_key_high();
        auto ppqn = editor->get_ppqn();
        
        // Calculate geometry
        float width = data.width;
        float height = data.height;
        float keyboard_y = -height * 0.4f + ypos;
        float keys_visible = key_high - key_low + 1;
        float key_height = height * 0.6f / keys_visible;
        
        // Draw time grid (vertical lines)
        glColor4ub(0x30, 0x30, 0x30, 0xFF);
        glLineWidth(1.f);
        glBegin(GL_LINES);
        for (tick_type beat = (view_start / ppqn) * ppqn; beat < view_start + view_duration; beat += ppqn)
        {
            float x = (beat - view_start) / (float)view_duration * width - width * 0.5f + xpos;
            glVertex2f(x, -height * 0.5f + ypos);
            glVertex2f(x, keyboard_y);
        }
        glEnd();
        
        // Draw keyboard
        draw_keyboard(keyboard_y, key_low, key_high, key_height, width);
        
        // Draw notes
        draw_notes(view_start, view_duration, key_low, key_high, keyboard_y, key_height, width);
        
        // Draw selection rectangle if active
        const auto& sel = editor->get_selection();
        if (sel.is_active())
        {
            draw_selection(sel, view_start, view_duration, key_low, key_high, keyboard_y, key_height, width);
        }
        
        // Draw playback cursor
        draw_playback_cursor(view_start, view_duration, keyboard_y, width, height);
    }

    void draw_keyboard(float keyboard_y, std::uint8_t key_low, std::uint8_t key_high, float key_height, float width)
    {
        float white_key_width = width / count_white_keys(key_low, key_high);
        
        // Draw white keys
        glColor3ub(0xFF, 0xFF, 0xFF);
        glBegin(GL_QUADS);
        for (std::uint8_t key = key_low; key <= key_high; ++key)
        {
            if (is_white_key(key))
            {
                float x = (key - key_low) * white_key_width - width * 0.5f + xpos;
                glVertex2f(x, keyboard_y);
                glVertex2f(x + white_key_width, keyboard_y);
                glVertex2f(x + white_key_width, keyboard_y - key_height);
                glVertex2f(x, keyboard_y - key_height);
            }
        }
        glEnd();
        
        // Draw black keys
        glColor3ub(0x00, 0x00, 0x00);
        glBegin(GL_QUADS);
        float black_key_width = white_key_width * 0.6f;
        float black_key_x_offset = white_key_width * 0.7f;
        for (std::uint8_t key = key_low; key <= key_high; ++key)
        {
            if (!is_white_key(key))
            {
                float x = (key - key_low) * white_key_width - width * 0.5f - black_key_width * 0.5f + black_key_x_offset + xpos;
                glVertex2f(x, keyboard_y);
                glVertex2f(x + black_key_width, keyboard_y);
                glVertex2f(x + black_key_width, keyboard_y - black_key_height);
                glVertex2f(x, keyboard_y - black_key_height);
            }
        }
        glEnd();
        
        // Draw key borders
        glColor3ub(0x00, 0x00, 0x00);
        glLineWidth(1.f);
        glBegin(GL_LINES);
        for (std::uint8_t key = key_low; key <= key_high; ++key)
        {
            if (is_white_key(key))
            {
                float x = (key - key_low) * white_key_width - width * 0.5f + xpos;
                glVertex2f(x, keyboard_y);
                glVertex2f(x, keyboard_y - key_height);
            }
        }
        glEnd();
    }

    void draw_notes(tick_type view_start, tick_type view_duration, 
                   std::uint8_t key_low, std::uint8_t key_high,
                   float keyboard_y, float key_height, float width)
    {
        auto notes = editor->get_notes_in_range(view_start, view_start + view_duration, key_low, key_high);
        
        for (const auto& note : notes)
        {
            // Calculate position
            float x_start = (note.start_tick - view_start) / (float)view_duration * width - width * 0.5f + xpos;
            float x_end = (note.end_tick - view_start) / (float)view_duration * width - width * 0.5f + xpos;
            float y = keyboard_y - (note.key - key_low) * key_height;
            
            // Color based on velocity
            std::uint8_t r = 0x7F + note.velocity / 2;
            std::uint8_t g = 0x00;
            std::uint8_t b = 0x8F + note.velocity / 4;
            
            glColor3ub(r, g, b);
            glBegin(GL_QUADS);
            glVertex2f(x_start, y);
            glVertex2f(x_end, y);
            glVertex2f(x_end, y - key_height * 0.8f);
            glVertex2f(x_start, y - key_height * 0.8f);
            glEnd();
            
            // Note border
            glColor3ub(0x00, 0x00, 0x00);
            glBegin(GL_LINE_LOOP);
            glVertex2f(x_start, y);
            glVertex2f(x_end, y);
            glVertex2f(x_end, y - key_height * 0.8f);
            glVertex2f(x_start, y - key_height * 0.8f);
            glEnd();
        }
    }

    void draw_selection(const midi_editor::selection& sel, 
                       tick_type view_start, tick_type view_duration,
                       std::uint8_t key_low, std::uint8_t key_high,
                       float keyboard_y, float key_height, float width)
    {
        float x_start = (sel.begin_tick - view_start) / (float)view_duration * width - width * 0.5f + xpos;
        float x_end = (sel.end_tick - view_start) / (float)view_duration * width - width * 0.5f + xpos;
        float y_top = keyboard_y - (sel.key_begin - key_low) * key_height;
        float y_bottom = keyboard_y - (sel.key_end - key_low + 1) * key_height;
        
        glColor4ub(0xFF, 0xFF, 0xFF, 0x40);
        glBegin(GL_QUADS);
        glVertex2f(x_start, y_top);
        glVertex2f(x_end, y_top);
        glVertex2f(x_end, y_bottom);
        glVertex2f(x_start, y_bottom);
        glEnd();
        
        glColor3ub(0xFF, 0xFF, 0xFF);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x_start, y_top);
        glVertex2f(x_end, y_top);
        glVertex2f(x_end, y_bottom);
        glVertex2f(x_start, y_bottom);
        glEnd();
    }

    void draw_playback_cursor(tick_type view_start, tick_type view_duration, float keyboard_y, float width, float height)
    {
        // Placeholder - draws cursor at center
        // Could integrate with simple_player to get real playback position
        glColor3ub(0xFF, 0x00, 0x00);
        glLineWidth(2.f);
        glBegin(GL_LINES);
        glVertex2f(xpos, -height * 0.5f + ypos);
        glVertex2f(xpos, keyboard_y);
        glEnd();
    }

    static bool is_white_key(std::uint8_t key)
    {
        constexpr bool white_keys[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
        return white_keys[key % 12];
    }

    static std::uint8_t count_white_keys(std::uint8_t key_low, std::uint8_t key_high)
    {
        std::uint8_t count = 0;
        for (std::uint8_t key = key_low; key <= key_high; ++key)
        {
            if (is_white_key(key))
                ++count;
        }
        return count;
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
        
        if (!editor)
            return false;
        
        // Convert screen coords to piano roll coords
        float width = 400.f;
        float height = 250.f;
        float keyboard_y = -height * 0.4f;
        
        mx -= xpos;
        my -= ypos;
        
        // Check if clicked in piano roll area
        if (my < keyboard_y && my > -height * 0.5f)
        {
            // Handle note selection, creation, etc.
            // TODO: Implement mouse interaction
        }
        
        return false;
    }

    // Getters for rescale
    float get_width() const { return 400.f; }
    float get_height() const { return 250.f; }
};

#endif // SAFGUIF_L_MIDI_EDITOR_VIEWER
