# Kofun VS Code metadata

This extension provides `.kofun` language registration, comments, brackets,
indentation, TextMate highlighting, inline diagnostics, go-to-definition, and
hover types. The dependency-free stdio language server is bundled in `server/`
and starts when a Kofun document opens.

To test locally, open `editor/vscode` with VS Code's extension development host.
Set `kofun.languageServer.path` only when testing another server build.
