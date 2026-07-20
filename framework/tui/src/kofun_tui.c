#define _POSIX_C_SOURCE 200809L

#include "kofun_tui.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct Scalar {
    uint32_t value;
    size_t bytes;
} Scalar;

typedef struct Cluster {
    size_t end;
    size_t width;
    size_t scalars;
    bool rtl;
} Cluster;

typedef struct KofunTuiSession {
    int descriptor;
    KofunTuiCapabilities capabilities;
    KofunTuiViewport viewport;
    size_t previous_lines;
    bool fixed_size;
    bool signal_registered;
} KofunTuiSession;

static volatile sig_atomic_t resize_pending = 0;
static struct sigaction previous_winch;
static unsigned signal_users = 0;

static size_t minimum_size(size_t left, size_t right) {
    return left < right ? left : right;
}

static bool text_contains(const char *text, const char *needle) {
    return text != NULL && strstr(text, needle) != NULL;
}

static bool text_equal_case(const char *left, const char *right) {
    return left != NULL && right != NULL && strcasecmp(left, right) == 0;
}

static size_t bounded_length(const char *text, bool *truncated) {
    size_t length = 0;
    if (text == NULL) return 0;
    while (length < KOFUN_TUI_MAX_TEXT_BYTES && text[length] != '\0') {
        ++length;
    }
    if (length == KOFUN_TUI_MAX_TEXT_BYTES) *truncated = true;
    return length;
}

static Scalar decode_utf8(const char *text, size_t length, size_t offset) {
    const unsigned char *bytes = (const unsigned char *)text;
    unsigned char first;
    Scalar invalid = {0xfffdU, 1};
    Scalar result;
    if (offset >= length) return (Scalar){0, 0};
    first = bytes[offset];
    if (first < 0x80U) return (Scalar){first, 1};
    if (first >= 0xc2U && first <= 0xdfU && offset + 1 < length &&
        (bytes[offset + 1] & 0xc0U) == 0x80U) {
        result.value = ((uint32_t)(first & 0x1fU) << 6) |
            (uint32_t)(bytes[offset + 1] & 0x3fU);
        result.bytes = 2;
        return result;
    }
    if (first >= 0xe0U && first <= 0xefU && offset + 2 < length &&
        (bytes[offset + 1] & 0xc0U) == 0x80U &&
        (bytes[offset + 2] & 0xc0U) == 0x80U &&
        !(first == 0xe0U && bytes[offset + 1] < 0xa0U) &&
        !(first == 0xedU && bytes[offset + 1] >= 0xa0U)) {
        result.value = ((uint32_t)(first & 0x0fU) << 12) |
            ((uint32_t)(bytes[offset + 1] & 0x3fU) << 6) |
            (uint32_t)(bytes[offset + 2] & 0x3fU);
        result.bytes = 3;
        return result;
    }
    if (first >= 0xf0U && first <= 0xf4U && offset + 3 < length &&
        (bytes[offset + 1] & 0xc0U) == 0x80U &&
        (bytes[offset + 2] & 0xc0U) == 0x80U &&
        (bytes[offset + 3] & 0xc0U) == 0x80U &&
        !(first == 0xf0U && bytes[offset + 1] < 0x90U) &&
        !(first == 0xf4U && bytes[offset + 1] >= 0x90U)) {
        result.value = ((uint32_t)(first & 0x07U) << 18) |
            ((uint32_t)(bytes[offset + 1] & 0x3fU) << 12) |
            ((uint32_t)(bytes[offset + 2] & 0x3fU) << 6) |
            (uint32_t)(bytes[offset + 3] & 0x3fU);
        result.bytes = 4;
        return result;
    }
    return invalid;
}

static bool in_range(uint32_t value, uint32_t first, uint32_t last) {
    return value >= first && value <= last;
}

