const siteBasePath = process.env.KOFUN_BASE_PATH ?? "";

export default function KofunMark({
  compact = false,
}: Readonly<{ compact?: boolean }>) {
  return (
    <img
      alt=""
      aria-hidden="true"
      className={compact ? "kofun-mark compact" : "kofun-mark"}
      src={`${siteBasePath}/kofun-mark.svg`}
    />
  );
}
