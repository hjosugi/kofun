#include "kofun_tui.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static KofunTuiCapabilities capabilities(KofunTuiColorLevel color, bool unicode) {
    return (KofunTuiCapabilities){true, unicode, color};
}

static void test_display_width(void) {
    assert(kofun_tui_display_width("ASCII") == 5);
    assert(kofun_tui_display_width("東京") == 4);
    assert(kofun_tui_display_width("e\xCC\x81") == 1);
    assert(kofun_tui_display_width("\xCC\x81") == 0);
    assert(kofun_tui_display_width("🙂") == 2);
    assert(kofun_tui_display_width("👨‍👩‍👧‍👦") == 2);
    assert(kofun_tui_display_width("🇯🇵") == 2);
    assert(kofun_tui_display_width("♥️") == 2);
    assert(kofun_tui_display_width("עברית") == 5);
    assert(kofun_tui_display_width("\xE2\x81\xA8עברית\xE2\x81\xA9") == 5);
    assert(kofun_tui_display_width_n("\xff", 1) == 1);
    assert(kofun_tui_contains_rtl_n("abc עברית", strlen("abc עברית")));
    assert(!kofun_tui_contains_rtl_n("東京", strlen("東京")));
}

static void test_capabilities(void) {
    KofunTuiProbe probe = {
        .is_tty = true,
        .term = "xterm-256color",
        .colorterm = "truecolor",
        .locale = "ja_JP.UTF-8",
        .colors = -1,
    };
    KofunTuiCapabilities result = kofun_tui_probe(probe);
    assert(result.interactive && result.unicode && result.color == KOFUN_TUI_COLOR_TRUE);
    probe.colorterm = NULL;
    result = kofun_tui_probe(probe);
    assert(result.interactive && result.color == KOFUN_TUI_COLOR_256);
    probe.term = "xterm";
    probe.colors = 16;
    result = kofun_tui_probe(probe);
    assert(result.interactive && result.color == KOFUN_TUI_COLOR_16);
    probe.colors = 0;
    result = kofun_tui_probe(probe);
    assert(result.interactive && result.color == KOFUN_TUI_COLOR_NONE);
    probe.no_color = true;
    result = kofun_tui_probe(probe);
    assert(!result.interactive && result.color == KOFUN_TUI_COLOR_NONE);
    probe.no_color = false;
    probe.ci = true;
    result = kofun_tui_probe(probe);
    assert(!result.interactive);
    probe.ci = false;
    probe.term = "dumb";
    result = kofun_tui_probe(probe);
    assert(!result.interactive);
    probe.term = "xterm";
    probe.is_tty = false;
    result = kofun_tui_probe(probe);
    assert(!result.interactive);
    probe.is_tty = true;
    probe.no_tui = true;
    result = kofun_tui_probe(probe);
    assert(!result.interactive);
}

static void test_progress_and_color(void) {
    char storage[2048];
    KofunTuiBuffer buffer;
    KofunTuiRenderStats stats;
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    stats = kofun_tui_render_progress(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, true),
        (KofunTuiViewport){40, 1}, (KofunTuiProgress){"compile 東京", 1, 2, 3});
    assert(stats.lines == 1);
    assert(stats.cells == 40);
    assert(strstr(storage, "東京") != NULL);
    assert(strchr(storage, '\x1b') == NULL);

    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    stats = kofun_tui_render_progress(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, false),
        (KofunTuiViewport){3, 1}, (KofunTuiProgress){"compile", 1, 2, 0});
    assert(stats.cells <= 3);

    for (int color = KOFUN_TUI_COLOR_16; color <= KOFUN_TUI_COLOR_TRUE; ++color) {
        kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
        kofun_tui_render_progress(&buffer, capabilities((KofunTuiColorLevel)color, false),
            (KofunTuiViewport){40, 1}, (KofunTuiProgress){"compile", 1, 2, 1});
        assert(strchr(storage, '\x1b') != NULL);
    }
    assert(fabs(kofun_tui_ease_cubic(0.0, 1.0, 0.5, 1.0) - 0.875) < 0.000001);
}

static void test_responsive_components(void) {
    const char *headers[] = {"Name", "מצב"};
    const char *cells[] = {"東京", "מוכן"};
    KofunTuiTable table = {headers, cells, 2, 1};
    KofunTuiTreeNode nodes[] = {
        {"root", 0, true, true}, {"子", 1, false, true}, {"leaf", 2, true, true},
    };
    KofunTuiLogEntry logs[] = {
        {1, KOFUN_TUI_LOG_INFO, "started"},
        {2, KOFUN_TUI_LOG_WARN, "slow"},
        {3, KOFUN_TUI_LOG_ERROR, "עברית"},
    };
    char wide[4096];
    char narrow[4096];
    KofunTuiBuffer buffer;

    kofun_tui_buffer_init(&buffer, wide, sizeof(wide));
    kofun_tui_render_table(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, true),
        (KofunTuiViewport){48, 8}, table);
    assert(strstr(wide, " | ") != NULL);
    assert(strstr(wide, "\xE2\x81\xA8") != NULL);
    assert(strstr(wide, "\xE2\x81\xA9") != NULL);

    kofun_tui_buffer_init(&buffer, narrow, sizeof(narrow));
    kofun_tui_render_table(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, true),
        (KofunTuiViewport){20, 8}, table);
    assert(strstr(narrow, " | ") == NULL);
    assert(strstr(narrow, ": ") != NULL);
    assert(strcmp(wide, narrow) != 0);

    kofun_tui_buffer_init(&buffer, wide, sizeof(wide));
    kofun_tui_render_tree(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, true),
        (KofunTuiViewport){20, 2}, (KofunTuiTree){nodes, 3});
    assert(buffer.stats.lines == 2);
    assert(buffer.stats.truncated);
    assert(strstr(wide, "├─") != NULL);

    kofun_tui_buffer_init(&buffer, wide, sizeof(wide));
    kofun_tui_render_log(&buffer, capabilities(KOFUN_TUI_COLOR_256, true),
        (KofunTuiViewport){40, 2}, logs, 3);
    assert(buffer.stats.lines == 2);
    assert(buffer.stats.truncated);
    assert(strstr(wide, "[000002]") != NULL);
    assert(strstr(wide, "\x1b[38;5;214m") != NULL);
    assert(strstr(wide, "\xE2\x81\xA8") != NULL);
}

static void test_bounded_rendering(void) {
    KofunTuiTreeNode nodes[KOFUN_TUI_MAX_ROWS + 20U];
    char storage[256];
    KofunTuiBuffer buffer;
    for (size_t index = 0; index < KOFUN_TUI_MAX_ROWS + 20U; ++index) {
        nodes[index] = (KofunTuiTreeNode){"bounded", index, true, true};
    }
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    kofun_tui_render_tree(&buffer, capabilities(KOFUN_TUI_COLOR_NONE, false),
        (KofunTuiViewport){512, 256},
        (KofunTuiTree){nodes, KOFUN_TUI_MAX_ROWS + 20U});
    assert(buffer.length < sizeof(storage));
    assert(buffer.stats.truncated);
    assert(buffer.stats.lines <= KOFUN_TUI_MAX_VIEWPORT_ROWS);
}

int main(void) {
    test_display_width();
    test_capabilities();
    test_progress_and_color();
    test_responsive_components();
    test_bounded_rendering();
    puts("PASS: TUI Unicode, degradation, responsive components, and bounds");
    return 0;
}
