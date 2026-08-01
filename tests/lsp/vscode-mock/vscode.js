'use strict';

class Position {
  constructor(line, character) {
    this.line = line;
    this.character = character;
  }
}

class Range {
  constructor(startLine, startCharacter, endLine, endCharacter) {
    this.start = new Position(startLine, startCharacter);
    this.end = new Position(endLine, endCharacter);
  }
}

class Uri {
  constructor(value) { this.value = value; }
  toString() { return this.value; }
  static parse(value) { return new Uri(value); }
}

class Location {
  constructor(uri, range) { this.uri = uri; this.range = range; }
}

class MarkdownString {
  constructor(value) { this.value = value; }
}

class Hover {
  constructor(contents, range) { this.contents = contents; this.range = range; }
}

class Diagnostic {
  constructor(range, message, severity) {
    this.range = range;
    this.message = message;
    this.severity = severity;
  }
}

class CompletionItem {
  constructor(label, kind) {
    this.label = label;
    this.kind = kind;
  }
}

class DocumentSymbol {
  constructor(name, detail, kind, range, selectionRange) {
    this.name = name;
    this.detail = detail;
    this.kind = kind;
    this.range = range;
    this.selectionRange = selectionRange;
    this.children = [];
  }
}

class DocumentHighlight {
  constructor(range, kind) { this.range = range; this.kind = kind; }
}

class SignatureHelp {
  constructor() {
    this.signatures = [];
    this.activeSignature = 0;
    this.activeParameter = 0;
  }
}

class SignatureInformation {
  constructor(label) { this.label = label; this.parameters = []; }
}

class ParameterInformation {
  constructor(label) { this.label = label; }
}

class FoldingRange {
  constructor(start, end, kind) { this.start = start; this.end = end; this.kind = kind; }
}

class SelectionRange {
  constructor(range, parent) { this.range = range; this.parent = parent; }
}

class Task {
  constructor(definition, scope, name, source, execution) {
    this.definition = definition;
    this.scope = scope;
    this.name = name;
    this.source = source;
    this.execution = execution;
  }
}

class ShellExecution {
  constructor(command, args) { this.command = command; this.args = args; }
}

class InlayHint {
  constructor(position, label, kind) {
    this.position = position;
    this.label = label;
    this.kind = kind;
  }
}

class CompletionList {
  constructor(items, isIncomplete) {
    this.items = items;
    this.isIncomplete = isIncomplete;
  }
}

const state = {
  diagnostics: new Map(),
  definitionProvider: null,
  hoverProvider: null,
  completionProvider: null,
  documentSymbolProvider: null,
  referenceProvider: null,
  documentHighlightProvider: null,
  inlayHintsProvider: null,
  signatureHelpProvider: null,
  foldingRangeProvider: null,
  selectionRangeProvider: null,
  taskProvider: null,
  commands: new Map(),
  statusBar: [],
  output: []
};

const document = {
  languageId: 'kofun',
  version: 1,
  uri: Uri.parse('file:///workspace/smoke.kofun'),
  getText() {
    return [
      'fn identity(value: Int) -> Int {',
      '    return value',
      '}',
      '',
      'fn main() {',
      '    let copy = identity(41)',
      '    print(copy)',
      '}',
      ''
    ].join('\n');
  }
};
function disposable() {
  return { dispose() {} };
}

module.exports = {
  __state: state,
  __document: document,
  Position, Range, Uri, Location, MarkdownString, Hover, Diagnostic,
  CompletionItem, CompletionList, DocumentSymbol, DocumentHighlight, InlayHint,
  SignatureHelp, SignatureInformation, ParameterInformation, FoldingRange,
  SelectionRange, Task, ShellExecution,
  StatusBarAlignment: { Left: 1, Right: 2 },
  TaskScope: { Workspace: 2 },
  FoldingRangeKind: { Comment: 1, Region: 3 },
  tasks: {
    registerTaskProvider(type, provider) {
      state.taskProvider = { type, provider };
      return disposable();
    }
  },
  commands: {
    registerCommand(id, handler) {
      state.commands.set(id, handler);
      return disposable();
    }
  },
  workspace: {
    workspaceFolders: [{ uri: Uri.parse('file:///workspace'), name: 'workspace' }],
    textDocuments: [document],
    getConfiguration() { return { get(_name, fallback) { return fallback; } }; },
    onDidOpenTextDocument: disposable,
    onDidChangeTextDocument: disposable,
    onDidCloseTextDocument: disposable
  },
  languages: {
    createDiagnosticCollection() {
      return {
        set(uri, values) { state.diagnostics.set(uri.toString(), values); },
        delete(uri) { state.diagnostics.delete(uri.toString()); },
        dispose() { state.diagnostics.clear(); }
      };
    },
    registerDefinitionProvider(_language, provider) {
      state.definitionProvider = provider;
      return disposable();
    },
    registerHoverProvider(_language, provider) {
      state.hoverProvider = provider;
      return disposable();
    },
    registerCompletionItemProvider(_language, provider) {
      state.completionProvider = provider;
      return disposable();
    },
    registerDocumentSymbolProvider(_language, provider) {
      state.documentSymbolProvider = provider;
      return disposable();
    },
    registerReferenceProvider(_language, provider) {
      state.referenceProvider = provider;
      return disposable();
    },
    registerDocumentHighlightProvider(_language, provider) {
      state.documentHighlightProvider = provider;
      return disposable();
    },
    registerInlayHintsProvider(_language, provider) {
      state.inlayHintsProvider = provider;
      return disposable();
    },
    registerSignatureHelpProvider(_language, provider) {
      state.signatureHelpProvider = provider;
      return disposable();
    },
    registerFoldingRangeProvider(_language, provider) {
      state.foldingRangeProvider = provider;
      return disposable();
    },
    registerSelectionRangeProvider(_language, provider) {
      state.selectionRangeProvider = provider;
      return disposable();
    }
  },
  window: {
    createOutputChannel() {
      return {
        append(value) { state.output.push(value); },
        appendLine(value) { state.output.push(`${value}\n`); },
        dispose() {}
      };
    },
    showErrorMessage(message) { throw new Error(message); },
    createStatusBarItem(alignment, priority) {
      const item = {
        alignment, priority, text: '', tooltip: '', command: '',
        show() { state.statusBar.push(this.text); }, dispose() {}
      };
      return item;
    }
  }
};
