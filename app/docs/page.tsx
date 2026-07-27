import Link from "next/link";
import DocsNav from "./docs-nav";
import { docs, snapshot } from "./docs-manifest";

const trackDescriptions: Record<number, string> = {
  650: "Publish an honest, navigable documentation surface from repository sources.",
  666: "Make backend conformance coverage explicit and execute numeric cases on direct x86-64 and AArch64.",
  667: "Connect every stable active diagnostic code to executable evidence and deterministic policy.",
  668: "Compare declared backends through normalized observations and an independent family oracle.",
};

const contributorPath = [
  {
    step: "01",
    title: "Set up and run Kofun",
    description:
      "Install the small toolchain, use the repository launcher, and get a checked program running.",
    href: "/docs/getting-started",
    action: "Start setup",
  },
  {
    step: "02",
    title: "Learn where work belongs",
    description:
      "Trace commands through compiler stages, specifications, tests, libraries, tooling, and the site.",
    href: "/docs/repository-guide",
    action: "Explore the repository",
  },
  {
    step: "03",
    title: "Make a verified change",
    description:
      "Follow the recipe for your subsystem, select the right gate, and prepare a reviewable patch.",
    href: "/docs/contributing",
    action: "Read the workflow",
  },
  {
    step: "04",
    title: "Check the real boundary",
    description:
      "Separate active compiler evidence from focused checkpoints, design direction, and planned work.",
    href: "/docs/implemented-status",
    action: "Check implementation",
  },
] as const;

export default function DocsHome() {
  return (
    <main className="docs-layout" id="main-content">
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
            <h1>Start here.<br />Ship with evidence.</h1>
            <p>
              New to Kofun? Follow the contributor path below to run the
              compiler, understand the repository, and make a checked change
              without guessing which source or test owns the behavior. The
              tracker snapshot observes main commit{" "}
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

          <section className="docs-onboarding" aria-labelledby="contributor-path">
            <div className="docs-section-title">
              <span>01</span>
              <div>
                <h2 id="contributor-path">Your first day, in order</h2>
                <p>
                  Four short guides take you from checkout to a reviewable
                  contribution.
                </p>
              </div>
            </div>
            <div className="docs-onboarding-grid">
              {contributorPath.map((item) => (
                <Link href={item.href} key={item.step}>
                  <span>{item.step}</span>
                  <div>
                    <h3>{item.title}</h3>
                    <p>{item.description}</p>
                  </div>
                  <b>{item.action} →</b>
                </Link>
              ))}
            </div>
          </section>

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
              <span>02</span>
              <div>
                <h2>Curated guides</h2>
                <p>
                  Authoritative repository Markdown, rendered with status
                  qualifiers and local links preserved.
                </p>
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
              <span>03</span>
              <div>
                <h2>Evidence tracks</h2>
                <p>
                  Tracker states observed on {snapshot.reviewed}; issue state
                  may change between site versions.
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
