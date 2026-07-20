#ifndef KOFUN_TUI_H
#define KOFUN_TUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOFUN_TUI_MAX_COLUMNS 16U
#define KOFUN_TUI_MAX_ROWS 256U
#define KOFUN_TUI_MAX_TREE_DEPTH 32U
#define KOFUN_TUI_MAX_TEXT_BYTES 4096U
#define KOFUN_TUI_MAX_VIEWPORT_COLUMNS 512U
#define KOFUN_TUI_MAX_VIEWPORT_ROWS 256U

typedef enum KofunTuiColorLevel {
    KOFUN_TUI_COLOR_NONE = 0,
    KOFUN_TUI_COLOR_16 = 1,
    KOFUN_TUI_COLOR_256 = 2,
    KOFUN_TUI_COLOR_TRUE = 3,
} KofunTuiColorLevel;

typedef struct KofunTuiProbe {
    bool is_tty;
    bool no_tui;
    bool no_color;
    bool ci;
    const char *term;
    const char *colorterm;
    const char *locale;
    long colors;
} KofunTuiProbe;

typedef struct KofunTuiCapabilities {
    bool interactive;
    bool unicode;
    KofunTuiColorLevel color;
} KofunTuiCapabilities;

typedef struct KofunTuiViewport {
    size_t columns;
    size_t rows;
} KofunTuiViewport;

typedef struct KofunTuiRenderStats {
    size_t bytes;
    size_t lines;
    size_t scalars;
    size_t cells;
    bool truncated;
} KofunTuiRenderStats;

typedef struct KofunTuiBuffer {
    char *data;
    size_t capacity;
    size_t length;
    KofunTuiRenderStats stats;
} KofunTuiBuffer;

typedef struct KofunTuiProgress {
    const char *label;
    uint64_t completed;
    uint64_t total;
    unsigned phase;
} KofunTuiProgress;

typedef struct KofunTuiTable {
    const char *const *headers;
    const char *const *cells;
    size_t columns;
    size_t rows;
} KofunTuiTable;

typedef struct KofunTuiTreeNode {
    const char *label;
    size_t depth;
    bool last;
    bool expanded;
} KofunTuiTreeNode;

typedef struct KofunTuiTree {
    const KofunTuiTreeNode *nodes;
    size_t count;
} KofunTuiTree;

typedef enum KofunTuiLogLevel {
    KOFUN_TUI_LOG_TRACE = 0,
    KOFUN_TUI_LOG_INFO = 1,
    KOFUN_TUI_LOG_WARN = 2,
    KOFUN_TUI_LOG_ERROR = 3,
} KofunTuiLogLevel;

typedef struct KofunTuiLogEntry {
    uint64_t sequence;
    KofunTuiLogLevel level;
    const char *message;
} KofunTuiLogEntry;

KofunTuiCapabilities kofun_tui_probe(KofunTuiProbe probe);
size_t kofun_tui_display_width_n(const char *text, size_t length);
size_t kofun_tui_display_width(const char *text);
bool kofun_tui_contains_rtl_n(const char *text, size_t length);
double kofun_tui_ease_cubic(double from, double to, double elapsed, double duration);

void kofun_tui_buffer_init(KofunTuiBuffer *buffer, char *storage, size_t capacity);
KofunTuiRenderStats kofun_tui_render_progress(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiProgress progress
);
KofunTuiRenderStats kofun_tui_render_table(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiTable table
);
KofunTuiRenderStats kofun_tui_render_tree(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiTree tree
);
KofunTuiRenderStats kofun_tui_render_log(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    const KofunTuiLogEntry *entries,
    size_t count
);

/*
 * Kofun's current C ABI can pass only scalar handles and C strings. These
 * wrappers expose the same components without leaking the C aggregate API.
 * A session owns no application data and is single-thread confined.
 */
long kofun_tui_begin(int no_tui);
int kofun_tui_end(long handle);
int kofun_tui_set_size(long handle, long columns, long rows);
int kofun_tui_progress(
    long handle,
    const char *label,
    long completed,
    long total,
    long phase
);
int kofun_tui_table2(
    long handle,
    const char *left_heading,
    const char *right_heading,
    const char *left_value,
    const char *right_value
);
int kofun_tui_tree_item(
    long handle,
    const char *label,
    long depth,
    int last,
    int expanded
);
int kofun_tui_log(long handle, long sequence, long level, const char *message);

#ifdef __cplusplus
}
#endif

#endif
