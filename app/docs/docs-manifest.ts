import statusSnapshot from "./status-snapshot.json" with { type: "json" };

export type DocEntry = {
  slug: string;
  title: string;
  summary: string;
  source: string;
  section:
    | "Start here"
    | "Language"
    | "Compiler"
    | "Contribute"
    | "Project";
};

export const snapshot = {
  commit: statusSnapshot.source_commit,
  shortCommit: statusSnapshot.source_commit.slice(0, 7),
  reviewed: statusSnapshot.reviewed_at.slice(0, 10),
  verification: statusSnapshot.verification,
  issues: statusSnapshot.issues,
};

export const docs: DocEntry[] = [
  {
    slug: "getting-started",
    title: "Getting started",
    summary: "From a clean machine to a checked compiler run and local documentation site.",
    source: "docs/GETTING_STARTED.md",
    section: "Start here",
  },
  {
    slug: "repository-guide",
    title: "Repository guide",
    summary: "A task-oriented map of every top-level area, compiler path, test family, and generated boundary.",
    source: "docs/REPOSITORY_GUIDE.md",
    section: "Start here",
  },
  {
    slug: "implemented-status",
    title: "Implemented status",
    summary: "The concise capability matrix. Active claims require an executable gate.",
    source: "docs/MVP_IMPLEMENTED.md",
    section: "Start here",
  },
  {
    slug: "language-vision",
    title: "Language vision",
    summary: "The intended audience, product direction, values, and explicit non-goals.",
    source: "docs/LANGUAGE_VISION.md",
    section: "Language",
  },
  {
    slug: "syntax",
    title: "Syntax guide",
    summary: "Current syntax direction and the boundary between accepted Core and planned language.",
    source: "docs/SYNTAX.md",
    section: "Language",
  },
  {
    slug: "type-system",
    title: "Type system",
    summary: "Static typing direction, generics, ownership interaction, and current limits.",
    source: "docs/TYPE_SYSTEM.md",
    section: "Language",
  },
  {
    slug: "decimal",
    title: "Decimal design",
    summary: "Exact base-10 representation, literals, rounding, fixed point, and law evidence.",
    source: "docs/DECIMAL.md",
    section: "Language",
  },
  {
    slug: "memory-model",
    title: "Memory model",
    summary: "The read/edit/take ownership model and the distinction between design and active checks.",
    source: "docs/MEMORY_MODEL.md",
    section: "Language",
  },
  {
    slug: "native-backends",
    title: "Native backends",
    summary: "Direct x86-64 and AArch64 ELF checkpoints, debug metadata, and bounded profiles.",
    source: "docs/NATIVE_BACKEND.md",
    section: "Compiler",
  },
  {
    slug: "compiler-architecture",
    title: "Compiler architecture",
    summary: "Bootstrap layers, frontend artifacts, backends, and the trust boundary.",
    source: "docs/COMPILER_ARCHITECTURE.md",
    section: "Compiler",
  },
  {
    slug: "developer-discovery",
    title: "Developer discovery",
    summary: "One compiler-backed contract for inferred types, callable operations, and safe explanations.",
    source: "docs/DEVELOPER_DISCOVERY.md",
    section: "Compiler",
  },
  {
    slug: "self-hosting",
    title: "Self-hosting",
    summary: "The frozen profile, typed HIR evidence, runnable compiler generation, and fixed-point boundary.",
    source: "bootstrap/selfhost/README.md",
    section: "Compiler",
  },
  {
    slug: "contributing",
    title: "Contributing",
    summary: "Change recipes, test selection, repository conventions, review expectations, and definition of done.",
    source: "docs/CONTRIBUTING.md",
    section: "Contribute",
  },
  {
    slug: "specification",
    title: "Specification",
    summary: "Normative contracts, roadmap documents, and executable conformance evidence.",
    source: "spec/README.md",
    section: "Project",
  },
  {
    slug: "roadmap",
    title: "Roadmap",
    summary: "Milestone outcomes and the evidence required to advance project status.",
    source: "docs/ROADMAP.md",
    section: "Project",
  },
  {
    slug: "delivery-plan",
    title: "Delivery plan",
    summary: "Generated four-agent schedule, completion forecast, readiness risks, and dependency lanes.",
    source: "docs/DELIVERY_PLAN.md",
    section: "Project",
  },
  {
    slug: "security",
    title: "Security",
    summary: "Threat boundaries, unsupported production claims, and hardening work.",
    source: "docs/SECURITY.md",
    section: "Project",
  },
  {
    slug: "release-evidence",
    title: "Release evidence",
    summary: "The machine-checked join between published capability claims and the gates, boundaries, and reproduction commands that bound them.",
    source: "docs/RELEASE_EVIDENCE.md",
    section: "Project",
  },
  {
    slug: "issue-progress",
    title: "Issue progress",
    summary: "A generated snapshot of selected documentation and executable-evidence tracks.",
    source: "docs/ISSUE_PROGRESS.md",
    section: "Project",
  },
];

export const docBySlug = new Map(docs.map((entry) => [entry.slug, entry]));
export const docBySource = new Map(docs.map((entry) => [entry.source, entry]));
