# Native CLI security boundary

The final application uses only Linux `write`, `ioctl(TCGETS)`, and `exit`
syscalls. It does not load shared objects, search `PATH`, execute a shell,
open configuration files, allocate a heap, or evaluate command text. Runtime
arguments and environment values are treated as byte strings supplied by the
kernel.

The declaration compiler enforces fixed limits before serialization:

- 65,536 source bytes and 65,536 metadata bytes;
- 63-byte identifiers and 255-byte strings;
- eight commands, four positions per command, and eight options per command;
- unique command names, option identifiers, and long names;
- checked action-specific position and option shapes;
- explicit errors for malformed tokens, strings, escapes, braces, and actions.

Source strings cannot contain literal C0 control bytes or newlines. Escaped
newlines and tabs are supported, but terminal escape bytes are not representable
by the declaration grammar. Runtime values may contain arbitrary
process-provided bytes; applications that echo untrusted values to a terminal
must account for terminal escape injection in a future policy layer.

`ioctl(TCGETS)` determines whether stdout is a terminal. Color requires both a
terminal and absence of `NO_COLOR`; redirecting stdout disables ANSI output.
The status action still uses carriage-return/erase control on a terminal when
color is disabled, because those bytes implement progress replacement rather
than color.

The embedded declaration table is trusted compiler output. Editing or
corrupting the emitted ELF after compilation is outside the runtime's trust
boundary. The active gate protects the checked runtime prefix with source
hashes and byte-for-byte regeneration.

The C11 compiler and checked x86-64 runtime prefix are bootstrap artifacts.
They do not establish memory safety for arbitrary native Kofun code, and this
profile must not be described as general native lowering.