static bool scalar_is_zero_width(uint32_t value) {
    return value == 0x200cU || value == 0x200dU || value == 0x20e3U ||
        value == 0xfeffU || in_range(value, 0, 0x1fU) ||
        in_range(value, 0x7fU, 0x9fU) || in_range(value, 0x300U, 0x36fU) ||
        in_range(value, 0x483U, 0x489U) || in_range(value, 0x591U, 0x5bdU) ||
        value == 0x5bfU || in_range(value, 0x5c1U, 0x5c2U) ||
        in_range(value, 0x5c4U, 0x5c5U) || value == 0x5c7U ||
        in_range(value, 0x610U, 0x61aU) || in_range(value, 0x64bU, 0x65fU) ||
        value == 0x670U || in_range(value, 0x6d6U, 0x6edU) ||
        in_range(value, 0x711U, 0x711U) || in_range(value, 0x730U, 0x74aU) ||
        in_range(value, 0x7a6U, 0x7b0U) || in_range(value, 0x7ebU, 0x7f3U) ||
        in_range(value, 0x816U, 0x819U) || in_range(value, 0x81bU, 0x823U) ||
        in_range(value, 0x825U, 0x827U) || in_range(value, 0x829U, 0x82dU) ||
        in_range(value, 0x859U, 0x85bU) || in_range(value, 0x8d3U, 0x902U) ||
        in_range(value, 0x93aU, 0x93cU) || value == 0x94dU ||
        in_range(value, 0x951U, 0x957U) || in_range(value, 0x962U, 0x963U) ||
        in_range(value, 0x981U, 0x981U) || value == 0x9bcU || value == 0x9cdU ||
        in_range(value, 0xa01U, 0xa02U) || value == 0xa3cU || value == 0xa4dU ||
        in_range(value, 0xa70U, 0xa71U) || in_range(value, 0xa81U, 0xa82U) ||
        value == 0xabcU || value == 0xacdU || in_range(value, 0xb01U, 0xb01U) ||
        value == 0xb3cU || value == 0xb4dU || in_range(value, 0xc00U, 0xc04U) ||
        value == 0xc4dU || in_range(value, 0xd00U, 0xd01U) || value == 0xd4dU ||
        in_range(value, 0xe31U, 0xe31U) || in_range(value, 0xe34U, 0xe3aU) ||
        in_range(value, 0xe47U, 0xe4eU) || value == 0xeb1U ||
        in_range(value, 0xeb4U, 0xebcU) || in_range(value, 0xec8U, 0xecdU) ||
        in_range(value, 0xf18U, 0xf19U) || value == 0xf35U || value == 0xf37U ||
        value == 0xf39U || in_range(value, 0xf71U, 0xf7eU) ||
        in_range(value, 0xf80U, 0xf84U) || in_range(value, 0xf86U, 0xf87U) ||
        in_range(value, 0xfc6U, 0xfc6U) || in_range(value, 0x102dU, 0x1030U) ||
        in_range(value, 0x1032U, 0x1037U) || in_range(value, 0x1039U, 0x103aU) ||
        in_range(value, 0x1058U, 0x1059U) || in_range(value, 0x1160U, 0x11ffU) ||
        in_range(value, 0x135dU, 0x135fU) || in_range(value, 0x1712U, 0x1714U) ||
        in_range(value, 0x1732U, 0x1734U) || in_range(value, 0x1752U, 0x1753U) ||
        in_range(value, 0x1772U, 0x1773U) || in_range(value, 0x17b4U, 0x17b5U) ||
        in_range(value, 0x17b7U, 0x17bdU) || value == 0x17c6U ||
        in_range(value, 0x17c9U, 0x17d3U) || value == 0x17ddU ||
        in_range(value, 0x180bU, 0x180fU) || in_range(value, 0x1885U, 0x1886U) ||
        value == 0x18a9U || in_range(value, 0x1920U, 0x1922U) ||
        in_range(value, 0x1927U, 0x1928U) || in_range(value, 0x1932U, 0x1932U) ||
        in_range(value, 0x1939U, 0x193bU) || in_range(value, 0x1a17U, 0x1a18U) ||
        value == 0x1a1bU || value == 0x1a56U || in_range(value, 0x1a58U, 0x1a5eU) ||
        value == 0x1a60U || value == 0x1a62U || in_range(value, 0x1a65U, 0x1a6cU) ||
        in_range(value, 0x1a73U, 0x1a7cU) || in_range(value, 0x1ab0U, 0x1affU) ||
        in_range(value, 0x1b00U, 0x1b03U) || value == 0x1b34U ||
        in_range(value, 0x1b36U, 0x1b3aU) || value == 0x1b3cU ||
        value == 0x1b42U || in_range(value, 0x1b6bU, 0x1b73U) ||
        in_range(value, 0x1dc0U, 0x1dffU) || in_range(value, 0x200bU, 0x200fU) ||
        in_range(value, 0x202aU, 0x202eU) || in_range(value, 0x2060U, 0x206fU) ||
        in_range(value, 0x20d0U, 0x20ffU) || in_range(value, 0x2cefU, 0x2cf1U) ||
        in_range(value, 0x2de0U, 0x2dffU) || in_range(value, 0x302aU, 0x302fU) ||
        in_range(value, 0x3099U, 0x309aU) || in_range(value, 0xa66fU, 0xa672U) ||
        in_range(value, 0xa674U, 0xa67dU) || in_range(value, 0xa69eU, 0xa69fU) ||
        in_range(value, 0xa6f0U, 0xa6f1U) || in_range(value, 0xa802U, 0xa802U) ||
        value == 0xa806U || value == 0xa80bU || in_range(value, 0xa825U, 0xa826U) ||
        value == 0xa8c4U || in_range(value, 0xa8e0U, 0xa8f1U) ||
        in_range(value, 0xa926U, 0xa92dU) || in_range(value, 0xa947U, 0xa951U) ||
        in_range(value, 0xa980U, 0xa982U) || value == 0xa9b3U || value == 0xa9bcU ||
        value == 0xa9e5U || in_range(value, 0xaa29U, 0xaa2eU) ||
        in_range(value, 0xaa31U, 0xaa32U) || in_range(value, 0xaa35U, 0xaa36U) ||
        value == 0xaa43U || value == 0xaa4cU || value == 0xaa7cU ||
        value == 0xaab0U || in_range(value, 0xaab2U, 0xaab4U) ||
        in_range(value, 0xaab7U, 0xaab8U) || in_range(value, 0xaabeU, 0xaabfU) ||
        value == 0xaac1U || in_range(value, 0xaaecU, 0xaaedU) || value == 0xaaf6U ||
        in_range(value, 0xabe5U, 0xabe5U) || value == 0xabe8U || value == 0xabedU ||
        in_range(value, 0xfb1eU, 0xfb1eU) || in_range(value, 0xfe00U, 0xfe0fU) ||
        in_range(value, 0xfe20U, 0xfe2fU) || in_range(value, 0x101fdU, 0x101fdU) ||
        in_range(value, 0x102e0U, 0x102e0U) || in_range(value, 0x10376U, 0x1037aU) ||
        in_range(value, 0x10a01U, 0x10a03U) || in_range(value, 0x10a05U, 0x10a06U) ||
        in_range(value, 0x10a0cU, 0x10a0fU) || in_range(value, 0x10a38U, 0x10a3aU) ||
        value == 0x10a3fU || in_range(value, 0x1d167U, 0x1d169U) ||
        in_range(value, 0x1d17bU, 0x1d182U) || in_range(value, 0x1d185U, 0x1d18bU) ||
        in_range(value, 0x1d1aaU, 0x1d1adU) || in_range(value, 0x1d242U, 0x1d244U) ||
        in_range(value, 0x1da00U, 0x1da36U) || in_range(value, 0x1da3bU, 0x1da6cU) ||
        in_range(value, 0x1da75U, 0x1da75U) || in_range(value, 0x1da84U, 0x1da84U) ||
        in_range(value, 0x1da9bU, 0x1da9fU) || in_range(value, 0x1daa1U, 0x1daafU) ||
        in_range(value, 0x1e000U, 0x1e02aU) || in_range(value, 0x1e130U, 0x1e136U) ||
        in_range(value, 0x1e2aeU, 0x1e2aeU) || in_range(value, 0x1e8d0U, 0x1e8d6U) ||
        in_range(value, 0x1f3fbU, 0x1f3ffU) || in_range(value, 0xe0100U, 0xe01efU);
}

