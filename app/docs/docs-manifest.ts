export type DocEntry = {
  slug: string;
  title: string;
  summary: string;
  source: string;
  section: "Start here" | "Language" | "Compiler" | "Project";
};

export const snapshot = {
  commit: "25a97098f3f06b6132ab61544261209ad84d42eb",
  shortCommit: "25a9709",
  reviewed: "2026-07-24",
};

export const docs: DocEntry[] = [
  {
    slug: "getting-started",
    title: "Getting started",
    summary: "Requirements, the repository launcher, quick start, and active toolchain paths.",
    source: "README.md",
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
    slug: "self-hosting",
    title: "Self-hosting",
    summary: "The frozen profile, typed HIR evidence, runnable compiler generation, and fixed-point boundary.",
    source: "bootstrap/selfhost/README.md",
    section: "Compiler",
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
    slug: "security",
    title: "Security",
    summary: "Threat boundaries, unsupported production claims, and hardening work.",
    source: "docs/SECURITY.md",
    section: "Project",
  },
];

export const docBySlug = new Map(docs.map((entry) => [entry.slug, entry]));
export const docBySource = new Map(docs.map((entry) => [entry.source, entry]));
