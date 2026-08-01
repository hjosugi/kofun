export function independentCanonical(value) {
  if (value === null) return "null";
  if (value === true) return "true";
  if (value === false) return "false";
  if (typeof value === "string") return JSON.stringify(value);
  if (Number.isSafeInteger(value)) return String(value);
  if (Array.isArray(value)) {
    let result = "[";
    for (let index = 0; index < value.length; index += 1) {
      if (index !== 0) result += ",";
      result += independentCanonical(value[index]);
    }
    return `${result}]`;
  }
  if (typeof value !== "object" || value === undefined) {
    throw new TypeError("unsupported canonical value");
  }
  let result = "{";
  const keys = Object.keys(value).sort();
  for (let index = 0; index < keys.length; index += 1) {
    if (index !== 0) result += ",";
    const key = keys[index];
    result += `${JSON.stringify(key)}:${independentCanonical(value[key])}`;
  }
  return `${result}}`;
}
