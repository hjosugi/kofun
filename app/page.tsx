import Playground from "./playground";

const githubUrl = "https://github.com/hjosugi/kofun";

function ArrowIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="M4 10h11M11 6l4 4-4 4" />
    </svg>
  );
}

function GithubIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path
        fill="currentColor"
        d="M12 .7a11.5 11.5 0 0 0-3.64 22.4c.58.1.79-.25.79-.56v-2.23c-3.22.7-3.9-1.37-3.9-1.37-.52-1.34-1.28-1.7-1.28-1.7-1.05-.72.08-.7.08-.7 1.16.08 1.77 1.19 1.77 1.19 1.03 1.77 2.7 1.26 3.36.96.1-.75.4-1.26.73-1.55-2.57-.29-5.27-1.29-5.27-5.73 0-1.27.45-2.3 1.19-3.11-.12-.3-.52-1.48.11-3.07 0 0 .97-.31 3.16 1.19a10.9 10.9 0 0 1 5.76 0c2.2-1.5 3.16-1.19 3.16-1.19.63 1.59.23 2.78.11 3.07.74.81 1.19 1.84 1.19 3.1 0 4.46-2.71 5.44-5.29 5.73.42.36.79 1.07.79 2.16v3.2c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .7Z"
      />
    </svg>
  );
}

function KofunMark({ compact = false }: { compact?: boolean }) {
  return (
    <span className={compact ? "kofun-mark compact" : "kofun-mark"}>
      <i />
      <b />
    </span>
  );
}

const principles = [
  {
    number: "01",
    title: "Ownership you can read",
    body: "Use read, edit, and take where resources matter. Copy values stay lightweight; affine resources make views and transfers explicit.",
    code: "fn upload(take file: File)",
  },
  {
    number: "02",
    title: "Functional, not ceremonial",
    body: "Immutable data, typed lambdas, and pipelines are the default. Local mutation remains available for algorithms.",
    code: "values |> filter(keep) |> sum()",
  },
  {
    number: "03",
    title: "Native from first principles",
    body: "The direct backend writes static ELF and machine code itself—without routing through C, an assembler, or a linker.",
    code: "source → Core → ELF64",
  },
];

const workingNow = [
  "Python-free bootstrap with frozen self-host profile",
  "Complete typed HIR evidence for the frozen compiler source",
  "Runnable compiler-produced compiler through the driver",
  "Direct x86-64 and AArch64 ELF for bounded Int, List, and Text",
  "Wasm32 arithmetic Core and no-install browser tour",
  "C ABI plus bounded HTTP, CLI, TUI, and syscall paths",
];

const openWork = [
  "Three-generation semantic self-hosting fixed point",
  "General type, ownership, effect, and law checking",
  "General native lowering beyond the checked Core profiles",
  "Stable specification, production hardening, and ecosystem",
];

