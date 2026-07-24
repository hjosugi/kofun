import Link from "next/link";
import DocsNav from "./docs-nav";
import { docs, snapshot } from "./docs-manifest";

const trackDescriptions: Record<number, string> = {
  650: "Publish an honest, navigable documentation surface from repository sources.",
  666: "Make backend conformance coverage explicit and execute numeric cases on direct x86-64.",
  667: "Connect every stable active diagnostic code to executable evidence and deterministic policy.",
  668: "Compare declared backends through normalized observations and an independent family oracle.",
};

export default function DocsHome() {
  return (
    <main className="docs-layout">
      <DocsNav />
      <div className="docs-main">
        <header className="docs-topbar">
          <Link href="/">← Project home</Link>
          <div>
            <a href="https://github.com/hjosugi/kofun">GitHub</a>
            <a href="https://github.com/hjosugi/kofun/issues">Issues</a>
          </div>
        </header>

        <div className="docs-overview">
          <div className="docs-overview-hero">
            <span className="section-kicker">Kofun docs</span>
            <h1>Evidence first.<br />Ambition intact.</h1>
            <p>
              The tracker snapshot observes main commit{" "}
              <a
                href={`https://github.com/hjosugi/kofun/commit/${snapshot.commit}`}
              >
                {snapshot.shortCommit}
              </a>
              . Its full{" "}
              <a href={snapshot.verification.url}>
                {snapshot.verification.workflow} verification
              </a>{" "}
              is{" "}
              <strong>
                {snapshot.verification.conclusion ??
                  snapshot.verification.status}
              </strong>
              . “Active” means an executable gate exists; design documents and
              open issues never become implementation claims by implication.
            </p>
          </div>

          <div className="docs-honesty">
            <span>Current boundary</span>
            <strong>Research compiler, not a production language.</strong>
            <p>
              The frozen compiler profile now reaches a runnable
              compiler-produced compiler through the driver. The required
              three-generation semantic fixed point remains open.
            </p>
            <Link href="/docs/implemented-status">
              Read the capability matrix →
            </Link>
          </div>

          <section className="docs-card-section">
            <div className="docs-section-title">
              <span>01</span>
              <div>
                <h2>Curated guides</h2>
                <p>Repository Markdown, with its status qualifiers preserved.</p>
              </div>
            </div>
            <div className="docs-card-grid">
              {docs.map((entry) => (
                <Link
                  className="docs-card"
                  href={`/docs/${entry.slug}`}
                  key={entry.slug}
                >
                  <span>{entry.section}</span>
                  <h3>{entry.title}</h3>
                  <p>{entry.summary}</p>
                  <b>Read document →</b>
                </Link>
              ))}
            </div>
          </section>

          <section className="docs-card-section">
            <div className="docs-section-title">
              <span>02</span>
              <div>
                <h2>Next evidence tracks</h2>
                <p>
                  Ready issues reviewed on {snapshot.reviewed}; issue state may
                  change between site versions.
                </p>
              </div>
            </div>
            <div className="issue-track-grid">
              {snapshot.issues.map((issue) => (
                <a
                  href={issue.url}
                  key={issue.number}
                >
                  <span>
                    Issue #{issue.number} · {issue.workflow}
                  </span>
                  <h3>{issue.title}</h3>
                  <p>{trackDescriptions[issue.number]}</p>
                </a>
              ))}
            </div>
          </section>
        </div>
      </div>
    </main>
  );
}
