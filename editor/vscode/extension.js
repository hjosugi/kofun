'use strict';

const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

let activeClient;

class KofunClient {
  constructor(command, rootUri, output) {
    this.command = command;
    this.rootUri = rootUri;
    this.output = output;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.diagnostics = vscode.languages.createDiagnosticCollection('kofun');
    this.disposables = [this.diagnostics];
  }

  async start(context) {
    this.process = childProcess.spawn(this.command.executable, this.command.args, {
      stdio: ['pipe', 'pipe', 'pipe'], env: this.command.env
    });
    this.process.stdout.on('data', (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this.drain();
    });
    this.process.stderr.on('data', (chunk) => this.output.append(chunk.toString()));
    this.exited = new Promise((resolve) => this.process.on('exit', (code) => {
      if (code && code !== 0) this.output.appendLine(`kofun-lsp exited with ${code}`);
      resolve(code);
    }));
    this.process.on('error', (caught) => {
      this.output.appendLine(caught.message);
    });
    const configuration = vscode.workspace.getConfiguration('kofun');
    const initialized = await this.request('initialize', {
      processId: process.pid,
      rootUri: this.rootUri,
      capabilities: { general: { positionEncodings: ['utf-16'] } },
      initializationOptions: {
        inlayHints: {
          parameterNames: configuration.get('inlayHints.parameterNames', true),
          ownershipModes: configuration.get('inlayHints.ownershipModes', true),
          inferredTypes: configuration.get('inlayHints.inferredTypes', true)
        }
      },
      workspaceFolders: vscode.workspace.workspaceFolders
        ? vscode.workspace.workspaceFolders.map((folder) => ({
          uri: folder.uri.toString(), name: folder.name
        })) : null
    });
    const semantic = initialized && initialized.capabilities &&
      initialized.capabilities.semanticTokensProvider;
    this.semanticLegend = semantic && semantic.legend
      ? new vscode.SemanticTokensLegend(
        semantic.legend.tokenTypes, semantic.legend.tokenModifiers)
      : new vscode.SemanticTokensLegend([], []);
    this.notify('initialized', {});

    for (const document of vscode.workspace.textDocuments) this.open(document);
    this.disposables.push(
      vscode.workspace.onDidOpenTextDocument((document) => this.open(document)),
      vscode.workspace.onDidChangeTextDocument((event) => this.change(event)),
      vscode.workspace.onDidCloseTextDocument((document) => this.close(document)),
      vscode.languages.registerDefinitionProvider('kofun', {
        provideDefinition: async (document, position) => {
          const result = await this.request('textDocument/definition', {
            textDocument: { uri: document.uri.toString() }, position
          });
          return result ? new vscode.Location(
            vscode.Uri.parse(result.uri), this.toRange(result.range)
          ) : null;
        }
      }),
      vscode.languages.registerHoverProvider('kofun', {
        provideHover: async (document, position) => {
          const result = await this.request('textDocument/hover', {
            textDocument: { uri: document.uri.toString() }, position
          });
          if (!result) return null;
          const contents = result.contents && result.contents.value
            ? new vscode.MarkdownString(result.contents.value) : result.contents;
          return new vscode.Hover(contents, result.range ? this.toRange(result.range) : undefined);
        }
      }),
      vscode.languages.registerCompletionItemProvider('kofun', {
        provideCompletionItems: async (document, position) => {
          const result = await this.request('textDocument/completion', {
            textDocument: { uri: document.uri.toString() }, position
          });
          if (!result) return null;
          const items = result.items.map((item) => {
            // VS Code's CompletionItemKind is the LSP enumeration minus one,
            // the same offset the diagnostic severities above are converted by.
            const value = new vscode.CompletionItem(
              item.label, item.kind === undefined ? undefined : item.kind - 1);
            if (item.detail) value.detail = item.detail;
            if (item.sortText) value.sortText = item.sortText;
            return value;
          });
          return new vscode.CompletionList(items, result.isIncomplete === true);
        }
      }),
      vscode.languages.registerDocumentSymbolProvider('kofun', {
        provideDocumentSymbols: async (document) => {
          const result = await this.request('textDocument/documentSymbol', {
            textDocument: { uri: document.uri.toString() }
          });
          if (!Array.isArray(result)) return [];
          // VS Code's SymbolKind is the LSP enumeration minus one, as its
          // DiagnosticSeverity and CompletionItemKind are. InlayHintKind below
          // is the exception: that one matches LSP exactly.
          return result.map((item) => {
            const symbol = new vscode.DocumentSymbol(
              item.name, item.detail || '', item.kind - 1,
              this.toRange(item.range), this.toRange(item.selectionRange)
            );
            symbol.children = (item.children || []).map((child) =>
              new vscode.DocumentSymbol(
                child.name, child.detail || '', child.kind - 1,
                this.toRange(child.range), this.toRange(child.selectionRange)
              ));
            return symbol;
          });
        }
      }),
      vscode.languages.registerReferenceProvider('kofun', {
        provideReferences: async (document, position, context) => {
          const result = await this.request('textDocument/references', {
            textDocument: { uri: document.uri.toString() }, position,
            context: { includeDeclaration: context ? context.includeDeclaration : true }
          });
          if (!Array.isArray(result)) return [];
          return result.map((item) => new vscode.Location(
            vscode.Uri.parse(item.uri), this.toRange(item.range)));
        }
      }),
      vscode.languages.registerDocumentHighlightProvider('kofun', {
        provideDocumentHighlights: async (document, position) => {
          const result = await this.request('textDocument/documentHighlight', {
            textDocument: { uri: document.uri.toString() }, position
          });
          if (!Array.isArray(result)) return [];
          return result.map((item) => new vscode.DocumentHighlight(
            this.toRange(item.range), item.kind - 1));
        }
      }),
      vscode.languages.registerDocumentSemanticTokensProvider('kofun', {
        provideDocumentSemanticTokens: async (document) => {
          const result = await this.request('textDocument/semanticTokens/full', {
            textDocument: { uri: document.uri.toString() }
          });
          if (!result || !Array.isArray(result.data)) return null;
          // The server already emits the delta encoding the protocol defines,
          // so the array is handed over as-is rather than decoded and rebuilt.
          return new vscode.SemanticTokens(new Uint32Array(result.data));
        }
      }, this.semanticLegend),
      vscode.languages.registerRenameProvider('kofun', {
        prepareRename: async (document, position) => {
          const result = await this.request('textDocument/prepareRename', {
            textDocument: { uri: document.uri.toString() }, position
          });
          if (!result) throw new Error('this name cannot be renamed here');
          return {
            range: this.toRange(result.range), placeholder: result.placeholder
          };
        },
        provideRenameEdits: async (document, position, newName) => {
          const result = await this.request('textDocument/rename', {
            textDocument: { uri: document.uri.toString() }, position, newName
          });
          if (!result || !result.changes) return null;
          const edit = new vscode.WorkspaceEdit();
          for (const [uri, edits] of Object.entries(result.changes)) {
            for (const item of edits) {
              edit.replace(vscode.Uri.parse(uri), this.toRange(item.range), item.newText);
            }
          }
          return edit;
        }
      }),
      vscode.languages.registerSignatureHelpProvider('kofun', {
        provideSignatureHelp: async (document, position) => {
          const result = await this.request('textDocument/signatureHelp', {
            textDocument: { uri: document.uri.toString() }, position
          });
          if (!result || !Array.isArray(result.signatures)) return null;
          const help = new vscode.SignatureHelp();
          help.signatures = result.signatures.map((item) => {
            const signature = new vscode.SignatureInformation(item.label);
            signature.parameters = (item.parameters || []).map((parameter) =>
              new vscode.ParameterInformation(parameter.label));
            return signature;
          });
          help.activeSignature = result.activeSignature || 0;
          help.activeParameter = result.activeParameter || 0;
          return help;
        }
      }, '(', ','),
      vscode.languages.registerFoldingRangeProvider('kofun', {
        provideFoldingRanges: async (document) => {
          const result = await this.request('textDocument/foldingRange', {
            textDocument: { uri: document.uri.toString() }
          });
          if (!Array.isArray(result)) return [];
          const kinds = {
            comment: vscode.FoldingRangeKind ? vscode.FoldingRangeKind.Comment : undefined,
            region: vscode.FoldingRangeKind ? vscode.FoldingRangeKind.Region : undefined
          };
          return result.map((item) =>
            new vscode.FoldingRange(item.startLine, item.endLine, kinds[item.kind]));
        }
      }),
      vscode.languages.registerSelectionRangeProvider('kofun', {
        provideSelectionRanges: async (document, positions) => {
          const result = await this.request('textDocument/selectionRange', {
            textDocument: { uri: document.uri.toString() },
            positions: positions.map((position) => ({
              line: position.line, character: position.character
            }))
          });
          if (!Array.isArray(result)) return [];
          const build = (node) => node
            ? new vscode.SelectionRange(this.toRange(node.range), build(node.parent))
            : undefined;
          return result.map(build);
        }
      }),
      vscode.languages.registerInlayHintsProvider('kofun', {
        provideInlayHints: async (document, range) => {
          const result = await this.request('textDocument/inlayHint', {
            textDocument: { uri: document.uri.toString() },
            range: range ? {
              start: range.start, end: range.end
            } : undefined
          });
          if (!Array.isArray(result)) return [];
          // InlayHintKind is one of the few enumerations LSP and VS Code
          // number identically, so this one is passed through unchanged.
          return result.map((item) => {
            const hint = new vscode.InlayHint(
              new vscode.Position(item.position.line, item.position.character),
              item.label, item.kind
            );
            if (item.tooltip) hint.tooltip = item.tooltip;
            if (item.paddingLeft) hint.paddingLeft = true;
            if (item.paddingRight) hint.paddingRight = true;
            return hint;
          });
        }
      })
    );
    context.subscriptions.push(...this.disposables);
  }

