"use client";

import {
  type CSSProperties,
  type ReactNode,
  useMemo,
  useState,
} from "react";

type CountMap = Record<string, number>;

export interface RoadmapLane {
  id: "writer-a" | "writer-b" | "writer-c" | "review" | string;
  label: string;
  role: string;
  capacity: number;
}

export interface ScheduledIssue {
  number: number;
  title: string;
  url: string;
  role?: string | null;
  workflow?: string | null;
  state_reason?: string | null;
  priority?: string | null;
  size?: string | null;
  kind?: string | null;
  area?: string | null;
  labels?: string[];
  lane?: string | null;
  start_date?: string | null;
  end_date?: string | null;
  writer_end_date?: string | null;
  review_start_date?: string | null;
  review_end_date?: string | null;
  depends_on?: number[];
  schedule_status?: string;
  confidence?: number | string;
}

interface ForecastScenario {
  id: string;
  label: string;
  writer_slots: number | null;
  reviewer_slots: number | null;
  finish_date: string | null;
  calendar_days: number | null;
  note?: string;
}

interface Forecast {
  scope?: string;
  scheduled_count?: number;
  unscheduled_count?: number;
  start_date?: string;
  likely_finish?: string;
  conservative_finish?: string;
  confidence?: number | string;
  scenarios?: ForecastScenario[];
  assumptions?: string[];
}

interface Summary {
  issues_total?: number;
  open_issues?: number;
  closed_issues?: number;
  open_curated?: number;
  open_planning?: number;
  scheduled_curated?: number;
  unscheduled_curated?: number;
  all?: number;
  open?: number;
  closed?: number;
  prs?: number;
  pull_requests?: number;
  open_roles?: CountMap;
  curated_workflows?: CountMap;
  curated_priorities?: CountMap;
  curated_sizes?: CountMap;
  workflow?: CountMap;
  priority?: CountMap;
  size?: CountMap;
  state_reason?: CountMap;
}

interface Throughput {
  [key: string]: unknown;
}

interface DependencyChain {
  id?: string;
  title?: string;
  label?: string;
  issue_numbers?: number[];
  issues?: number[];
  critical?: boolean;
  status?: string;
}

interface CalendarWeek {
  week_start: string;
  week_end: string;
  active_issue_numbers?: number[];
  starting_issue_numbers?: number[];
  finishing_issue_numbers?: number[];
  review_issue_numbers?: number[];
  planned_writer_days?: number;
  writer_capacity_days?: number;
}

export interface RoadmapSnapshot {
  schema?: string | number;
  repository?: string;
  as_of?: string;
  generated_at?: string;
  time_zone?: string;
  source?: string | Record<string, unknown>;
  capacity?: number | Record<string, unknown>;
  summary?: Summary;
  throughput?: Throughput;
  forecast?: Forecast;
  lanes?: RoadmapLane[];
  dependency_chains?: DependencyChain[];
  schedule?: ScheduledIssue[];
  unscheduled?: ScheduledIssue[] | number[];
  calendar?: CalendarWeek[];
}

type FilterState = {
  status: string;
  priority: string;
  lane: string;
};

type CalendarCell = {
  date: Date;
  iso: string;
  inMonth: boolean;
  issues: ScheduledIssue[];
  starting: number;
  finishing: number;
};

const defaultLanes: RoadmapLane[] = [
  { id: "writer-a", label: "Agent A", role: "実装", capacity: 1 },
  { id: "writer-b", label: "Agent B", role: "実装", capacity: 1 },
  { id: "writer-c", label: "Agent C", role: "実装", capacity: 1 },
  { id: "review", label: "Agent D", role: "レビュー・統合", capacity: 1 },
];

const dayMs = 86_400_000;
const weekdays = ["月", "火", "水", "木", "金", "土", "日"];
const githubRepository = "https://github.com/hjosugi/kofun";

function validDate(value?: string | null): Date | null {
  if (!value) return null;
  const date = new Date(
    /^\d{4}-\d{2}-\d{2}$/.test(value) ? `${value}T00:00:00Z` : value,
  );
  return Number.isNaN(date.getTime()) ? null : date;
}

function isoDate(date: Date): string {
  return date.toISOString().slice(0, 10);
}

function addDays(date: Date, days: number): Date {
  return new Date(date.getTime() + days * dayMs);
}

function diffDays(from: Date, to: Date): number {
  return Math.round((to.getTime() - from.getTime()) / dayMs);
}

