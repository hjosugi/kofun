import type { Metadata } from "next";

export const metadata: Metadata = {
  title: "Documentation",
  description:
    "Curated Kofun documentation rendered from the repository's verified Markdown sources.",
};

export default function DocsLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return children;
}