static bool scalar_is_wide(uint32_t value) {
    return in_range(value, 0x1100U, 0x115fU) || value == 0x2329U ||
        value == 0x232aU || in_range(value, 0x231aU, 0x231bU) ||
        in_range(value, 0x23e9U, 0x23ecU) || value == 0x23f0U ||
        value == 0x23f3U || in_range(value, 0x25fdU, 0x25feU) ||
        in_range(value, 0x2614U, 0x2615U) || in_range(value, 0x2648U, 0x2653U) ||
        value == 0x267fU || value == 0x2693U || value == 0x26a1U ||
        in_range(value, 0x26aaU, 0x26abU) || in_range(value, 0x26bdU, 0x26beU) ||
        in_range(value, 0x26c4U, 0x26c5U) || value == 0x26ceU || value == 0x26d4U ||
        value == 0x26eaU || in_range(value, 0x26f2U, 0x26f3U) ||
        value == 0x26f5U || value == 0x26faU || value == 0x26fdU ||
        value == 0x2705U || in_range(value, 0x270aU, 0x270bU) ||
        value == 0x2728U || value == 0x274cU || value == 0x274eU ||
        in_range(value, 0x2753U, 0x2755U) || value == 0x2757U ||
        in_range(value, 0x2795U, 0x2797U) || value == 0x27b0U ||
        value == 0x27bfU || in_range(value, 0x2b1bU, 0x2b1cU) ||
        value == 0x2b50U || value == 0x2b55U || in_range(value, 0x2e80U, 0x303eU) ||
        in_range(value, 0x3040U, 0xa4cfU) || in_range(value, 0xac00U, 0xd7a3U) ||
        in_range(value, 0xf900U, 0xfaffU) || in_range(value, 0xfe10U, 0xfe19U) ||
        in_range(value, 0xfe30U, 0xfe6fU) || in_range(value, 0xff01U, 0xff60U) ||
        in_range(value, 0xffe0U, 0xffe6U) || in_range(value, 0x1f000U, 0x1faffU) ||
        in_range(value, 0x20000U, 0x3fffdU);
}

static bool scalar_is_rtl(uint32_t value) {
    return in_range(value, 0x590U, 0x8ffU) || in_range(value, 0xfb1dU, 0xfdffU) ||
        in_range(value, 0xfe70U, 0xfefcU) || in_range(value, 0x10800U, 0x10fffU) ||
        in_range(value, 0x1e800U, 0x1eeffU);
}

static bool scalar_is_regional(uint32_t value) {
    return in_range(value, 0x1f1e6U, 0x1f1ffU);
}

