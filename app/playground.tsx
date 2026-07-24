"use client";

import { useCallback, useMemo, useState } from "react";
import {
  PLAYGROUND_EXAMPLES,
  runKofun,
  type PlaygroundResult,
} from "./kofun-runtime";

function RunIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="M6.8 4.9v10.2L15 10 6.8 4.9Z" fill="currentColor" />
    </svg>
  );
}

function CopyIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <rect x="6.5" y="6.5" width="9" height="9" rx="1.5" />
      <path d="M4.5 13.5h-1v-9h9v1" />
    </svg>
  );
}

export default function Playground() {
  const firstExample = PLAYGROUND_EXAMPLES[0];
  const [selected, setSelected] = useState(firstExample.id);
  const [source, setSource] = useState(firstExample.source);
  const [result, setResult] = useState<PlaygroundResult>(() =>
    runKofun(firstExample.source),
  );
  const [running, setRunning] = useState(false);
  const [copied, setCopied] = useState(false);

  const lineCount = useMemo(
    () => Math.max(1, source.split("\n").length),
    [source],
  );

  const execute = useCallback(() => {
    setRunning(true);
    window.requestAnimationFrame(() => {
      setResult(runKofun(source));
      setRunning(false);
    });
  }, [source]);

  const chooseExample = (id: string) => {
    const example = PLAYGROUND_EXAMPLES.find((item) => item.id === id);
    if (!example) return;
    setSelected(id);
    setSource(example.source);
    setResult(runKofun(example.source));
  };

  const copySource = async () => {
    await navigator.clipboard.writeText(source);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1200);
  };

  return (
    <div className="playground-shell">
      <div className="playground-topbar">
        <div className="window-dots" aria-hidden="true">
          <span />
          <span />
          <span />
        </div>
        <div className="playground-title">
          <span>play.kofun</span>
          <span className="browser-badge">browser subset</span>
        </div>
        <button
          className="run-button"
          type="button"
          onClick={execute}
          disabled={running}
        >
          <RunIcon />
          {running ? "Running" : "Run"}
          <kbd>⌘ ↵</kbd>
        </button>
      </div>

      <div className="example-rail" aria-label="Playground examples">
        {PLAYGROUND_EXAMPLES.map((example) => (
          <button
            key={example.id}
            type="button"
            className={selected === example.id ? "active" : ""}
            onClick={() => chooseExample(example.id)}
            title={example.description}
          >
            {example.name}
          </button>
        ))}
      </div>

      <div className="playground-grid">
        <div className="editor-pane">
          <div className="pane-label">
            <span>main.kofun</span>
            <button type="button" onClick={copySource}>
              <CopyIcon />
              {copied ? "Copied" : "Copy"}
            </button>
          </div>
          <div className="editor-wrap">
            <div className="line-numbers" aria-hidden="true">
              {Array.from({ length: lineCount }, (_, index) => (
                <span key={index}>{index + 1}</span>
              ))}
            </div>
            <textarea
              aria-label="Kofun source editor"
              value={source}
              onChange={(event) => {
                setSource(event.target.value);
                setSelected("");
              }}
              onKeyDown={(event) => {
                if (
                  event.key === "Enter" &&
                  (event.metaKey || event.ctrlKey)
                ) {
                  event.preventDefault();
                  execute();
                }
                if (event.key === "Tab") {
                  event.preventDefault();
                  const target = event.currentTarget;
                  const start = target.selectionStart;
                  const end = target.selectionEnd;
                  const next = `${source.slice(0, start)}    ${source.slice(end)}`;
                  setSource(next);
                  window.requestAnimationFrame(() => {
                    target.selectionStart = target.selectionEnd = start + 4;
                  });
                }
              }}
              spellCheck={false}
              autoCapitalize="off"
              autoComplete="off"
            />
          </div>
        </div>

        <div className="output-pane" aria-live="polite">
          <div className="pane-label">
            <span>Output</span>
            <span className={result.error ? "status error" : "status"}>
              <i />
              {result.error ? "diagnostic" : "finished"}
            </span>
          </div>
          <div className="output-body">
            {result.error ? (
              <div className="diagnostic">
                <div className="diagnostic-code">{result.error.code}</div>
                <strong>{result.error.message}</strong>
                <span>
                  main.kofun:{result.error.line}:{result.error.column}
                </span>
              </div>
            ) : (
              <pre>{result.output || "Program finished without output."}</pre>
            )}
          </div>
          <div className="output-meta">
            <span>{result.tokenCount} tokens</span>
            <span>{result.steps} steps</span>
            <span>{result.durationMs.toFixed(2)} ms</span>
          </div>
          <div className="subset-note">
            <span>Honest boundary</span>
            This runner implements a safe learning subset in TypeScript. The
            repository CLI remains the source of truth for ownership, laws, and
            native ELF builds.
          </div>
        </div>
      </div>
    </div>
  );
}
