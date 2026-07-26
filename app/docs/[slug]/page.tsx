import { readFile } from "node:fs/promises";
import { posix } from "node:path";
import Link from "next/link";
import { notFound } from "next/navigation";
import ReactMarkdown from "react-markdown";
import rehypeAutolinkHeadings from "rehype-autolink-headings";
import rehypeSlug from "rehype-slug";
import remarkGfm from "remark-gfm";
import DocsNav from "../docs-nav";
import { docBySlug, docBySource, docs } from "../docs-manifest";

const githubBlob = "https://github.com/hjosugi/kofun/blob/main";
const githubRaw = "https://raw.githubusercontent.com/hjosugi/kofun/main";
const githubTree = "https://github.com/hjosugi/kofun/tree/main";
const siteBasePath = process.env.KOFUN_BASE_PATH ?? "";

export const dynamicParams = false;

export function generateStaticParams() {
  return docs.map((entry) => ({ slug: entry.slug }));
}

function rewriteHref(href: string | undefined, source: string) {
  if (!href || /^(?:[a-z]+:|#)/i.test(href)) return href;

  const [rawPath, fragment] = href.split("#", 2);
  const resolved = posix.normalize(
    posix.join(posix.dirname(source), decodeURIComponent(rawPath)),
  );
  const localDoc = docBySource.get(resolved);
  const suffix = fragment ? `#${fragment}` : "";

  if (localDoc) {
    return `${siteBasePath}/docs/${localDoc.slug}/${suffix}`;
  }
  if (rawPath.endsWith("/")) {
    return `${githubTree}/${resolved.replace(/\/$/, "")}${suffix}`;
  }
  return `${githubBlob}/${resolved}${suffix}`;
}

function rewriteImageSrc(src: string | undefined, source: string) {
  if (!src || /^(?:[a-z]+:|data:)/i.test(src)) return src;

  const resolved = posix.normalize(
    posix.join(posix.dirname(source), decodeURIComponent(src)),
  );
  if (resolved.startsWith("public/")) {
    return `${siteBasePath}/${resolved.slice("public/".length)}`;
  }
  return `${githubRaw}/${resolved}`;
}

export default async function DocPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const { slug } = await params;
  const entry = docBySlug.get(slug);
  if (!entry) notFound();

  const markdown = await readFile(
    posix.join(process.cwd(), entry.source),
    "utf8",
  );

  return (
    <main className="docs-layout" id="main-content">
      <DocsNav active={entry.slug} />
      <div className="docs-main">
        <header className="docs-topbar">
          <Link href="/docs">← Documentation</Link>
          <div>
            <Link href="/">Project home</Link>
            <a href={`${githubBlob}/${entry.source}`}>Edit source</a>
          </div>
        </header>

        <article className="markdown-document">
          <div className="markdown-document-meta">
            <span>{entry.section}</span>
            <h1>{entry.title}</h1>
            <p>{entry.summary}</p>
            <a href={`${githubBlob}/${entry.source}`}>{entry.source}</a>
          </div>

          <div className="markdown-body">
            <ReactMarkdown
              rehypePlugins={[
                rehypeSlug,
                [rehypeAutolinkHeadings, { behavior: "wrap" }],
              ]}
              remarkPlugins={[remarkGfm]}
              components={{
                a({ href, ...props }) {
                  return (
                    <a
                      href={rewriteHref(href, entry.source)}
                      {...props}
                    />
                  );
                },
                img({ src, ...props }) {
                  return (
                    <img
                      src={
                        typeof src === "string"
                          ? rewriteImageSrc(src, entry.source)
                          : undefined
                      }
                      {...props}
                    />
                  );
                },
              }}
            >
              {markdown}
            </ReactMarkdown>
          </div>
        </article>
      </div>
    </main>
  );
}
