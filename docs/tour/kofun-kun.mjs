// Static browser port of the canonical 16x16 Kofun-kun pixel grid from
// hjosugi/hjosugi-hub, lib/hjosugi_hub/kofun.ex at commit 8435101e7b0a.
// That source is 0BSD. Keeping the small grid here means the no-install tour
// still has no runtime request or dependency on the portfolio site.

const BODY = Object.freeze([
  [6, 0, 4],
  [5, 1, 6],
  [4, 2, 8],
  [3, 3, 10],
  [3, 4, 10],
  [3, 5, 10],
  [3, 6, 10],
  [4, 7, 8],
  [5, 8, 6],
  [5, 9, 6],
  [4, 10, 8],
  [4, 11, 8],
  [3, 12, 10],
  [2, 13, 12],
  [2, 14, 12],
  [4, 15, 3],
  [9, 15, 3],
]);

const FACES = Object.freeze({
  idle: Object.freeze([[5, 4, 1, 2], [10, 4, 1, 2], [7, 6, 2, 1]]),
  blink: Object.freeze([[5, 5, 1, 1], [10, 5, 1, 1], [7, 6, 2, 1]]),
  smile: Object.freeze([
    [5, 4, 1, 2],
    [10, 4, 1, 2],
    [6, 6, 1, 1],
    [7, 7, 2, 1],
    [9, 6, 1, 1],
  ]),
  munch: Object.freeze([[5, 4, 1, 2], [10, 4, 1, 2], [7, 6, 2, 2]]),
});

export const KOFUN_KUN_POSES = Object.freeze(Object.keys(FACES));
export const KOFUN_KUN_SOURCE =
  "https://github.com/hjosugi/hjosugi-hub/blob/8435101e7b0ae91a934fbd1f280e00e2449e468b/lib/hjosugi_hub/kofun.ex";

function rect([x, y, width, height = 1]) {
  return `<rect x="${x}" y="${y}" width="${width}" height="${height}"/>`;
}

function group(className, rows) {
  return `<g class="${className}">${rows.map(rect).join("")}</g>`;
}

export function kofunKunSvg(pose = "idle") {
  const face = FACES[pose];
  if (face === undefined) {
    throw new RangeError(`unknown Kofun-kun pose: ${pose}`);
  }
  const treat = pose === "munch"
    ? group("kofun-kun-treat", [[1, 6, 2, 2], [0, 8, 1, 1]])
    : "";
  return [
    '<svg viewBox="0 0 16 16" class="kofun-kun-svg"',
    ' shape-rendering="crispEdges" aria-hidden="true" focusable="false">',
    group("kofun-kun-body", BODY),
    group("kofun-kun-face", face),
    treat,
    "</svg>",
  ].join("");
}

export function renderKofunKun(element, pose = "idle") {
  element.innerHTML = kofunKunSvg(pose);
  element.dataset.kofunPose = pose;
}
