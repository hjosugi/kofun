import type { Metadata } from "next";
import Link from "next/link";
import KofunMark from "../kofun-mark";
import planSnapshot from "./plan-snapshot.json";
import RoadmapClient, { type RoadmapSnapshot } from "./roadmap-client";
import "./roadmap.css";

export const metadata: Metadata = {
  title: "開発ロードマップ",
  description:
    "GitHub Issuesを4エージェント体制へ割り当てた、Kofunの見通し・ガントチャート・月間カレンダー。",
};

const roadmapSnapshot: RoadmapSnapshot = {
  schema: planSnapshot.schema,
  repository: planSnapshot.repository,
  as_of: planSnapshot.as_of,
  generated_at: planSnapshot.generated_at,
  time_zone: planSnapshot.time_zone,
  source: planSnapshot.source,
  capacity: planSnapshot.capacity,
  summary: planSnapshot.summary,
  throughput: planSnapshot.throughput,
  forecast: planSnapshot.forecast,
  lanes: planSnapshot.lanes,
  dependency_chains: planSnapshot.dependency_chains.map((chain) => ({
    id: chain.id,
    title: chain.title,
    issue_numbers: chain.issue_numbers,
  })),
  schedule: planSnapshot.schedule,
  unscheduled: planSnapshot.unscheduled,
  calendar: planSnapshot.calendar,
};

function GithubIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path
        fill="currentColor"
        d="M12 .7a11.5 11.5 0 0 0-3.64 22.4c.58.1.79-.25.79-.56v-2.23c-3.22.7-3.9-1.37-3.9-1.37-.52-1.34-1.28-1.7-1.28-1.7-1.05-.72.08-.7.08-.7 1.16.08 1.77 1.19 1.77 1.19 1.03 1.77 2.7 1.26 3.36.96.1-.75.4-1.26.73-1.55-2.57-.29-5.27-1.29-5.27-5.73 0-1.27.45-2.3 1.19-3.11-.12-.3-.52-1.48.11-3.07 0 0 .97-.31 3.16 1.19a10.9 10.9 0 0 1 5.76 0c2.2-1.5 3.16-1.19 3.16-1.19.63 1.59.23 2.78.11 3.07.74.81 1.19 1.84 1.19 3.1 0 4.46-2.71 5.44-5.29 5.73.42.36.79 1.07.79 2.16v3.2c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .7Z"
      />
    </svg>
  );
}

export default function RoadmapPage() {
  return (
    <main className="roadmap-page" id="main-content" lang="ja">
      <nav className="roadmap-nav" aria-label="ロードマップ">
        <Link className="roadmap-brand" href="/" aria-label="Kofun ホーム">
          <KofunMark compact />
          <span>Kofun</span>
          <sup>roadmap</sup>
        </Link>
        <div className="roadmap-nav-links">
          <a href="#timeline">予定表</a>
          <a href="#calendar">カレンダー</a>
          <a href="#notes">前提</a>
          <a
            className="roadmap-github"
            href="https://github.com/hjosugi/kofun/issues"
          >
            <GithubIcon />
            <span>Issues</span>
          </a>
        </div>
      </nav>

      <RoadmapClient snapshot={roadmapSnapshot} />
    </main>
  );
}
