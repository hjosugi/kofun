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

  if (localDoc) return `/docs/${localDoc.slug}${suffix}`;
  return `${githubBlob}/${resolved}${suffix}`;
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
    <main className="docs-layout">
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
