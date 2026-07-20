export const MAX_SHARED_SOURCE_BYTES = 16 * 1024;

function toBase64Url(bytes) {
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  }
  return btoa(binary)
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replace(/=+$/u, "");
}

function fromBase64Url(encoded) {
  if (!/^[A-Za-z0-9_-]*$/u.test(encoded)) {
    throw new Error("shared code is not valid base64url");
  }
  const padding = "=".repeat((4 - (encoded.length % 4)) % 4);
  const binary = atob(encoded.replaceAll("-", "+").replaceAll("_", "/") + padding);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

export function encodeShareHash(stepId, source) {
  const bytes = new TextEncoder().encode(source);
  if (bytes.length > MAX_SHARED_SOURCE_BYTES) {
    throw new Error("shared code must be 16 KiB or smaller");
  }
  const parameters = new URLSearchParams({
    step: stepId,
    code: toBase64Url(bytes),
  });
  return `#${parameters.toString()}`;
}

export function decodeShareHash(hash) {
  if (hash === "" || hash === "#") return null;
  const parameters = new URLSearchParams(hash.startsWith("#") ? hash.slice(1) : hash);
  const stepId = parameters.get("step");
  const encoded = parameters.get("code");
  if (stepId === null || encoded === null) return null;
  const bytes = fromBase64Url(encoded);
  if (bytes.length > MAX_SHARED_SOURCE_BYTES) {
    throw new Error("shared code must be 16 KiB or smaller");
  }
  return {
    stepId,
    source: new TextDecoder("utf-8", { fatal: true }).decode(bytes),
  };
}