static Cluster next_cluster(const char *text, size_t length, size_t start) {
    size_t cursor = start;
    Scalar scalar = decode_utf8(text, length, cursor);
    Cluster cluster = {start, 0, 0, false};
    bool has_base = false;
    bool force_emoji = false;
    bool regional = false;
    if (scalar.bytes == 0) return cluster;
    cursor += scalar.bytes;
    cluster.scalars = 1;
    cluster.rtl = scalar_is_rtl(scalar.value);
    if (!scalar_is_zero_width(scalar.value)) {
        has_base = true;
        cluster.width = scalar_is_wide(scalar.value) ? 2U : 1U;
        regional = scalar_is_regional(scalar.value);
    }
    while (cursor < length) {
        Scalar following = decode_utf8(text, length, cursor);
        if (regional && scalar_is_regional(following.value)) {
            cursor += following.bytes;
            ++cluster.scalars;
            cluster.width = 2;
            regional = false;
            continue;
        }
        if (following.value == 0xfe0fU || following.value == 0x20e3U) {
            force_emoji = true;
        }
        if (scalar_is_zero_width(following.value) && following.value != 0x200dU) {
            cursor += following.bytes;
            ++cluster.scalars;
            cluster.rtl = cluster.rtl || scalar_is_rtl(following.value);
            continue;
        }
        if (following.value == 0x200dU) {
            Scalar joined;
            cursor += following.bytes;
            ++cluster.scalars;
            if (cursor >= length) break;
            joined = decode_utf8(text, length, cursor);
            cursor += joined.bytes;
            ++cluster.scalars;
            cluster.rtl = cluster.rtl || scalar_is_rtl(joined.value);
            if (!scalar_is_zero_width(joined.value)) {
                has_base = true;
                if (scalar_is_wide(joined.value)) cluster.width = 2;
            }
            force_emoji = true;
            continue;
        }
        break;
    }
    if (has_base && force_emoji) cluster.width = 2;
    cluster.end = cursor;
    return cluster;
}

KofunTuiCapabilities kofun_tui_probe(KofunTuiProbe probe) {
    KofunTuiCapabilities result = {false, false, KOFUN_TUI_COLOR_NONE};
    bool utf8 = text_contains(probe.locale, "UTF-8") ||
        text_contains(probe.locale, "utf8") || text_contains(probe.locale, "UTF8");
    result.unicode = utf8;
    if (!probe.is_tty || probe.no_tui || probe.no_color || probe.ci ||
        probe.term == NULL || probe.term[0] == '\0' || text_equal_case(probe.term, "dumb")) {
        return result;
    }
    result.interactive = true;
    if (probe.colors == 0 || text_contains(probe.term, "mono")) return result;
    if (text_equal_case(probe.colorterm, "truecolor") ||
        text_equal_case(probe.colorterm, "24bit")) {
        result.color = KOFUN_TUI_COLOR_TRUE;
    } else if (probe.colors >= 256 || text_contains(probe.term, "256color")) {
        result.color = KOFUN_TUI_COLOR_256;
    } else {
        result.color = KOFUN_TUI_COLOR_16;
    }
    return result;
}

size_t kofun_tui_display_width_n(const char *text, size_t length) {
    size_t cursor = 0;
    size_t width = 0;
    if (text == NULL) return 0;
    while (cursor < length) {
        Cluster cluster = next_cluster(text, length, cursor);
        if (cluster.end <= cursor) break;
        if (SIZE_MAX - width < cluster.width) return SIZE_MAX;
        width += cluster.width;
        cursor = cluster.end;
    }
    return width;
}

size_t kofun_tui_display_width(const char *text) {
    bool ignored = false;
    size_t length = bounded_length(text, &ignored);
    return kofun_tui_display_width_n(text, length);
}

bool kofun_tui_contains_rtl_n(const char *text, size_t length) {
    size_t cursor = 0;
    if (text == NULL) return false;
    while (cursor < length) {
        Scalar scalar = decode_utf8(text, length, cursor);
        if (scalar.bytes == 0) break;
        if (scalar_is_rtl(scalar.value)) return true;
        cursor += scalar.bytes;
    }
    return false;
}

double kofun_tui_ease_cubic(double from, double to, double elapsed, double duration) {
    double position;
    if (duration <= 0.0 || elapsed >= duration) return to;
    if (elapsed <= 0.0) return from;
    position = elapsed / duration;
    position = 1.0 - (1.0 - position) * (1.0 - position) * (1.0 - position);
    return from + (to - from) * position;
}

void kofun_tui_buffer_init(KofunTuiBuffer *buffer, char *storage, size_t capacity) {
    if (buffer == NULL) return;
    buffer->data = storage;
    buffer->capacity = capacity;
    buffer->length = 0;
    buffer->stats = (KofunTuiRenderStats){0, 0, 0, 0, false};
    if (storage != NULL && capacity > 0) storage[0] = '\0';
}

static void buffer_raw(KofunTuiBuffer *buffer, const char *text, size_t length) {
    size_t available;
    size_t copied;
    if (buffer == NULL || text == NULL || length == 0) return;
    if (buffer->data == NULL || buffer->capacity == 0) {
        buffer->stats.truncated = true;
        return;
    }
    available = buffer->capacity - 1U - minimum_size(buffer->length, buffer->capacity - 1U);
    copied = minimum_size(length, available);
    if (copied > 0) {
        memcpy(buffer->data + buffer->length, text, copied);
        buffer->length += copied;
        buffer->data[buffer->length] = '\0';
    }
    if (copied != length) buffer->stats.truncated = true;
    buffer->stats.bytes = buffer->length;
}

