import type { Metadata } from "next";
import "./globals.css";

const siteBasePath = process.env.KOFUN_BASE_PATH ?? "";

export const metadata: Metadata = {
  title: {
    default: "Kofun — Clear code, native ground",
    template: "%s · Kofun",
  },
  description:
    "Kofun is an experimental programming language combining low-sigil ownership, functional programming, scientific computing, and direct native code generation.",
  keywords: [
    "Kofun",
    "programming language",
    "ownership",
    "functional programming",
    "native compiler",
    "x86-64",
  ],
  icons: {
    icon: `${siteBasePath}/kofun-mark.svg`,
  },
  openGraph: {
    title: "Kofun — Clear code, native ground",
    description:
      "Memory-aware, functional by default, and capable of emitting static native binaries directly.",
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
