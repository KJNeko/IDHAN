/**
 * A tiny semver range check, just enough for plugin manifests to declare which host API they target
 * (`"hostApi": "^1.2.0"`), without pulling in a full semver dependency.
 *
 * Supported ranges: an exact version ("1.2.3"), a wildcard ("*" / "x"), or a caret ("^1.2.3"). A
 * caret allows up to but not including the next major, with the usual pre-1.0 tightening (^0.2.3 is
 * locked to 0.2.x). An unrecognised range is treated as incompatible.
 */

interface Parsed {
  major: number;
  minor: number;
  patch: number;
}

function parse(version: string): Parsed | null {
  const match = /^(\d+)\.(\d+)\.(\d+)/.exec(version.trim());
  if (!match) return null;
  return { major: Number(match[1]), minor: Number(match[2]), patch: Number(match[3]) };
}

/** True if the current host `version` satisfies the plugin-declared `range`. */
export function satisfiesHostApi(version: string, range: string): boolean {
  const trimmed = range.trim();
  if (trimmed === '*' || trimmed === 'x') return true;

  const host = parse(version);
  if (!host) return false;

  if (trimmed.startsWith('^')) {
    const want = parse(trimmed.slice(1));
    if (!want) return false;
    if (host.major !== want.major) return false;
      // Pre-1.0, caret locks the minor too: ^0.2.x allows 0.2.*, not 0.3.*.
    if (want.major === 0 && host.minor !== want.minor) return false;
    return atLeast(host, want);
  }

  // Bare exact version.
  const want = parse(trimmed);
  if (!want) return false;
  return host.major === want.major && host.minor === want.minor && host.patch === want.patch;
}

/** host >= want, lexicographically over (major, minor, patch). Callers have already matched major. */
function atLeast(host: Parsed, want: Parsed): boolean {
  if (host.minor !== want.minor) return host.minor > want.minor;
  return host.patch >= want.patch;
}