static void buffer_literal(KofunTuiBuffer *buffer, const char *text) {
    buffer_raw(buffer, text, strlen(text));
}

static void buffer_cells(KofunTuiBuffer *buffer, const char *text, size_t cells) {
    buffer_literal(buffer, text);
    buffer->stats.cells += cells;
}

static void buffer_spaces(KofunTuiBuffer *buffer, size_t count) {
    while (count-- > 0) buffer_cells(buffer, " ", 1);
}

static void buffer_newline(KofunTuiBuffer *buffer) {
    buffer_raw(buffer, "\n", 1);
    ++buffer->stats.lines;
}

static KofunTuiViewport normalized_viewport(KofunTuiViewport viewport) {
    if (viewport.columns == 0) viewport.columns = 80;
    if (viewport.rows == 0) viewport.rows = 24;
    viewport.columns = minimum_size(viewport.columns, KOFUN_TUI_MAX_VIEWPORT_COLUMNS);
    viewport.rows = minimum_size(viewport.rows, KOFUN_TUI_MAX_VIEWPORT_ROWS);
    return viewport;
}

static size_t fitted_text(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    const char *text,
    size_t columns
) {
    bool source_truncated = false;
    size_t length = bounded_length(text, &source_truncated);
    size_t cursor = 0;
    size_t used = 0;
    size_t full_width = kofun_tui_display_width_n(text, length);
    bool clip = full_width > columns || source_truncated;
    size_t budget = clip && columns > 0 ? columns - 1U : columns;
    bool rtl = capabilities.unicode && kofun_tui_contains_rtl_n(text, length);
    if (rtl) buffer_literal(buffer, "\xE2\x81\xA8");
    while (cursor < length) {
        Cluster cluster = next_cluster(text, length, cursor);
        if (cluster.end <= cursor || used + cluster.width > budget) break;
        buffer_raw(buffer, text + cursor, cluster.end - cursor);
        buffer->stats.scalars += cluster.scalars;
        buffer->stats.cells += cluster.width;
        used += cluster.width;
        cursor = cluster.end;
    }
    if (clip && columns > 0) {
        buffer_cells(buffer, capabilities.unicode ? "\xE2\x80\xA6" : ".", 1);
        ++used;
        buffer->stats.truncated = true;
    }
    if (rtl) buffer_literal(buffer, "\xE2\x81\xA9");
    return used;
}

static void style_begin(
    KofunTuiBuffer *buffer,
    KofunTuiColorLevel color,
    KofunTuiLogLevel level
) {
    static const char *const true_styles[] = {
        "\x1b[38;2;128;128;128m", "\x1b[38;2;80;170;255m",
        "\x1b[38;2;255;190;64m", "\x1b[38;2;255;90;90m"
    };
    static const char *const styles_256[] = {
        "\x1b[38;5;244m", "\x1b[38;5;39m", "\x1b[38;5;214m", "\x1b[38;5;196m"
    };
    static const char *const styles_16[] = {
        "\x1b[90m", "\x1b[34m", "\x1b[33m", "\x1b[31m"
    };
    size_t index = minimum_size((size_t)level, 3U);
    if (color == KOFUN_TUI_COLOR_TRUE) buffer_literal(buffer, true_styles[index]);
    if (color == KOFUN_TUI_COLOR_256) buffer_literal(buffer, styles_256[index]);
    if (color == KOFUN_TUI_COLOR_16) buffer_literal(buffer, styles_16[index]);
}

static void style_end(KofunTuiBuffer *buffer, KofunTuiColorLevel color) {
    if (color != KOFUN_TUI_COLOR_NONE) buffer_literal(buffer, "\x1b[0m");
}

