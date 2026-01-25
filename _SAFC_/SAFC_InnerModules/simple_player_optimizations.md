# Black MIDI Optimization Considerations

Optimizations for handling black MIDI files (millions of notes) in `simple_player.h`.

## 1. Rendering - Batch Drawing (Highest Impact)

Currently each note is drawn with separate `glBegin`/`glEnd` calls. For black MIDI this is devastating - potentially millions of draw calls per frame.

```cpp
// Current: O(n) draw calls
for each note:
    glBegin(GL_QUADS);
    glVertex2f(...);
    // ...
    glEnd();

// Better: Single draw call with vertex buffer
std::vector<float> vertices;
std::vector<uint32_t> colors;
vertices.reserve(visible_notes * 8);  // 4 verts * 2 floats
colors.reserve(visible_notes * 4);     // 4 verts * 1 color

for each note:
    // Just append to buffers
    vertices.insert(vertices.end(), {...});
    colors.insert(colors.end(), {...});

glEnableClientState(GL_VERTEX_ARRAY);
glEnableClientState(GL_COLOR_ARRAY);
glVertexPointer(2, GL_FLOAT, 0, vertices.data());
glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.data());
glDrawArrays(GL_QUADS, 0, vertices.size() / 2);
glDisableClientState(GL_VERTEX_ARRAY);
glDisableClientState(GL_COLOR_ARRAY);
```

Even better: use persistent mapped VBOs with streaming updates for true zero-copy rendering.

**Expected improvement: 10-100x for rendering**

---

## 2. Lock Contention - Double Buffering

Parser and renderer currently fight over a single mutex. With millions of notes being pushed while rendering, this becomes a severe bottleneck.

```cpp
struct visuals_viewport {
    // Two copies of visual state
    struct buffer_state {
        std::array<buffered_queue<buffered_note>, key_count> falling_notes;
        std::array<pending_list, key_count> pending;
    };

    buffer_state buffers[2];
    std::atomic<int> read_index{0};   // renderer reads from this
    int write_index{1};                // parser writes to this

    // Parser calls this periodically (e.g., every N notes or every M microseconds)
    void swap_buffers() {
        // Copy pending state if needed, then swap
        read_index.store(write_index, std::memory_order_release);
        write_index = 1 - write_index;
        // Clear write buffer for fresh writes
    }

    // Renderer reads lock-free
    const buffer_state& get_read_buffer() const {
        return buffers[read_index.load(std::memory_order_acquire)];
    }
};
```

Alternative: Triple buffering allows parser to never block even if renderer is slow.

**Expected improvement: Eliminates mutex contention entirely**

---

## 3. Pending Note Matching - Hash Map

Current `pending_list::find_and_remove` does linear search O(n) per note-off. For black MIDI with many overlapping notes on the same key, this becomes O(n^2) overall.

```cpp
// Current: O(n) search per note-off
struct pending_list {
    std::vector<pending_entry> entries;

    buffered_note* find_and_remove(uint32_t color_id) {
        for (auto it = entries.rbegin(); ...) {  // Linear scan
            if (it->color_id == color_id) ...
        }
    }
};

// Better: O(1) average with hash map
struct pending_list {
    // color_id -> stack of note pointers (for LIFO matching)
    std::unordered_map<uint32_t, std::vector<buffered_note*>> by_color;

    void push(uint32_t color_id, buffered_note* ptr) {
        by_color[color_id].push_back(ptr);
    }

    buffered_note* find_and_remove(uint32_t color_id) {
        auto it = by_color.find(color_id);
        if (it == by_color.end() || it->second.empty())
            return nullptr;
        buffered_note* result = it->second.back();
        it->second.pop_back();
        return result;
    }
};
```

**Expected improvement: O(1) vs O(n) per note-off**

---

## 4. Reduce Per-Note Memory Footprint

```cpp
// Current: 24 bytes per note (with padding)
struct buffered_note {
    uint64_t start_time_us;   // 8 bytes
    uint64_t end_time_us;     // 8 bytes
    uint32_t color_id;        // 4 bytes + 4 padding
};

// Optimized: 12-16 bytes
struct buffered_note {
    uint32_t start_time_ms;   // 4 bytes - milliseconds sufficient for display
    uint32_t end_time_ms;     // 4 bytes - ~49 days max, plenty for any MIDI
    uint16_t color_id;        // 2 bytes - 65k track/channel combos
    uint8_t key;              // 1 byte  - move key into note struct
    uint8_t flags;            // 1 byte  - ended flag, etc.
};  // 12 bytes total, no padding
```