function startOfWeek(date: Date): Date {
  const day = date.getUTCDay();
  return addDays(date, -(day === 0 ? 6 : day - 1));
}

function formatDate(
  value?: string | null,
  options: Intl.DateTimeFormatOptions = {
    month: "short",
    day: "numeric",
  },
): string {
  const date = validDate(value);
  if (!date) return "未定";
  return new Intl.DateTimeFormat("ja-JP", {
    timeZone: "Asia/Tokyo",
    ...options,
  }).format(date);
}

function displayText(value?: string): string {
  if (!value) return "未設定";
  const translations: Record<string, string> = {
    all: "すべて",
    blocked: "ブロック中",
    deferred: "保留",
    done: "完了",
    "in-progress": "進行中",
    "in_progress": "進行中",
    ready: "着手可能",
    review: "レビュー",
    scheduled: "予定済み",
    unscheduled: "未割当",
    unknown: "未設定",
    urgent: "最優先",
    high: "高",
    medium: "中",
    normal: "中",
    low: "低",
    "needs-decision": "判断待ち",
    "needs-detail": "詳細化待ち",
    unclassified: "未分類",
    "writer-a": "Agent A",
    "writer-b": "Agent B",
    "writer-c": "Agent C",
  };
  return translations[value.toLowerCase()] ?? value.replaceAll("-", " ");
}

function issueStatus(issue: ScheduledIssue): string {
  if (issue.workflow === "blocked" || issue.state_reason === "blocked") {
    return "blocked";
  }
  return issue.workflow || issue.state_reason || issue.schedule_status || "unknown";
}

function numericConfidence(value?: number | string): number | null {
  if (typeof value === "number") {
    return Math.max(0, Math.min(100, value <= 1 ? value * 100 : value));
  }
  if (typeof value === "string") {
    const qualitative: Record<string, number> = {
      low: 35,
      medium: 65,
      high: 85,
    };
    if (qualitative[value.toLowerCase()] !== undefined) {
      return qualitative[value.toLowerCase()];
    }
    const parsed = Number.parseFloat(value);
    if (!Number.isNaN(parsed)) {
      return Math.max(0, Math.min(100, parsed <= 1 ? parsed * 100 : parsed));
    }
  }
  return null;
}

function confidenceLabel(value?: number | string): string {
  if (
    typeof value === "string" &&
    ["low", "medium", "high"].includes(value.toLowerCase())
  ) {
    return displayText(value);
  }
  const numeric = numericConfidence(value);
  if (numeric !== null) return `${Math.round(numeric)}%`;
  if (!value) return "算定中";
  return displayText(String(value));
}

function sourceLabel(source: RoadmapSnapshot["source"]): string {
  if (typeof source === "string") return source;
  if (!source) return "GitHub Issues";
  const candidate =
    source.description ??
    source.label ??
    source.kind ??
    source.type ??
    source.endpoint;
  if (typeof source.endpoint === "string") return "GitHub Issues API";
  return typeof candidate === "string" ? candidate : "GitHub Issues";
}

function forecastScopeLabel(scope?: string): string {
  if (!scope) return "日程化できるcurated issue";
  if (scope.startsWith("Open curated issues")) {
    return "保留を除き、依存関係を日程化できるcurated issue";
  }
  return scope;
}

function unique(values: Array<string | null | undefined>): string[] {
  return Array.from(new Set(values.filter((value): value is string => Boolean(value))));
}

function normalizeLanes(snapshot: RoadmapSnapshot): RoadmapLane[] {
  const supplied = Array.isArray(snapshot.lanes) ? snapshot.lanes : [];
  return defaultLanes.map((fallback) => {
    const lane = supplied.find((item) => item.id === fallback.id);
    return lane
      ? {
          ...fallback,
          ...lane,
          label: lane.label || fallback.label,
          role: lane.role || fallback.role,
        }
      : fallback;
  });
}

function laneRole(lane: RoadmapLane): string {
  if (lane.id === "review" || lane.role === "reviewer") return "レビュー・統合";
  if (lane.role === "writer") return "実装";
  return lane.role || "実装";
}

function schemaVersion(schema: RoadmapSnapshot["schema"]): string {
  if (schema === undefined) return "1";
  const matched = String(schema).match(/v(\d+)$/i);
  return matched?.[1] ?? String(schema);
}