KofunTuiRenderStats kofun_tui_render_progress(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiProgress progress
) {
    static const char *const unicode_spinner[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87", "\xE2\xA0\x8F"};
    static const char ascii_spinner[] = "|/-\\";
    char percent[16];
    char spin[2] = {'\0', '\0'};
    size_t percent_width;
    size_t label_budget;
    size_t bar_width;
    size_t label_used;
    size_t filled;
    uint64_t ratio;
    viewport = normalized_viewport(viewport);
    if (progress.total == 0) {
        ratio = 0;
    } else if (progress.completed >= progress.total) {
        ratio = 100;
    } else {
        ratio = (uint64_t)((long double)progress.completed * 100.0L /
            (long double)progress.total);
    }
    (void)snprintf(percent, sizeof(percent), "%3llu%%", (unsigned long long)ratio);
    percent_width = strlen(percent);
    if (capabilities.unicode) {
        buffer_cells(buffer, unicode_spinner[progress.phase % 10U], 1);
    } else {
        spin[0] = ascii_spinner[progress.phase % 4U];
        buffer_cells(buffer, spin, 1);
    }
    if (viewport.columns <= 1) {
        buffer_newline(buffer);
        return buffer->stats;
    }
    buffer_cells(buffer, " ", 1);
    if (viewport.columns < 20) {
        size_t remaining = viewport.columns - 2U;
        if (remaining > percent_width + 1U) {
            size_t budget = remaining - percent_width - 1U;
            fitted_text(buffer, capabilities, progress.label, budget);
            buffer_cells(buffer, " ", 1);
            buffer_cells(buffer, percent, percent_width);
        } else {
            fitted_text(buffer, capabilities, percent, remaining);
        }
        buffer_newline(buffer);
        return buffer->stats;
    }
    label_budget = minimum_size(viewport.columns / 3U, 24U);
    bar_width = viewport.columns - 2U - label_budget - percent_width - 4U;
    label_used = fitted_text(buffer, capabilities, progress.label, label_budget);
    buffer_spaces(buffer, label_budget - minimum_size(label_used, label_budget));
    buffer_cells(buffer, " [", 2);
    filled = (size_t)(ratio * bar_width / 100U);
    style_begin(buffer, capabilities.color, KOFUN_TUI_LOG_INFO);
    for (size_t index = 0; index < bar_width; ++index) {
        if (capabilities.unicode) {
            buffer_cells(buffer, index < filled ? "\xE2\x96\x88" : "\xE2\x96\x91", 1);
        } else {
            buffer_cells(buffer, index < filled ? "#" : "-", 1);
        }
    }
    style_end(buffer, capabilities.color);
    buffer_cells(buffer, "] ", 2);
    buffer_cells(buffer, percent, percent_width);
    buffer_newline(buffer);
    return buffer->stats;
}

static void table_heading(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    const char *text,
    size_t width
) {
    size_t used;
    style_begin(buffer, capabilities.color, KOFUN_TUI_LOG_INFO);
    used = fitted_text(buffer, capabilities, text, width);
    style_end(buffer, capabilities.color);
    buffer_spaces(buffer, width - minimum_size(used, width));
}

KofunTuiRenderStats kofun_tui_render_table(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiTable table
) {
    size_t columns = minimum_size(table.columns, KOFUN_TUI_MAX_COLUMNS);
    size_t rows = minimum_size(table.rows, KOFUN_TUI_MAX_ROWS);
    size_t emitted_lines = 0;
    viewport = normalized_viewport(viewport);
    if (columns == 0 || table.headers == NULL || table.cells == NULL) return buffer->stats;
    if (viewport.columns < 30 || viewport.columns < columns * 5U) {
        for (size_t row = 0; row < rows && emitted_lines < viewport.rows; ++row) {
            for (size_t column = 0; column < columns && emitted_lines < viewport.rows; ++column) {
                size_t heading_width = minimum_size(kofun_tui_display_width(table.headers[column]), viewport.columns / 2U);
                table_heading(buffer, capabilities, table.headers[column], heading_width);
                if (heading_width + 2U < viewport.columns) {
                    buffer_cells(buffer, ": ", 2);
                    fitted_text(buffer, capabilities, table.cells[row * columns + column], viewport.columns - heading_width - 2U);
                }
                buffer_newline(buffer);
                ++emitted_lines;
            }
        }
        if (rows * columns > emitted_lines) buffer->stats.truncated = true;
        return buffer->stats;
    }
    {
        size_t separators = (columns - 1U) * 3U;
        size_t base = (viewport.columns - separators) / columns;
        size_t remainder = (viewport.columns - separators) % columns;
        for (size_t column = 0; column < columns; ++column) {
            size_t width = base + (column < remainder ? 1U : 0U);
            table_heading(buffer, capabilities, table.headers[column], width);
            if (column + 1U < columns) buffer_cells(buffer, " | ", 3);
        }
        buffer_newline(buffer);
        ++emitted_lines;
        if (emitted_lines < viewport.rows) {
            for (size_t column = 0; column < columns; ++column) {
                size_t width = base + (column < remainder ? 1U : 0U);
                for (size_t index = 0; index < width; ++index) buffer_cells(buffer, "-", 1);
                if (column + 1U < columns) buffer_cells(buffer, "-+-", 3);
            }
            buffer_newline(buffer);
            ++emitted_lines;
        }
        for (size_t row = 0; row < rows && emitted_lines < viewport.rows; ++row) {
            for (size_t column = 0; column < columns; ++column) {
                size_t width = base + (column < remainder ? 1U : 0U);
                size_t used = fitted_text(buffer, capabilities, table.cells[row * columns + column], width);
                buffer_spaces(buffer, width - minimum_size(used, width));
                if (column + 1U < columns) buffer_cells(buffer, " | ", 3);
            }
            buffer_newline(buffer);
            ++emitted_lines;
        }
    }
    if (rows + 2U > emitted_lines) buffer->stats.truncated = true;
    return buffer->stats;
}

KofunTuiRenderStats kofun_tui_render_tree(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    KofunTuiTree tree
) {
    size_t count = minimum_size(tree.count, KOFUN_TUI_MAX_ROWS);
    viewport = normalized_viewport(viewport);
    count = minimum_size(count, viewport.rows);
    for (size_t index = 0; index < count; ++index) {
        KofunTuiTreeNode node = tree.nodes[index];
        size_t depth = minimum_size(node.depth, KOFUN_TUI_MAX_TREE_DEPTH);
        size_t prefix = 0;
        size_t suffix;
        for (size_t level = 0; level < depth && level * 2U + 2U <= viewport.columns; ++level) {
            buffer_cells(buffer, capabilities.unicode ? "\xE2\x94\x82 " : "| ", 2);
            prefix += 2U;
        }
        if (prefix + 2U <= viewport.columns) {
            if (capabilities.unicode) buffer_cells(buffer, node.last ? "\xE2\x94\x94\xE2\x94\x80" : "\xE2\x94\x9C\xE2\x94\x80", 2);
            else buffer_cells(buffer, node.last ? "`-" : "+-", 2);
            prefix += 2U;
        }
        suffix = !node.expanded && viewport.columns >= prefix + 2U ? 2U : 0U;
        fitted_text(buffer, capabilities, node.label, viewport.columns - prefix - suffix);
        if (suffix > 0) buffer_cells(buffer, capabilities.unicode ? " \xE2\x80\xA6" : " .", 2);
        buffer_newline(buffer);
    }
    if (tree.count > count) buffer->stats.truncated = true;
    return buffer->stats;
}

static const char *log_name(KofunTuiLogLevel level) {
    switch (level) {
        case KOFUN_TUI_LOG_TRACE: return "TRACE";
        case KOFUN_TUI_LOG_INFO: return "INFO ";
        case KOFUN_TUI_LOG_WARN: return "WARN ";
        case KOFUN_TUI_LOG_ERROR: return "ERROR";
    }
    return "INFO ";
}

KofunTuiRenderStats kofun_tui_render_log(
    KofunTuiBuffer *buffer,
    KofunTuiCapabilities capabilities,
    KofunTuiViewport viewport,
    const KofunTuiLogEntry *entries,
    size_t count
) {
    size_t start = 0;
    viewport = normalized_viewport(viewport);
    count = minimum_size(count, KOFUN_TUI_MAX_ROWS);
    if (count > viewport.rows) start = count - viewport.rows;
    for (size_t index = start; index < count; ++index) {
        char sequence[24];
        size_t prefix = 6;
        KofunTuiLogLevel level = entries[index].level;
        if (level < KOFUN_TUI_LOG_TRACE || level > KOFUN_TUI_LOG_ERROR) level = KOFUN_TUI_LOG_INFO;
        if (viewport.columns <= 5U) {
            fitted_text(buffer, capabilities, log_name(level), viewport.columns);
            buffer_newline(buffer);
            continue;
        }
        if (viewport.columns >= 32) {
            (void)snprintf(sequence, sizeof(sequence), "[%06llu] ", (unsigned long long)(entries[index].sequence % 1000000U));
            buffer_cells(buffer, sequence, strlen(sequence));
            prefix += strlen(sequence);
        }
        style_begin(buffer, capabilities.color, level);
        buffer_cells(buffer, log_name(level), 5);
        style_end(buffer, capabilities.color);
        buffer_cells(buffer, " ", 1);
        if (prefix < viewport.columns) fitted_text(buffer, capabilities, entries[index].message, viewport.columns - prefix);
        buffer_newline(buffer);
    }
    if (start > 0) buffer->stats.truncated = true;
    return buffer->stats;
}

static void winch_handler(int signal_number) {
    (void)signal_number;
    resize_pending = 1;
}

static bool register_winch(void) {
    struct sigaction action;
    if (signal_users++ > 0) return true;
    memset(&action, 0, sizeof(action));
    action.sa_handler = winch_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGWINCH, &action, &previous_winch) != 0) {
        signal_users = 0;
        return false;
    }
    return true;
}

