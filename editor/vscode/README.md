# Kofun VS Code metadata

This extension provides `.kofun` language registration, comments, brackets,
indentation, TextMate highlighting, inline diagnostics, go-to-definition, and
hover types. The stdio language server is bundled in `server/` and starts when
a Kofun document opens. Diagnostics, hover, and definition for the covered
Stage 2 subset consume one validated in-memory typed sidecar; sources outside
the bounded producer profile use a visibly labelled syntactic fallback.

Run `npm --prefix editor/vscode run vscode:prepublish` before opening
`editor/vscode` with VS Code's extension development host or packaging a VSIX.
That command builds the host Node-API producer bridge and stages the audited
codec/projector beside the bundled server. A C11 compiler and Node headers are
required; set `NODE_INCLUDE_DIR` only when the headers are outside the Node
installation prefix and standard include locations.

Set `kofun.languageServer.path` only when testing another server build.
