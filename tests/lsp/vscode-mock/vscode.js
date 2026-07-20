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

const state = {
  diagnostics: new Map(),
  definitionProvider: null,
  hoverProvider: null,
  output: []
};

const document = {
  languageId: 'kofun',
  version: 1,
  uri: Uri.parse('file:///workspace/smoke.kofun'),
  getText() {
    return [
      'fn identity(read value: Int) -> Int {',
      '    let copy = value',
      '    return copy',
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
    showErrorMessage(message) { throw new Error(message); }
  }
};
