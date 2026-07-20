/*
 * Freestanding Linux x86-64 runtime for Kofun's bounded native CLI profile.
 *
 * There is deliberately no libc entry point or runtime dependency here.
 * framework/cli/compiler.c appends a validated declaration table at the
 * cli_config_anchor virtual address in the final ELF image.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CONFIG_MAGIC = 0x31494c43U,
    HEADER_SIZE = 28,
    COMMAND_SIZE = 32,
    POSITION_SIZE = 8,
    OPTION_SIZE = 20,
    MAX_POSITIONS = 4,
    MAX_OPTIONS = 8,
    ACTION_GREET = 1,
    ACTION_SUM = 2,
    ACTION_ENV = 3,
    ACTION_STATUS = 4,
    OPTION_BOOL = 1,
    OPTION_TEXT = 2,
    EXIT_USAGE = 2,
    EXIT_ENVIRONMENT = 3,
    SYS_WRITE = 1,
    SYS_IOCTL = 16,
    TCGETS = 0x5401,
};

extern const unsigned char cli_config_anchor[];

static long system_call3(long number, long first, long second, long third) {
    register long rax __asm__("rax") = number;
    register long rdi __asm__("rdi") = first;
    register long rsi __asm__("rsi") = second;
    register long rdx __asm__("rdx") = third;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static bool text_equal(const char *left, const char *right) {
    size_t index = 0;
    while (left[index] == right[index]) {
        if (left[index] == '\0') return true;
        ++index;
    }
    return false;
}

static bool text_prefix(
    const char *text,
    const char *prefix,
    size_t prefix_length
) {
    for (size_t index = 0; index < prefix_length; ++index) {
        if (text[index] != prefix[index]) return false;
    }
    return true;
}

static void write_all(int descriptor, const char *text, size_t length) {
    while (length > 0) {
        long written = system_call3(
            SYS_WRITE,
            descriptor,
            (long)(uintptr_t)text,
            (long)length
        );
        if (written <= 0) return;
        text += (size_t)written;
        length -= (size_t)written;
    }
}

static void write_text(int descriptor, const char *text) {
    write_all(descriptor, text, text_length(text));
}

static uint32_t load_u32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static const char *config_text(
    const unsigned char *config,
    uint32_t offset
) {
    return (const char *)(config + offset);
}

static const unsigned char *command_at(
    const unsigned char *config,
    uint32_t index
) {
    return config + load_u32(config + 24) + index * COMMAND_SIZE;
}

static const unsigned char *position_at(
    const unsigned char *config,
    const unsigned char *command,
    uint32_t index
) {
    return config + load_u32(command + 16) + index * POSITION_SIZE;
}

static const unsigned char *option_at(
    const unsigned char *config,
    const unsigned char *command,
    uint32_t index
) {
    return config + load_u32(command + 24) + index * OPTION_SIZE;
}

static const unsigned char *find_command(
    const unsigned char *config,
    const char *name
) {
    uint32_t count = load_u32(config + 20);
    for (uint32_t index = 0; index < count; ++index) {
        const unsigned char *command = command_at(config, index);
        if (text_equal(config_text(config, load_u32(command)), name)) {
            return command;
        }
    }
    return NULL;
}

static bool environment_lookup(
    char **environment,
    const char *name,
    const char **value
) {
    size_t name_length = text_length(name);
    for (size_t index = 0; environment[index] != NULL; ++index) {
        const char *entry = environment[index];
        if (text_prefix(entry, name, name_length) &&
            entry[name_length] == '=') {
            *value = entry + name_length + 1;
            return true;
        }
    }
    return false;
}

static bool output_is_terminal(void) {
    unsigned char terminal[64];
    long result = system_call3(
        SYS_IOCTL,
        1,
        TCGETS,
        (long)(uintptr_t)terminal
    );
    return result == 0;
}

static bool color_enabled(char **environment) {
    const char *ignored = NULL;
    return output_is_terminal() &&
        !environment_lookup(environment, "NO_COLOR", &ignored);
}

static void usage_error_start(const unsigned char *config) {
    write_text(2, config_text(config, load_u32(config + 8)));
    write_text(2, ": ");
}

static int unknown_command(
    const unsigned char *config,
    const char *argument
) {
    usage_error_start(config);
    write_text(2, "unknown command: ");
    write_text(2, argument);
    write_text(2, "\n");
    return EXIT_USAGE;
}

static int unknown_option(
    const unsigned char *config,
    const unsigned char *command,
    const char *argument
) {
    usage_error_start(config);
    write_text(2, "unknown option for ");
    write_text(2, config_text(config, load_u32(command)));
    write_text(2, ": ");
    write_text(2, argument);
    write_text(2, "\n");
    return EXIT_USAGE;
}

static void print_global_help(const unsigned char *config) {
    const char *name = config_text(config, load_u32(config + 8));
    write_text(1, name);
    write_text(1, " ");
    write_text(1, config_text(config, load_u32(config + 12)));
    write_text(1, "\n");
    write_text(1, config_text(config, load_u32(config + 16)));
    write_text(1, "\n\nUsage: ");
    write_text(1, name);
    write_text(1, " <command> [options]\n\nCommands:\n");
    uint32_t count = load_u32(config + 20);
    for (uint32_t index = 0; index < count; ++index) {
        const unsigned char *command = command_at(config, index);
        write_text(1, "  ");
        write_text(1, config_text(config, load_u32(command)));
        write_text(1, "\t");
        write_text(1, config_text(config, load_u32(command + 4)));
        write_text(1, "\n");
    }
    write_text(1, "\nRun '");
    write_text(1, name);
    write_text(1, " <command> --help' for command help.\n");
}

static void print_command_help(
    const unsigned char *config,
    const unsigned char *command
) {
    const char *app_name = config_text(config, load_u32(config + 8));
    const char *command_name = config_text(config, load_u32(command));
    write_text(1, command_name);
    write_text(1, " - ");
    write_text(1, config_text(config, load_u32(command + 4)));
    write_text(1, "\n\nUsage: ");
    write_text(1, app_name);
    write_text(1, " ");
    write_text(1, command_name);
    uint32_t position_count = load_u32(command + 12);
    for (uint32_t index = 0; index < position_count; ++index) {
        const unsigned char *position =
            position_at(config, command, index);
        write_text(1, " <");
        write_text(1, config_text(config, load_u32(position)));
        write_text(1, ">");
    }
    if (load_u32(command + 20) > 0) write_text(1, " [options]");
    write_text(1, "\n");
    if (position_count > 0) {
        write_text(1, "\nArguments:\n");
        for (uint32_t index = 0; index < position_count; ++index) {
            const unsigned char *position =
                position_at(config, command, index);
            write_text(1, "  ");
            write_text(1, config_text(config, load_u32(position)));
            write_text(1, "\t");
            write_text(1, config_text(config, load_u32(position + 4)));
            write_text(1, "\n");
        }
    }
    uint32_t option_count = load_u32(command + 20);
    if (option_count > 0) {
        write_text(1, "\nOptions:\n");
        for (uint32_t index = 0; index < option_count; ++index) {
            const unsigned char *option =
                option_at(config, command, index);
            write_text(1, "  ");
            write_text(1, config_text(config, load_u32(option + 4)));
            if (load_u32(option + 12) == OPTION_TEXT) {
                write_text(1, " <VALUE>");
            }
            write_text(1, "\t");
            write_text(1, config_text(config, load_u32(option + 8)));
            write_text(1, "\n");
        }
    }
    write_text(1, "\n  --help\tShow this help\n");
}

static int parse_integer(const char *text, int64_t *result) {
    bool negative = false;
    size_t index = 0;
    uint64_t value = 0;
    if (text[0] == '-') {
        negative = true;
        ++index;
    }
    if (text[index] == '\0') return 0;
    for (; text[index] != '\0'; ++index) {
        unsigned char digit = (unsigned char)text[index];
        if (digit < '0' || digit > '9') return 0;
        uint64_t limit = negative ?
            UINT64_C(9223372036854775808) :
            UINT64_C(9223372036854775807);
        if (value > (limit - (uint64_t)(digit - '0')) / 10) return 0;
        value = value * 10 + (uint64_t)(digit - '0');
    }
    if (negative) {
        if (value == UINT64_C(9223372036854775808)) {
            *result = INT64_MIN;
        } else {
            *result = -(int64_t)value;
        }
    } else {
        *result = (int64_t)value;
    }
    return 1;
}

static void print_integer(int64_t value) {
    char buffer[32];
    size_t cursor = sizeof(buffer);
    uint64_t magnitude;
    if (value < 0) {
        magnitude = (uint64_t)(-(value + 1)) + 1;
    } else {
        magnitude = (uint64_t)value;
    }
    do {
        uint64_t quotient = magnitude / 10;
        buffer[--cursor] = (char)('0' + magnitude - quotient * 10);
        magnitude = quotient;
    } while (magnitude != 0);
    if (value < 0) buffer[--cursor] = '-';
    write_all(1, buffer + cursor, sizeof(buffer) - cursor);
}

static int action_greet(
    const unsigned char *config,
    const unsigned char *command,
    const char **positions,
    const char **option_values,
    const bool *option_seen,
    char **environment
) {
    const char *prefix = "Hello";
    bool shout = false;
    uint32_t option_count = load_u32(command + 20);
    for (uint32_t index = 0; index < option_count; ++index) {
        const unsigned char *option = option_at(config, command, index);
        const char *identifier = config_text(config, load_u32(option));
        if (text_equal(identifier, "shout")) {
            shout = option_seen[index];
        } else if (text_equal(identifier, "prefix")) {
            if (option_seen[index]) {
                prefix = option_values[index];
            } else if (load_u32(option + 16) != 0) {
                prefix = config_text(config, load_u32(option + 16));
            }
        }
    }
    bool color = color_enabled(environment);
    if (color) write_text(1, "\033[32m");
    if (shout) {
        const char *parts[3] = {prefix, ", ", positions[0]};
        for (size_t part = 0; part < 3; ++part) {
            const char *text = parts[part];
            for (size_t index = 0; text[index] != '\0'; ++index) {
                char letter = text[index];
                if (letter >= 'a' && letter <= 'z') {
                    letter = (char)(letter - ('a' - 'A'));
                }
                write_all(1, &letter, 1);
            }
        }
    } else {
        write_text(1, prefix);
        write_text(1, ", ");
        write_text(1, positions[0]);
    }
    write_text(1, "!");
    if (color) write_text(1, "\033[0m");
    write_text(1, "\n");
    return 0;
}

static int action_sum(
    const unsigned char *config,
    const char **positions
) {
    int64_t left;
    int64_t right;
    if (!parse_integer(positions[0], &left) ||
        !parse_integer(positions[1], &right) ||
        (right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        usage_error_start(config);
        write_text(2, "sum expects two Int64 values without overflow\n");
        return EXIT_USAGE;
    }
    print_integer(left + right);
    write_text(1, "\n");
    return 0;
}

static int action_environment(
    const unsigned char *config,
    const char **positions,
    char **environment
) {
    const char *value = NULL;
    if (!environment_lookup(environment, positions[0], &value)) {
        usage_error_start(config);
        write_text(2, "environment variable not set: ");
        write_text(2, positions[0]);
        write_text(2, "\n");
        return EXIT_ENVIRONMENT;
    }
    write_text(1, value);
    write_text(1, "\n");
    return 0;
}

static int action_status(
    const char **positions,
    char **environment
) {
    if (!output_is_terminal()) {
        write_text(1, "status: ");
        write_text(1, positions[0]);
        write_text(1, "\n");
        return 0;
    }
    bool color = color_enabled(environment);
    write_text(1, "\r");
    if (color) write_text(1, "\033[36m");
    write_text(1, "... ");
    write_text(1, positions[0]);
    if (color) write_text(1, "\033[0m");
    write_text(1, "\r\033[K");
    if (color) write_text(1, "\033[32m");
    write_text(1, "done ");
    write_text(1, positions[0]);
    if (color) write_text(1, "\033[0m");
    write_text(1, "\n");
    return 0;
}

static const unsigned char *find_option(
    const unsigned char *config,
    const unsigned char *command,
    const char *argument,
    size_t name_length,
    uint32_t *found_index
) {
    uint32_t option_count = load_u32(command + 20);
    for (uint32_t index = 0; index < option_count; ++index) {
        const unsigned char *option = option_at(config, command, index);
        const char *long_name = config_text(config, load_u32(option + 4));
        if (text_length(long_name) == name_length &&
            text_prefix(argument, long_name, name_length)) {
            *found_index = index;
            return option;
        }
    }
    return NULL;
}

static int execute_command(
    const unsigned char *config,
    const unsigned char *command,
    int argument_count,
    char **arguments,
    char **environment
) {
    const char *positions[MAX_POSITIONS] = {NULL, NULL, NULL, NULL};
    const char *option_values[MAX_OPTIONS] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    };
    bool option_seen[MAX_OPTIONS] = {
        false, false, false, false, false, false, false, false,
    };
    uint32_t position_count = load_u32(command + 12);
    uint32_t seen_positions = 0;
    bool options_enabled = true;
    for (int argument_index = 0;
         argument_index < argument_count;
         ++argument_index) {
        const char *argument = arguments[argument_index];
        if (options_enabled && text_equal(argument, "--help")) {
            print_command_help(config, command);
            return 0;
        }
        if (options_enabled && text_equal(argument, "--")) {
            options_enabled = false;
            continue;
        }
        if (options_enabled && argument[0] == '-' && argument[1] == '-') {
            size_t name_length = 0;
            while (argument[name_length] != '\0' &&
                   argument[name_length] != '=') {
                ++name_length;
            }
            uint32_t option_index = 0;
            const unsigned char *option = find_option(
                config,
                command,
                argument,
                name_length,
                &option_index
            );
            if (option == NULL) {
                return unknown_option(config, command, argument);
            }
            if (option_seen[option_index]) {
                usage_error_start(config);
                write_text(2, "duplicate option: ");
                write_all(2, argument, name_length);
                write_text(2, "\n");
                return EXIT_USAGE;
            }
            option_seen[option_index] = true;
            uint32_t kind = load_u32(option + 12);
            if (kind == OPTION_BOOL) {
                if (argument[name_length] == '=') {
                    usage_error_start(config);
                    write_text(2, "boolean option does not take a value: ");
                    write_all(2, argument, name_length);
                    write_text(2, "\n");
                    return EXIT_USAGE;
                }
            } else {
                if (argument[name_length] == '=') {
                    if (argument[name_length + 1] == '\0') {
                        usage_error_start(config);
                        write_text(2, "missing value for option: ");
                        write_all(2, argument, name_length);
                        write_text(2, "\n");
                        return EXIT_USAGE;
                    }
                    option_values[option_index] = argument + name_length + 1;
                } else {
                    if (argument_index + 1 >= argument_count) {
                        usage_error_start(config);
                        write_text(2, "missing value for option: ");
                        write_all(2, argument, name_length);
                        write_text(2, "\n");
                        return EXIT_USAGE;
                    }
                    option_values[option_index] =
                        arguments[++argument_index];
                }
            }
            continue;
        }
        if (seen_positions >= position_count) {
            usage_error_start(config);
            write_text(2, "unexpected argument for ");
            write_text(2, config_text(config, load_u32(command)));
            write_text(2, ": ");
            write_text(2, argument);
            write_text(2, "\n");
            return EXIT_USAGE;
        }
        positions[seen_positions++] = argument;
    }
    if (seen_positions < position_count) {
        const unsigned char *position =
            position_at(config, command, seen_positions);
        usage_error_start(config);
        write_text(2, "missing argument ");
        write_text(2, config_text(config, load_u32(position)));
        write_text(2, " for ");
        write_text(2, config_text(config, load_u32(command)));
        write_text(2, "\n");
        return EXIT_USAGE;
    }
    uint32_t action = load_u32(command + 8);
    if (action == ACTION_GREET) {
        return action_greet(
            config,
            command,
            positions,
            option_values,
            option_seen,
            environment
        );
    }
    if (action == ACTION_SUM) return action_sum(config, positions);
    if (action == ACTION_ENV) {
        return action_environment(config, positions, environment);
    }
    return action_status(positions, environment);
}

int cli_main(int argument_count, char **arguments, char **environment) {
    const unsigned char *config = cli_config_anchor;
    if (load_u32(config) != CONFIG_MAGIC ||
        load_u32(config + 4) < HEADER_SIZE) {
        write_text(2, "kofun cli: invalid embedded declaration\n");
        return 125;
    }
    if (argument_count <= 1 ||
        text_equal(arguments[1], "--help") ||
        text_equal(arguments[1], "-h")) {
        print_global_help(config);
        return 0;
    }
    const unsigned char *command = find_command(config, arguments[1]);
    if (command == NULL) return unknown_command(config, arguments[1]);
    return execute_command(
        config,
        command,
        argument_count - 2,
        arguments + 2,
        environment
    );
}
