import type { NextConfig } from "next";

const staticExport = process.env.KOFUN_STATIC_EXPORT === "1";
const basePath = staticExport
  ? (process.env.KOFUN_BASE_PATH ?? "/kofun")
  : "";

const nextConfig: NextConfig = {
  poweredByHeader: false,
  reactStrictMode: true,
  ...(staticExport
    ? {
        basePath,
        output: "export" as const,
        trailingSlash: true,
      }
    : {}),
};

export default nextConfig;
