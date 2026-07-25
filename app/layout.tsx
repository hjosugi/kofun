import type { Metadata } from "next";
import "./globals.css";

const siteBasePath = process.env.KOFUN_BASE_PATH ?? "";

export const metadata: Metadata = {
  title: {
    default: "Kofun — Clear code, native ground",
    template: "%s · Kofun",
  },
  description:
    "Kofun is an experimental Kofun-written language with a Python-free bootstrap and bounded direct x86-64/AArch64 ELF backends.",
  keywords: [
    "Kofun",
    "programming language",
    "ownership",
    "functional programming",
    "native compiler",
    "x86-64",
    "AArch64",
  ],
  icons: {
    icon: `${siteBasePath}/kofun-mark.svg`,
  },
  openGraph: {
    title: "Kofun — Clear code, native ground",
    description:
      "A research language with executable bootstrap evidence and bounded direct static ELF checkpoints.",
    type: "website",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
