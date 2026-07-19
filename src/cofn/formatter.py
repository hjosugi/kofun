from __future__ import annotations


def format_source(source: str, indent_width: int = 4) -> str:
    """Conservative bootstrap formatter.

    It normalizes indentation and trailing whitespace while preserving comments
    and token spelling. The full syntax-aware formatter is tracked in the
    language backlog; this version is intentionally lossless for comments.
    """

    output: list[str] = []
    indent = 0
    blank_count = 0
    for raw in source.splitlines():
        stripped = raw.strip()
        if not stripped:
            blank_count += 1
            if output and blank_count <= 1:
                output.append("")
            continue
        blank_count = 0
        leading_closes = _leading_closing_braces(stripped)
        indent = max(0, indent - leading_closes)
        output.append(" " * (indent_width * indent) + stripped)
        opens, closes = _brace_delta(stripped)
        # Leading closes were already applied; account for remaining closes.
        indent = max(0, indent + opens - max(0, closes - leading_closes))
    while output and output[-1] == "":
        output.pop()
    return "\n".join(output) + ("\n" if output else "")


def _leading_closing_braces(line: str) -> int:
    count = 0
    for char in line:
        if char == "}":
            count += 1
        elif char.isspace():
            continue
        else:
            break
    return count


def _brace_delta(line: str) -> tuple[int, int]:
    opens = 0
    closes = 0
    in_string = False
    escaped = False
    for index, char in enumerate(line):
        if not in_string and char == "#":
            break
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            opens += 1
        elif char == "}":
            closes += 1
    return opens, closes