  frame(message) {
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
    this.process.stdin.write(body);
  }

  notify(method, params) {
    this.frame({ jsonrpc: '2.0', method, params });
  }

  request(method, params) {
    const id = this.nextId++;
    this.frame({ jsonrpc: '2.0', id, method, params });
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  drain() {
    for (;;) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) return;
      const header = this.buffer.subarray(0, headerEnd).toString('ascii');
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        this.buffer = this.buffer.subarray(headerEnd + 4);
        continue;
      }
      const length = Number(match[1]);
      if (this.buffer.length < headerEnd + 4 + length) return;
      const body = this.buffer.subarray(headerEnd + 4, headerEnd + 4 + length);
      this.buffer = this.buffer.subarray(headerEnd + 4 + length);
      const message = JSON.parse(body.toString('utf8'));
      if (message.method === 'textDocument/publishDiagnostics') {
        const uri = vscode.Uri.parse(message.params.uri);
        const diagnostics = message.params.diagnostics.map((item) => {
          const value = new vscode.Diagnostic(
            this.toRange(item.range), item.message, item.severity - 1
          );
          value.code = item.code;
          value.source = item.source;
          return value;
        });
        this.diagnostics.set(uri, diagnostics);
      } else if (Object.prototype.hasOwnProperty.call(message, 'id')) {
        const pending = this.pending.get(message.id);
        if (!pending) continue;
        this.pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message));
        else pending.resolve(message.result);
      }
    }
  }

  toRange(value) {
    return new vscode.Range(
      value.start.line, value.start.character, value.end.line, value.end.character
    );
  }

  open(document) {
    if (document.languageId !== 'kofun') return;
    this.notify('textDocument/didOpen', {
      textDocument: {
        uri: document.uri.toString(), languageId: 'kofun',
        version: document.version, text: document.getText()
      }
    });
  }

  change(event) {
    if (event.document.languageId !== 'kofun') return;
    this.notify('textDocument/didChange', {
      textDocument: {
        uri: event.document.uri.toString(), version: event.document.version
      },
      contentChanges: event.contentChanges.map((change) => ({
        range: change.range, rangeLength: change.rangeLength, text: change.text
      }))
    });
  }

  close(document) {
    if (document.languageId !== 'kofun') return;
    this.notify('textDocument/didClose', {
      textDocument: { uri: document.uri.toString() }
    });
    this.diagnostics.delete(document.uri);
  }

  async stop() {
    if (!this.process || this.process.killed || this.stopping) return this.stopping;
    this.stopping = (async () => {
    try {
      await this.request('shutdown', null);
      this.notify('exit', null);
      this.process.stdin.end();
      await this.exited;
    } catch (caught) {
      this.output.appendLine(caught.message);
      this.process.kill();
    }
    })();
    return this.stopping;
  }
}