function timelineStyle(
  startValue: string | null | undefined,
  endValue: string | null | undefined,
  rangeStart: Date,
  totalDays: number,
): CSSProperties {
  const start = validDate(startValue) ?? rangeStart;
  const end = validDate(endValue) ?? start;
  const left = Math.max(0, diffDays(rangeStart, start));
  const duration = Math.max(1, diffDays(start, end) + 1);
  return {
    "--bar-left": `${(left / totalDays) * 100}%`,
    "--bar-width": `${Math.max(0.85, (duration / totalDays) * 100)}%`,
  } as CSSProperties;
}

function Icon({
  children,
  className,
}: {
  children: ReactNode;
  className?: string;
}) {
  return (
    <svg
      className={className}
      viewBox="0 0 20 20"
      aria-hidden="true"
      fill="none"
    >
      {children}
    </svg>
  );
}

function IssueLink({
  issue,
  compact = false,
}: {
  issue: ScheduledIssue;
  compact?: boolean;
}) {
  return (
    <a
      className={compact ? "roadmap-issue-link compact" : "roadmap-issue-link"}
      href={issue.url || `${githubRepository}/issues/${issue.number}`}
      title={`#${issue.number} ${issue.title}`}
    >
      <span>#{issue.number}</span>
      {!compact && <strong>{issue.title}</strong>}
    </a>
  );
}

function FilterSelect({
  id,
  label,
  value,
  values,
  onChange,
}: {
  id: string;
  label: string;
  value: string;
  values: string[];
  onChange: (value: string) => void;
}) {
  return (
    <label className="roadmap-filter" htmlFor={id}>
      <span>{label}</span>
      <span className="roadmap-select-wrap">
        <select
          id={id}
          value={value}
          onChange={(event) => onChange(event.target.value)}
        >
          <option value="all">すべて</option>
          {values.map((item) => (
            <option key={item} value={item}>
              {displayText(item)}
            </option>
          ))}
        </select>
        <Icon>
          <path d="m6 8 4 4 4-4" stroke="currentColor" strokeWidth="1.6" />
        </Icon>
      </span>
    </label>
  );
}

