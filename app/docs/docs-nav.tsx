import Link from "next/link";
import KofunMark from "../kofun-mark";
import { docs, snapshot } from "./docs-manifest";

const sections = [
  "Start here",
  "Language",
  "Compiler",
  "Contribute",
  "Project",
] as const;

export default function DocsNav({ active }: { active?: string }) {
  return (
    <aside className="docs-sidebar">
      <Link className="docs-wordmark" href="/">
        <KofunMark compact />
        <span>
          <strong>Kofun</strong>
          <small>Documentation</small>
        </span>
      </Link>

      <nav aria-label="Documentation">
        <Link
          aria-current={!active ? "page" : undefined}
          className={!active ? "active" : ""}
          href="/docs"
        >
          Overview
        </Link>
        {sections.map((section) => (
          <div className="docs-nav-section" key={section}>
            <span>{section}</span>
            {docs
              .filter((entry) => entry.section === section)
              .map((entry) => (
                <Link
                  aria-current={active === entry.slug ? "page" : undefined}
                  className={active === entry.slug ? "active" : ""}
                  href={`/docs/${entry.slug}`}
                  key={entry.slug}
                >
                  {entry.title}
                </Link>
              ))}
          </div>
        ))}
      </nav>

      <details className="docs-mobile-menu">
        <summary>Browse all guides</summary>
        <div>
          {docs.map((entry) => (
            <Link
              aria-current={active === entry.slug ? "page" : undefined}
              className={active === entry.slug ? "active" : ""}
              href={`/docs/${entry.slug}`}
              key={entry.slug}
            >
              <span>{entry.section}</span>
              {entry.title}
            </Link>
          ))}
        </div>
      </details>

      <div className="docs-snapshot">
        <span>Observed main</span>
        <a
          href={`https://github.com/hjosugi/kofun/commit/${snapshot.commit}`}
        >
          {snapshot.shortCommit}
        </a>
        <small>Snapshot {snapshot.reviewed}</small>
      </div>
    </aside>
  );
}