function serverCommand(context) {
  const configured = vscode.workspace.getConfiguration('kofun')
    .get('languageServer.path', '');
  if (configured) {
    return { executable: configured, args: [], env: process.env };
  }
  const bundled = path.join(context.extensionPath, 'server', 'server.js');
  if (fs.existsSync(bundled)) {
    return {
      executable: process.execPath,
      args: ['--expose-gc', '--optimize-for-size', bundled],
      env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' }
    };
  }
  throw new Error('Bundled Kofun language server is missing.');
}

// A server that died is otherwise invisible until a request silently returns
// nothing, so its state is always on screen while a Kofun file is open.
function createStatusItem(context) {
  const item = vscode.window.createStatusBarItem
    ? vscode.window.createStatusBarItem(vscode.StatusBarAlignment
      ? vscode.StatusBarAlignment.Right : undefined, 100)
    : null;
  if (!item) return null;
  item.command = 'kofun.showOutput';
  context.subscriptions.push(item);
  return item;
}

function setStatus(item, state, detail) {
  if (!item) return;
  const faces = {
    starting: '$(sync~spin) Kofun',
    running: '$(check) Kofun',
    stopped: '$(error) Kofun'
  };
  item.text = faces[state] || 'Kofun';
  item.tooltip = detail;
  item.show();
}