With key stored in the note, can use flat storage instead of per-key queues (see below).

For 10 million notes: 240MB -> 120MB (current -> optimized)

**Expected improvement: 50% memory reduction, better cache utilization**

---

## 5. Flat Note Storage with Binary Search

128 separate queues have overhead and poor cache locality. A single flat buffer sorted by time is more cache-friendly:

```cpp
struct flat_visuals {
    // Single contiguous buffer, sorted by start_time
    std::vector<buffered_note> notes;  // key stored in each note

    // Binary search to find visible window
    auto find_visible_range(uint64_t window_start, uint64_t window_end) {
        auto begin = std::lower_bound(notes.begin(), notes.end(), window_start,
            [](const buffered_note& n, uint64_t t) { return n.start_time < t; });
        auto end = std::upper_bound(begin, notes.end(), window_end,
            [](uint64_t t, const buffered_note& n) { return t < n.end_time; });
        return std::make_pair(begin, end);
    }

    // Render all visible notes in one pass
    void render(uint64_t current_time, uint64_t scroll_window) {
        auto [begin, end] = find_visible_range(
            current_time - scroll_window,  // notes that haven't fully passed
            current_time + scroll_window   // notes that have appeared
        );

        for (auto it = begin; it != end; ++it) {
            // Batch into vertex buffer by key for rendering
        }
    }
};
```

**Expected improvement: Better cache locality, O(log n) to find visible range**

---

## 6. Culling Optimization - Track Active Keys

Skip iteration over keys that have no visible notes:

```cpp
struct visuals_viewport {
    std::bitset<128> active_keys;  // Keys with pending/visible notes

    void push_note_on(uint8_t key, ...) {
        active_keys.set(key);
        // ... existing logic
    }

    void cull_and_render() {
        for (size_t key = 0; key < 128; ++key) {
            if (!active_keys.test(key))
                continue;  // Skip empty keys entirely

            // ... process key

            if (falling_notes[key].empty() && pending[key].empty())
                active_keys.reset(key);
        }
    }
};
```

**Expected improvement: Significant for sparse sections of black MIDI**

---

## 7. MIDI Output Optimizations

For black MIDI, even KDMAPI can struggle with note throughput.

### 7.1 Coalesce Duplicate Events
```cpp
// Skip redundant note-ons at same tick
std::unordered_set<uint32_t> notes_this_tick;  // key | (channel << 8)
if (!notes_this_tick.insert(key | (channel << 8)).second)
    continue;  // Duplicate, skip
```

### 7.2 Rate Limit Visualization
Render at fixed 60fps even if parsing/playing faster:
```cpp
auto last_render = std::chrono::steady_clock::now();
constexpr auto render_interval = std::chrono::microseconds(16667);  // 60fps

void maybe_render() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_render < render_interval)
        return;
    last_render = now;
    render();
}
```

### 7.3 Note Dropping Strategy
For truly extreme black MIDI, consider intelligent note dropping:
- Drop notes shorter than N ms (inaudible)
- Drop notes when > M notes already playing on same key
- Prioritize bass and melody ranges over mid-range spam

---

## Priority Order for Implementation

| Priority | Optimization | Effort | Impact |
|----------|--------------|--------|--------|
| 1 | Batch rendering | Medium | 10-100x render perf |
| 2 | Double-buffered visuals | Medium | Eliminates lock contention |
| 3 | Hash map for pending | Low | O(1) vs O(n) matching |
| 4 | Flat storage + binary search | High | Better cache, O(log n) lookup |
| 5 | Reduced note size | Low | 50% memory reduction |
| 6 | Active key tracking | Low | Helps sparse sections |
| 7 | Output optimizations | Medium | Prevents synth overload |

---

## Profiling Notes

Before optimizing, profile to identify actual bottlenecks:

```cpp
// Simple timing
auto start = std::chrono::high_resolution_clock::now();
// ... code to measure
auto elapsed = std::chrono::high_resolution_clock::now() - start;
auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
```

Key metrics to track:
- Frame time (should be < 16ms for 60fps)
- Lock wait time
- Notes rendered per frame
- Memory usage
- Parse throughput (notes/second)
