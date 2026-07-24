import Link from "next/link";
import { docs, snapshot } from "./docs-manifest";

const sections = ["Start here", "Language", "Compiler", "Project"] as const;

export default function DocsNav({ active }: { active?: string }) {
  return (
    <aside className="docs-sidebar">
      <Link className="docs-wordmark" href="/">
        <span className="docs-wordmark-icon">K</span>
        <span>
          <strong>Kofun</strong>
          <small>Documentation</small>
        </span>
      </Link>

      <nav aria-label="Documentation">
        <Link className={!active ? "active" : ""} href="/docs">
          Overview
        </Link>
        {sections.map((section) => (
          <div className="docs-nav-section" key={section}>
            <span>{section}</span>
            {docs
              .filter((entry) => entry.section === section)
              .map((entry) => (
                <Link
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

      <div className="docs-snapshot">
        <span>Verified source</span>
        <a
          href={`https://github.com/hjosugi/kofun/commit/${snapshot.commit}`}
        >
          {snapshot.shortCommit}
        </a>
        <small>Reviewed {snapshot.reviewed}</small>
      </div>
    </aside>
  );
}