async function startClient(context, output, statusItem) {
  const rootUri = vscode.workspace.workspaceFolders &&
    vscode.workspace.workspaceFolders.length > 0
    ? vscode.workspace.workspaceFolders[0].uri.toString() : null;
  setStatus(statusItem, 'starting', 'Kofun language server is starting');
  const client = new KofunClient(serverCommand(context), rootUri, output);
  await client.start(context);
  setStatus(statusItem, 'running', 'Kofun language server is running');
  return client;
}

// `kofun check|build|test` as ordinary tasks, the way the Go and Rust
// extensions expose their toolchains. No problemMatcher is contributed: the
// CLI reports byte offsets rather than line and column, so a matcher would
// place every problem on the wrong line. Diagnostics come from the language
// server, which converts those byte spans against the open document.
function registerTasks(context) {
  if (!vscode.tasks || !vscode.tasks.registerTaskProvider) return;
  const definitions = [
    { name: 'check', description: 'Type-check the active file' },
    { name: 'build', description: 'Build the active file' },
    { name: 'test', description: 'Run the tests beside the active file' }
  ];
  context.subscriptions.push(vscode.tasks.registerTaskProvider('kofun', {
    provideTasks: () => definitions.map((entry) => {
      const executable = vscode.workspace.getConfiguration('kofun')
        .get('cli.path', 'kofun');
      const task = new vscode.Task(
        { type: 'kofun', command: entry.name },
        vscode.TaskScope ? vscode.TaskScope.Workspace : undefined,
        entry.name, 'kofun',
        new vscode.ShellExecution(executable, [entry.name, '${file}'])
      );
      task.detail = entry.description;
      return task;
    }),
    resolveTask: () => undefined
  }));
}

async function activate(context) {
  const output = vscode.window.createOutputChannel('Kofun Language Server');
  context.subscriptions.push(output);
  const statusItem = createStatusItem(context);
  registerTasks(context);

  context.subscriptions.push(
    vscode.commands.registerCommand('kofun.showOutput', () => output.show(true)),
    vscode.commands.registerCommand('kofun.restartServer', async () => {
      output.appendLine('restarting the Kofun language server');
      try {
        if (activeClient) await activeClient.stop();
        activeClient = await startClient(context, output, statusItem);
      } catch (caught) {
        setStatus(statusItem, 'stopped', caught.message);
        output.appendLine(caught.stack || caught.message);
        vscode.window.showErrorMessage(`Kofun language server: ${caught.message}`);
      }
    })
  );

  try {
    activeClient = await startClient(context, output, statusItem);
    context.subscriptions.push({ dispose: () => activeClient.stop() });
  } catch (caught) {
    setStatus(statusItem, 'stopped', caught.message);
    output.appendLine(caught.stack || caught.message);
    vscode.window.showErrorMessage(`Kofun language server: ${caught.message}`);
  }
}

async function deactivate() {
  if (activeClient) await activeClient.stop();
}

module.exports = { activate, deactivate };