export default function RoadmapClient({
  snapshot,
}: {
  snapshot: RoadmapSnapshot;
}) {
  const schedule = Array.isArray(snapshot.schedule) ? snapshot.schedule : [];
  const lanes = normalizeLanes(snapshot);
  const forecast = snapshot.forecast ?? {};
  const summary = snapshot.summary ?? {};
  const unscheduledIssues = Array.isArray(snapshot.unscheduled)
    ? snapshot.unscheduled.filter(
        (issue): issue is ScheduledIssue =>
          typeof issue === "object" && issue !== null,
      )
    : [];
  const unscheduledCount =
    unscheduledIssues.length || forecast.unscheduled_count || 0;

  const [filters, setFilters] = useState<FilterState>({
    status: "all",
    priority: "all",
    lane: "all",
  });

  const availableStatuses = useMemo(
    () => unique(schedule.map(issueStatus)).sort(),
    [schedule],
  );
  const availablePriorities = useMemo(
    () => unique(schedule.map((issue) => issue.priority)).sort(),
    [schedule],
  );

  const filtered = useMemo(
    () =>
      schedule.filter((issue) => {
        const laneMatch =
          filters.lane === "all" ||
          issue.lane === filters.lane ||
          (filters.lane === "review" && Boolean(issue.review_start_date));
        return (
          laneMatch &&
          (filters.status === "all" || issueStatus(issue) === filters.status) &&
          (filters.priority === "all" || issue.priority === filters.priority)
        );
      }),
    [filters, schedule],
  );

  const dateBounds = useMemo(() => {
    const dates = schedule.flatMap((issue) =>
      [
        issue.start_date,
        issue.end_date,
        issue.review_start_date,
        issue.review_end_date,
      ]
        .map(validDate)
        .filter((date): date is Date => date !== null),
    );
    const forecastStart = validDate(forecast.start_date);
    const forecastEnd =
      validDate(forecast.conservative_finish) ??
      validDate(forecast.likely_finish);
    if (forecastStart) dates.push(forecastStart);
    if (forecastEnd) dates.push(forecastEnd);
    const now = validDate(snapshot.as_of) ?? new Date();
    const min = dates.length
      ? new Date(Math.min(...dates.map((date) => date.getTime())))
      : now;
    const max = dates.length
      ? new Date(Math.max(...dates.map((date) => date.getTime())))
      : addDays(now, 28);
    const start = startOfWeek(min);
    const rawEnd = addDays(startOfWeek(max), 6);
    const end = diffDays(start, rawEnd) < 27 ? addDays(start, 27) : rawEnd;
    return { start, end, total: Math.max(1, diffDays(start, end) + 1) };
  }, [
    forecast.conservative_finish,
    forecast.likely_finish,
    forecast.start_date,
    schedule,
    snapshot.as_of,
  ]);

  const timelineTicks = useMemo(() => {
    const ticks: Date[] = [];
    for (
      let cursor = dateBounds.start;
      cursor <= dateBounds.end;
      cursor = addDays(cursor, 7)
    ) {
      ticks.push(cursor);
    }
    return ticks;
  }, [dateBounds]);

  const months = useMemo(() => {
    const values: Date[] = [];
    let cursor = new Date(
      Date.UTC(dateBounds.start.getUTCFullYear(), dateBounds.start.getUTCMonth(), 1),
    );
    const end = new Date(
      Date.UTC(dateBounds.end.getUTCFullYear(), dateBounds.end.getUTCMonth(), 1),
    );
    while (cursor <= end) {
      values.push(cursor);
      cursor = new Date(
        Date.UTC(cursor.getUTCFullYear(), cursor.getUTCMonth() + 1, 1),
      );
    }
    return values;
  }, [dateBounds]);

  const initialMonthIndex = Math.max(
    0,
    months.findIndex((month) => {
      const asOf = validDate(snapshot.as_of);
      return (
        asOf &&
        month.getUTCFullYear() === asOf.getUTCFullYear() &&
        month.getUTCMonth() === asOf.getUTCMonth()
      );
    }),
  );
  const [monthIndex, setMonthIndex] = useState(initialMonthIndex);
  const safeMonthIndex = Math.min(monthIndex, Math.max(0, months.length - 1));
  const selectedMonth = months[safeMonthIndex] ?? dateBounds.start;

  const calendarCells = useMemo<CalendarCell[]>(() => {
    const first = new Date(
      Date.UTC(
        selectedMonth.getUTCFullYear(),
        selectedMonth.getUTCMonth(),
        1,
      ),
    );
    const last = new Date(
      Date.UTC(
        selectedMonth.getUTCFullYear(),
        selectedMonth.getUTCMonth() + 1,
        0,
      ),
    );
    const gridStart = startOfWeek(first);
    const gridEnd = addDays(startOfWeek(last), 6);
    const cells: CalendarCell[] = [];
    for (
      let cursor = gridStart;
      cursor <= gridEnd;
      cursor = addDays(cursor, 1)
    ) {
      const iso = isoDate(cursor);
      const issues = filtered.filter((issue) => {
        const start = issue.start_date ?? issue.review_start_date;
        const end = issue.review_end_date ?? issue.end_date ?? start;
        return Boolean(start && end && iso >= start && iso <= end);
      });
      cells.push({
        date: cursor,
        iso,
        inMonth: cursor.getUTCMonth() === selectedMonth.getUTCMonth(),
        issues,
        starting: issues.filter(
          (issue) =>
            issue.start_date === iso || issue.review_start_date === iso,
        ).length,
        finishing: issues.filter(
          (issue) => issue.end_date === iso || issue.review_end_date === iso,
        ).length,
      });
    }
    return cells;
  }, [filtered, selectedMonth]);

  const blocked = [...schedule, ...unscheduledIssues].filter(
    (issue) => issue.state_reason === "blocked" || issueStatus(issue) === "blocked",
  );
  const blockedCount = summary.curated_workflows?.blocked ?? blocked.length;
  const confidence = numericConfidence(forecast.confidence);
  const openCuratedCount =
    summary.open_curated ??
    summary.open ??
    Object.values(summary.open_roles ?? {}).reduce(
      (total, value) => total + value,
      0,
    );
  const scheduledCount = forecast.scheduled_count ?? schedule.length;
  const likelyDuration =
    validDate(forecast.start_date) && validDate(forecast.likely_finish)
      ? diffDays(
          validDate(forecast.start_date) as Date,
          validDate(forecast.likely_finish) as Date,
        ) + 1
      : null;

  const clearFilters = () =>
    setFilters({ status: "all", priority: "all", lane: "all" });

  return (
    <>
      <header className="roadmap-hero">
        <div className="roadmap-hero-copy">
          <span className="roadmap-kicker">
            <i />
            GitHub Issuesから自動生成
          </span>
          <h1>
            4人で進める。
            <br />
            <em>見通しは、正直に。</em>
          </h1>
          <p>
            3エージェントが実装し、1エージェントがレビューと統合を担当。
            現在の依存関係・処理実績・未確定事項を含めて、完了見込みを毎回計算します。
          </p>
          <div className="roadmap-hero-meta">
            <span>
              基準日 <strong>{formatDate(snapshot.as_of, {
                year: "numeric",
                month: "long",
                day: "numeric",
              })}</strong>
            </span>
            <span>
              対象 <strong>{forecastScopeLabel(forecast.scope)}</strong>
            </span>
          </div>
        </div>

        <div className="roadmap-eta" aria-label="完了見込み">
          <span className="roadmap-eta-label">現在の完了見込み</span>
          <strong>{formatDate(forecast.likely_finish, {
            year: "numeric",
            month: "long",
            day: "numeric",
          })}</strong>
          <div className="roadmap-eta-range">
            <span>安全側</span>
            <b>{formatDate(forecast.conservative_finish, {
              month: "long",
              day: "numeric",
            })}</b>
          </div>
          <div className="roadmap-confidence">
            <div>
              <span>予測の確度</span>
              <b>{confidenceLabel(forecast.confidence)}</b>
            </div>
            <div
              className="roadmap-confidence-track"
              role="meter"
              aria-label="予測の確度"
              aria-valuemin={0}
              aria-valuemax={100}
              aria-valuenow={confidence ?? undefined}
              aria-valuetext={confidenceLabel(forecast.confidence)}
            >
              <i
                style={
                  {
                    "--confidence": `${confidence ?? 0}%`,
                  } as CSSProperties
                }
              />
            </div>
          </div>
          {likelyDuration !== null && (
            <p>開始から約 {likelyDuration} 暦日。新規追加が止まる前提です。</p>
          )}
        </div>
      </header>

      <section className="roadmap-stat-strip" aria-label="進捗サマリー">
        <div>
          <span>OPEN CURATED</span>
          <strong>{openCuratedCount ?? "—"}</strong>
          <small>実作業</small>
        </div>
        <div>
          <span>SCHEDULED</span>
          <strong>{scheduledCount}</strong>
          <small>割当済み</small>
        </div>
        <div>
          <span>UNSCHEDULED</span>
          <strong>{unscheduledCount}</strong>
          <small>要トリアージ</small>
        </div>
        <div>
          <span>BLOCKED</span>
          <strong>{blockedCount}</strong>
          <small>依存待ち</small>
        </div>
      </section>

      <section className="roadmap-section roadmap-overview" aria-labelledby="team-title">
        <div className="roadmap-section-heading">
          <div>
            <span className="roadmap-section-number">01 / CAPACITY</span>
            <h2 id="team-title">書く3枠。<br />守る1枠。</h2>
          </div>
          <p>
            同時実装は最大3件。Agent Dはコードレビュー、CI確認、競合解消、
            リリース判断を受け持ち、完了の定義を薄めません。
          </p>
        </div>

        <div className="roadmap-lane-cards">
          {lanes.map((lane, index) => {
            const count =
              lane.id === "review"
                ? schedule.filter((issue) => issue.review_start_date).length
                : schedule.filter((issue) => issue.lane === lane.id).length;
            return (
              <article
                className={`roadmap-lane-card lane-${lane.id}`}
                key={lane.id}
              >
                <div className="roadmap-agent-mark">
                  <span>0{index + 1}</span>
                  <i />
                </div>
                <div>
                  <span>{lane.label}</span>
                  <h3>{laneRole(lane)}</h3>
                  <p>{count} issues · WIP {lane.capacity || 1}</p>
                </div>
              </article>
            );
          })}
        </div>

        {(blockedCount > 0 || unscheduledCount > 0 || (confidence ?? 100) < 70) && (
          <div className="roadmap-warnings" aria-label="計画上の注意">
            {blockedCount > 0 && (
              <div className="roadmap-warning critical">
                <Icon>
                  <path
                    d="M10 2.4 18 17H2L10 2.4Zm0 5.1v4.2m0 2.3v.1"
                    stroke="currentColor"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    strokeWidth="1.5"
                  />
                </Icon>
                <div>
                  <strong>{blockedCount}件がブロック中</strong>
                  <p>
                    {blocked.slice(0, 3).map((issue, index) => (
                      <span key={issue.number}>
                        {index > 0 && "、"}
                        <IssueLink compact issue={issue} />
                      </span>
                    ))}
                    {blockedCount > 3 && ` ほか${blockedCount - 3}件`}
                    。先に依存元を解消しない限り、終了日は後ろへ動きます。
                  </p>
                </div>
              </div>
            )}
            {unscheduledCount > 0 && (
              <div className="roadmap-warning">
                <Icon>
                  <circle cx="10" cy="10" r="7.7" stroke="currentColor" strokeWidth="1.5" />
                  <path d="M10 6.1v4.7m0 2.7v.1" stroke="currentColor" strokeLinecap="round" strokeWidth="1.5" />
                </Icon>
                <div>
                  <strong>{unscheduledCount}件はまだ見積もり外</strong>
                  <p>詳細化・サイズ付け・依存関係の確定後に日程へ入ります。</p>
                </div>
              </div>
            )}
          </div>
        )}
      </section>

      <section
        className="roadmap-section roadmap-plan-section"
        id="timeline"
        aria-labelledby="timeline-title"
      >
        <div className="roadmap-section-heading compact">
          <div>
            <span className="roadmap-section-number">02 / TIMELINE</span>
            <h2 id="timeline-title">実装とレビューを、<br />一本の時間軸で。</h2>
          </div>
          <p>
            棒の実線部分が実装、斜線部分がレビュー期間です。
            フィルターはタイムラインと月間カレンダーの両方に反映されます。
          </p>
        </div>

        <div className="roadmap-filter-bar">
          <div className="roadmap-filters">
            <FilterSelect
              id="roadmap-status"
              label="ステータス"
              value={filters.status}
              values={availableStatuses}
              onChange={(status) => setFilters((current) => ({ ...current, status }))}
            />
            <FilterSelect
              id="roadmap-priority"
              label="優先度"
              value={filters.priority}
              values={availablePriorities}
              onChange={(priority) =>
                setFilters((current) => ({ ...current, priority }))
              }
            />
            <FilterSelect
              id="roadmap-lane"
              label="担当"
              value={filters.lane}
              values={lanes.map((lane) => lane.id)}
              onChange={(lane) => setFilters((current) => ({ ...current, lane }))}
            />
          </div>
          <div className="roadmap-filter-result" aria-live="polite">
            <b>{filtered.length}</b> / {schedule.length} issues
            {(filters.status !== "all" ||
              filters.priority !== "all" ||
              filters.lane !== "all") && (
              <button type="button" onClick={clearFilters}>
                条件をクリア
              </button>
            )}
          </div>
        </div>

        <div className="roadmap-gantt-shell">
          <div className="roadmap-gantt-scroll">
            <div
              className="roadmap-gantt"
              style={{
                minWidth: `${Math.max(
                  62,
                  13.5 + timelineTicks.length * 5,
                )}rem`,
              }}
            >
              <div className="roadmap-gantt-head">
                <span>担当 / Issue</span>
                <div
                  className="roadmap-ticks"
                  style={{ gridTemplateColumns: `repeat(${timelineTicks.length}, 1fr)` }}
                >
                  {timelineTicks.map((tick) => (
                    <span key={isoDate(tick)}>
                      {formatDate(isoDate(tick))}
                    </span>
                  ))}
                </div>
              </div>

              {lanes.map((lane) => {
                const laneIssues =
                  lane.id === "review"
                    ? filtered.filter(
                        (issue) =>
                          Boolean(issue.review_start_date) || issue.lane === "review",
                      )
                    : filtered.filter((issue) => issue.lane === lane.id);
                return (
                  <div className={`roadmap-gantt-lane gantt-${lane.id}`} key={lane.id}>
                    <div className="roadmap-gantt-lane-title">
                      <span>{lane.label}</span>
                      <strong>{laneRole(lane)}</strong>
                      <small>{laneIssues.length} items</small>
                    </div>
                    <div className="roadmap-gantt-rows">
                      {laneIssues.length === 0 ? (
                        <div className="roadmap-gantt-empty">
                          該当する予定はありません
                        </div>
                      ) : lane.id === "review" ? (
                        <div className="roadmap-gantt-row roadmap-review-queue">
                          <div className="roadmap-gantt-grid" aria-hidden="true">
                            {timelineTicks.map((tick) => (
                              <i key={isoDate(tick)} />
                            ))}
                          </div>
                          {laneIssues.map((issue) => (
                            <a
                              className={`roadmap-gantt-bar review ${
                                issueStatus(issue) === "blocked" ? "blocked" : ""
                              }`}
                              href={issue.url || `${githubRepository}/issues/${issue.number}`}
                              style={timelineStyle(
                                issue.review_start_date ?? issue.start_date,
                                issue.review_end_date ?? issue.end_date,
                                dateBounds.start,
                                dateBounds.total,
                              )}
                              aria-label={`Issue #${issue.number} ${issue.title}のレビュー、${formatDate(
                                issue.review_start_date,
                              )}から${formatDate(issue.review_end_date)}まで`}
                              key={`review-${issue.number}`}
                              title={`#${issue.number} ${issue.title}`}
                            >
                              <b>#{issue.number}</b>
                            </a>
                          ))}
                        </div>
                      ) : (
                        laneIssues.map((issue) => {
                          const start = issue.start_date;
                          const end = issue.writer_end_date ?? issue.end_date;
                          return (
                            <div className="roadmap-gantt-row" key={`${lane.id}-${issue.number}`}>
                              <div className="roadmap-gantt-grid" aria-hidden="true">
                                {timelineTicks.map((tick) => (
                                  <i key={isoDate(tick)} />
                                ))}
                              </div>
                              <a
                                className={`roadmap-gantt-bar ${
                                  issueStatus(issue) === "blocked" ? "blocked" : ""
                                }`}
                                href={issue.url || `${githubRepository}/issues/${issue.number}`}
                                style={timelineStyle(
                                  start,
                                  end,
                                  dateBounds.start,
                                  dateBounds.total,
                                )}
                                aria-label={`Issue #${issue.number} ${issue.title}、${formatDate(start)}から${formatDate(end)}まで`}
                              >
                                <b>#{issue.number}</b>
                                <span>{issue.title}</span>
                              </a>
                            </div>
                          );
                        })
                      )}
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
          <div className="roadmap-gantt-legend">
            <span><i className="writer" />実装</span>
            <span><i className="review" />レビュー・統合</span>
            <span><i className="blocked" />ブロック中</span>
            <small>横方向にスクロールできます</small>
          </div>
        </div>
      </section>

      <section
        className="roadmap-section roadmap-calendar-section"
        id="calendar"
        aria-labelledby="calendar-title"
      >
        <div className="roadmap-section-heading compact">
          <div>
            <span className="roadmap-section-number">03 / CALENDAR</span>
            <h2 id="calendar-title">今月、何を<br />終わらせるか。</h2>
          </div>
          <p>
            日付ごとの着手・完了を確認できます。カードを選ぶとGitHub Issueへ移動します。
            土日も表示しますが、見積もりはスナップショットの稼働日前提に従います。
          </p>
        </div>

        <div className="roadmap-calendar-shell">
          <div className="roadmap-calendar-toolbar">
            <div>
              <span>MONTHLY PLAN</span>
              <h3>
                {new Intl.DateTimeFormat("ja-JP", {
                  timeZone: "UTC",
                  year: "numeric",
                  month: "long",
                }).format(selectedMonth)}
              </h3>
            </div>
            <div className="roadmap-month-controls">
              <button
                type="button"
                aria-label="前の月"
                disabled={safeMonthIndex === 0}
                onClick={() => setMonthIndex((current) => Math.max(0, current - 1))}
              >
                <Icon><path d="m12 5-5 5 5 5" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" /></Icon>
              </button>
              <span>{safeMonthIndex + 1} / {months.length}</span>
              <button
                type="button"
                aria-label="次の月"
                disabled={safeMonthIndex >= months.length - 1}
                onClick={() =>
                  setMonthIndex((current) =>
                    Math.min(months.length - 1, current + 1),
                  )
                }
              >
                <Icon><path d="m8 5 5 5-5 5" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" /></Icon>
              </button>
            </div>
          </div>
          <div className="roadmap-calendar-scroll">
            <div className="roadmap-calendar" role="grid" aria-label="月間ロードマップ">
              {weekdays.map((weekday) => (
                <div className="roadmap-weekday" role="columnheader" key={weekday}>
                  {weekday}
                </div>
              ))}
              {calendarCells.map((cell) => (
                <div
                  className={`roadmap-calendar-day ${cell.inMonth ? "" : "outside"} ${
                    cell.iso === snapshot.as_of ? "today" : ""
                  }`}
                  key={cell.iso}
                  role="gridcell"
                  aria-label={`${formatDate(cell.iso, {
                    month: "long",
                    day: "numeric",
                  })}、${cell.issues.length}件`}
                >
                  <div className="roadmap-calendar-date">
                    <time dateTime={cell.iso}>{cell.date.getUTCDate()}</time>
                    {(cell.starting > 0 || cell.finishing > 0) && (
                      <span>
                        {cell.starting > 0 && `着手 ${cell.starting}`}
                        {cell.starting > 0 && cell.finishing > 0 && " · "}
                        {cell.finishing > 0 && `完了 ${cell.finishing}`}
                      </span>
                    )}
                  </div>
                  <div className="roadmap-day-issues">
                    {cell.issues.slice(0, 3).map((issue) => (
                      (() => {
                        const inReview = Boolean(
                          issue.review_start_date &&
                            issue.review_end_date &&
                            cell.iso >= issue.review_start_date &&
                            cell.iso <= issue.review_end_date,
                        );
                        return (
                          <a
                            className={`calendar-issue lane-${
                              inReview ? "review" : issue.lane || "unknown"
                            } ${
                              issueStatus(issue) === "blocked" ? "blocked" : ""
                            }`}
                            href={issue.url || `${githubRepository}/issues/${issue.number}`}
                            key={`${cell.iso}-${issue.number}`}
                            title={issue.title}
                          >
                            <b>{inReview ? "R" : `#${issue.number}`}</b>
                            <span>{issue.title}</span>
                          </a>
                        );
                      })()
                    ))}
                    {cell.issues.length > 3 && (
                      <span className="roadmap-day-more">
                        +{cell.issues.length - 3}件
                      </span>
                    )}
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>
      </section>

      <section className="roadmap-section roadmap-notes" id="notes" aria-labelledby="notes-title">
        <div className="roadmap-notes-title">
          <span className="roadmap-section-number">04 / ASSUMPTIONS</span>
          <h2 id="notes-title">この日付を、約束ではなく<br />判断材料として使う。</h2>
        </div>

        <div className="roadmap-notes-grid">
          <article>
            <span>計算の前提</span>
            <h3>最大4エージェント</h3>
            <p>
              実装3枠＋レビュー専任1枠。Issueの優先度、サイズ、依存関係、
              直近の処理実績をもとに並べます。
            </p>
          </article>
          <article>
            <span>日付が動く条件</span>
            <h3>スコープとブロッカー</h3>
            <p>
              Issueの追加、サイズ変更、依存待ち、レビュー差し戻しが発生すると
              完了見込みは後ろへ動きます。
            </p>
          </article>
          <article>
            <span>更新元</span>
            <h3>{sourceLabel(snapshot.source)}</h3>
            <p>
              {snapshot.repository || "hjosugi/kofun"} · schema v
              {schemaVersion(snapshot.schema)} · 生成{" "}
              {formatDate(snapshot.generated_at, {
                year: "numeric",
                month: "long",
                day: "numeric",
                hour: "2-digit",
                minute: "2-digit",
              })}
            </p>
          </article>
        </div>

        {Array.isArray(snapshot.dependency_chains) &&
          snapshot.dependency_chains.length > 0 && (
            <div className="roadmap-dependencies">
              <div>
                <span>DEPENDENCY WATCH</span>
                <strong>先に通すべきチェーン</strong>
              </div>
              <ol>
                {snapshot.dependency_chains.slice(0, 4).map((chain, index) => {
                  const numbers = chain.issue_numbers ?? chain.issues ?? [];
                  return (
                    <li key={`${chain.label || "chain"}-${index}`}>
                      <span>{chain.critical ? "Critical" : `Chain ${index + 1}`}</span>
                      <div>
                        {numbers.map((number, numberIndex) => (
                          <span key={number}>
                            {numberIndex > 0 && <i>→</i>}
                            <a href={`${githubRepository}/issues/${number}`}>#{number}</a>
                          </span>
                        ))}
                        {numbers.length === 0 && (chain.label || "詳細化待ち")}
                      </div>
                    </li>
                  );
                })}
              </ol>
            </div>
          )}

        <footer className="roadmap-footer">
          <p>
            このページは公開済みのGitHubデータから生成した計画です。
            実際の状態はIssueを正としてください。
          </p>
          <div>
            <a href={`${githubRepository}/issues`}>すべてのIssuesを見る ↗</a>
            <a href="https://github.com/users/hjosugi/projects">
              GitHub Projects ↗
            </a>
          </div>
        </footer>
      </section>
    </>
  );
}