static void unregister_winch(void) {
    if (signal_users == 0 || --signal_users > 0) return;
    (void)sigaction(SIGWINCH, &previous_winch, NULL);
}

static void session_terminal_size(KofunTuiSession *session) {
    struct winsize size;
    if (session->fixed_size) return;
    if (ioctl(session->descriptor, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col > 0) session->viewport.columns = size.ws_col;
        if (size.ws_row > 0) session->viewport.rows = size.ws_row;
    }
}

static int write_all(int descriptor, const char *data, size_t length) {
    while (length > 0) {
        ssize_t written = write(descriptor, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -1;
        data += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

static int session_emit(KofunTuiSession *session, KofunTuiBuffer *buffer) {
    char control[64];
    int length;
    if (session == NULL || buffer == NULL) return -1;
    if (resize_pending) {
        resize_pending = 0;
        session_terminal_size(session);
    }
    if (!session->capabilities.interactive) return write_all(session->descriptor, buffer->data, buffer->length);
    if (session->previous_lines > 0) {
        length = snprintf(control, sizeof(control), "\x1b[%zuF", session->previous_lines);
        if (length < 0 || write_all(session->descriptor, control, (size_t)length) != 0) return -1;
        for (size_t line = 0; line < session->previous_lines; ++line) {
            if (write_all(session->descriptor, "\x1b[2K", 4) != 0) return -1;
            if (line + 1U < session->previous_lines && write_all(session->descriptor, "\x1b[1E", 4) != 0) return -1;
        }
        if (session->previous_lines > 1U) {
            length = snprintf(control, sizeof(control), "\x1b[%zuF", session->previous_lines - 1U);
            if (length < 0 || write_all(session->descriptor, control, (size_t)length) != 0) return -1;
        }
    }
    if (write_all(session->descriptor, buffer->data, buffer->length) != 0) return -1;
    session->previous_lines = buffer->stats.lines;
    return 0;
}

long kofun_tui_begin(int no_tui) {
    KofunTuiSession *session = calloc(1, sizeof(*session));
    KofunTuiProbe probe;
    const char *locale;
    if (session == NULL) return 0;
    session->descriptor = STDOUT_FILENO;
    session->viewport = (KofunTuiViewport){80, 24};
    locale = getenv("LC_ALL");
    if (locale == NULL || locale[0] == '\0') locale = getenv("LC_CTYPE");
    if (locale == NULL || locale[0] == '\0') locale = getenv("LANG");
    probe = (KofunTuiProbe){
        .is_tty = isatty(session->descriptor) == 1,
        .no_tui = no_tui != 0,
        .no_color = getenv("NO_COLOR") != NULL,
        .ci = getenv("CI") != NULL,
        .term = getenv("TERM"),
        .colorterm = getenv("COLORTERM"),
        .locale = locale,
        .colors = -1,
    };
    session->capabilities = kofun_tui_probe(probe);
    session_terminal_size(session);
    if (session->capabilities.interactive) {
        session->signal_registered = register_winch();
        if (write_all(session->descriptor, "\x1b[?25l", 6) != 0) {
            if (session->signal_registered) unregister_winch();
            free(session);
            return 0;
        }
    }
    return (long)(intptr_t)session;
}

int kofun_tui_end(long handle) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    int result = 0;
    if (session == NULL) return -1;
    if (session->capabilities.interactive) result = write_all(session->descriptor, "\x1b[?25h", 6);
    if (session->signal_registered) unregister_winch();
    free(session);
    return result;
}

int kofun_tui_set_size(long handle, long columns, long rows) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    if (session == NULL || columns <= 0 || rows <= 0) return -1;
    session->viewport.columns = minimum_size((size_t)columns, KOFUN_TUI_MAX_VIEWPORT_COLUMNS);
    session->viewport.rows = minimum_size((size_t)rows, KOFUN_TUI_MAX_VIEWPORT_ROWS);
    session->fixed_size = true;
    return 0;
}

int kofun_tui_progress(long handle, const char *label, long completed, long total, long phase) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    char storage[8192];
    KofunTuiBuffer buffer;
    if (session == NULL) return -1;
    session_terminal_size(session);
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    kofun_tui_render_progress(&buffer, session->capabilities, session->viewport,
        (KofunTuiProgress){label, completed < 0 ? 0U : (uint64_t)completed,
            total < 0 ? 0U : (uint64_t)total, phase < 0 ? 0U : (unsigned)phase});
    return session_emit(session, &buffer);
}