export default function Home() {
  return (
    <main>
      <nav className="nav">
        <a className="brand" href="#top" aria-label="Kofun home">
          <KofunMark compact />
          <span>Kofun</span>
          <sup>experimental</sup>
        </a>
        <div className="nav-links">
          <a href="#language">Language</a>
          <a href="#playground">Playground</a>
          <a href="#status">Status</a>
          <a href="/docs">Docs</a>
          <a className="nav-github" href={githubUrl}>
            <GithubIcon />
            GitHub
          </a>
        </div>
      </nav>

      <section className="hero" id="top">
        <div className="hero-copy">
          <div className="eyebrow">
            <span />
            A programming language in active construction
          </div>
          <h1>
            Clear code.
            <br />
            <em>Native ground.</em>
          </h1>
          <p>
            Kofun explores low-sigil ownership, functional composition, and
            scientific fluency in one language—while keeping every active
            compiler claim tied to executable evidence.
          </p>
          <div className="hero-actions">
            <a className="primary-button" href="/docs">
              Browse documentation <ArrowIcon />
            </a>
            <a className="text-button" href="#playground">
              Try the playground
            </a>
          </div>
        </div>

        <div className="hero-art" aria-label="Kofun language mark">
          <div className="orbit orbit-one" />
          <div className="orbit orbit-two" />
          <div className="hero-mark">
            <KofunMark />
          </div>
          <div className="art-label label-one">
            <span>read</span>
            non-owning view
          </div>
          <div className="art-label label-two">
            <span>edit</span>
            exclusive view
          </div>
          <div className="art-label label-three">
            <span>take</span>
            ownership transfer
          </div>
        </div>
      </section>

      <div className="signal-strip" aria-label="Current Kofun capabilities">
        <span>Python-free bootstrap</span>
        <i />
        <span>static ELF</span>
        <i />
        <span>read · edit · take</span>
        <i />
        <span>typed HIR v1</span>
        <i />
        <span>C ABI</span>
      </div>

      <section className="principles section" id="language">
        <div className="section-heading">
          <div>
            <span className="section-kicker">Language</span>
            <h2>Less punctuation.<br />More intent.</h2>
          </div>
          <p>
            Kofun is designed so a small program stays small, while deeper
            safety and type machinery remains available when the problem asks
            for it.
          </p>
        </div>
        <div className="principle-grid">
          {principles.map((principle) => (
            <article key={principle.number} className="principle-card">
              <span className="card-number">{principle.number}</span>
              <div className="mini-mark" aria-hidden="true">
                <span />
                <i />
              </div>
              <h3>{principle.title}</h3>
              <p>{principle.body}</p>
              <code>{principle.code}</code>
            </article>
          ))}
        </div>
      </section>

      <section className="playground-section section" id="playground">
        <div className="section-heading playground-heading">
          <div>
            <span className="section-kicker light">Playground</span>
            <h2>Touch the syntax.<br />See the idea run.</h2>
          </div>
          <p>
            Edit a program and press Run. The safe browser evaluator covers the
            learning subset shown here; native builds and ownership checks live
            in the repository CLI.
          </p>
        </div>
        <Playground />
      </section>

      <section className="toolchain section">
        <div className="toolchain-intro">
          <span className="section-kicker">Toolchain</span>
          <h2>A straight line<br />to the metal.</h2>
          <p>
            Kofun keeps the compiler path visible. Today&apos;s checked
            profiles are deliberately bounded—and every supported path has an
            executable gate.
          </p>
          <a href={`${githubUrl}/tree/main/bootstrap/native`}>
            Inspect the native backend <ArrowIcon />
          </a>
        </div>
        <div className="compiler-flow" aria-label="Compiler pipeline">
          <div className="flow-node">
            <span>01</span>
            <strong>Source</strong>
            <code>.kofun</code>
          </div>
          <div className="flow-line"><i /></div>
          <div className="flow-node">
            <span>02</span>
            <strong>Typed HIR</strong>
            <code>versioned evidence</code>
          </div>
          <div className="flow-line"><i /></div>
          <div className="flow-node accent">
            <span>03</span>
            <strong>Checked output</strong>
            <code>C11 · ELF64 · wasm32</code>
          </div>
        </div>
      </section>

      <section className="status-section section" id="status">
        <div className="status-title">
          <span className="section-kicker">Project status</span>
          <h2>Ambitious,<br />and explicit about the gaps.</h2>
          <p>
            Kofun is a working research compiler, not a production language.
            That distinction is part of the design discipline.
          </p>
        </div>
        <div className="status-board">
          <div className="status-column">
            <div className="status-column-title">
              <span className="status-dot done" />
              Working now
            </div>
            <ul>
              {workingNow.map((item) => (
                <li key={item}><span>✓</span>{item}</li>
              ))}
            </ul>
          </div>
          <div className="status-column open">
            <div className="status-column-title">
              <span className="status-dot" />
              Open by design
            </div>
            <ul>
              {openWork.map((item) => (
                <li key={item}><span>→</span>{item}</li>
              ))}
            </ul>
          </div>
        </div>
      </section>

      <section className="quickstart section">
        <div className="quickstart-copy">
          <span className="section-kicker light">Start local</span>
          <h2>From checkout<br />to native in seconds.</h2>
          <p>
            No package installation is required for the repository launcher.
            A C11 compiler is required for the audited seed and host-C paths.
          </p>
        </div>
        <div className="terminal">
          <div className="terminal-head">
            <div className="window-dots"><span /><span /><span /></div>
            <span>terminal</span>
          </div>
          <pre>
            <span className="prompt">$</span> git clone {githubUrl}.git{"\n"}
            <span className="prompt">$</span> cd kofun{"\n"}
            <span className="prompt">$</span> ./bin/kofun run bootstrap/fixtures/answer.kofun{"\n"}
            <span className="terminal-output">42</span>{"\n\n"}
            <span className="prompt">$</span> ./bin/kofun build bootstrap/fixtures/answer.kofun \{"\n"}
            {"    "}-o build/answer{"\n"}
            <span className="terminal-output">build/answer</span>
          </pre>
        </div>
      </section>

      <footer>
        <div className="footer-brand">
          <KofunMark compact />
          <div>
            <strong>Kofun</strong>
            <span>Clear code, native ground.</span>
          </div>
        </div>
        <div className="footer-links">
          <a href={githubUrl}>Repository</a>
          <a href="/docs">Documentation</a>
          <a href={`${githubUrl}/issues`}>Issues</a>
          <a href={`${githubUrl}/blob/main/LICENSE-MIT`}>License</a>
        </div>
        <div className="footer-note">
          Experimental software · Apache-2.0 OR MIT
        </div>
      </footer>
    </main>
  );
}
