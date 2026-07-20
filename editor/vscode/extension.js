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
    await this.request('initialize', {
      processId: process.pid,
      rootUri: this.rootUri,
      capabilities: { general: { positionEncodings: ['utf-16'] } },
      workspaceFolders: vscode.workspace.workspaceFolders
        ? vscode.workspace.workspaceFolders.map((folder) => ({
          uri: folder.uri.toString(), name: folder.name
        })) : null
    });
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

async function activate(context) {
  const output = vscode.window.createOutputChannel('Kofun Language Server');
  context.subscriptions.push(output);
  try {
    const rootUri = vscode.workspace.workspaceFolders &&
      vscode.workspace.workspaceFolders.length > 0
      ? vscode.workspace.workspaceFolders[0].uri.toString() : null;
    activeClient = new KofunClient(serverCommand(context), rootUri, output);
    await activeClient.start(context);
    context.subscriptions.push({ dispose: () => activeClient.stop() });
  } catch (caught) {
    output.appendLine(caught.stack || caught.message);
    vscode.window.showErrorMessage(`Kofun language server: ${caught.message}`);
  }
}

async function deactivate() {
  if (activeClient) await activeClient.stop();
}

module.exports = { activate, deactivate };