int kofun_tui_table2(long handle, const char *left_heading, const char *right_heading, const char *left_value, const char *right_value) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    const char *headers[] = {left_heading, right_heading};
    const char *cells[] = {left_value, right_value};
    char storage[8192];
    KofunTuiBuffer buffer;
    if (session == NULL) return -1;
    session_terminal_size(session);
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    kofun_tui_render_table(&buffer, session->capabilities, session->viewport,
        (KofunTuiTable){headers, cells, 2, 1});
    return session_emit(session, &buffer);
}

int kofun_tui_tree_item(long handle, const char *label, long depth, int last, int expanded) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    KofunTuiTreeNode node;
    char storage[8192];
    KofunTuiBuffer buffer;
    if (session == NULL) return -1;
    session_terminal_size(session);
    node = (KofunTuiTreeNode){label, depth < 0 ? 0U : (size_t)depth, last != 0, expanded != 0};
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    kofun_tui_render_tree(&buffer, session->capabilities, session->viewport, (KofunTuiTree){&node, 1});
    return session_emit(session, &buffer);
}

int kofun_tui_log(long handle, long sequence, long level, const char *message) {
    KofunTuiSession *session = (KofunTuiSession *)(intptr_t)handle;
    KofunTuiLogEntry entry;
    char storage[8192];
    KofunTuiBuffer buffer;
    if (session == NULL) return -1;
    session_terminal_size(session);
    entry = (KofunTuiLogEntry){sequence < 0 ? 0U : (uint64_t)sequence,
        level < 0 || level > 3 ? KOFUN_TUI_LOG_INFO : (KofunTuiLogLevel)level, message};
    kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
    kofun_tui_render_log(&buffer, session->capabilities, session->viewport, &entry, 1);
    return session_emit(session, &buffer);
}
