import type { Metadata } from "next";

export const metadata: Metadata = {
  title: "Documentation",
  description:
    "Curated Kofun documentation rendered from checked-in Markdown sources.",
};

export default function DocsLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return children;
}
