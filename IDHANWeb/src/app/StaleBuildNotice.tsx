/**
 * Prompt for a manual reload when the server serves a different UI build, preserving unsaved
 * state until reload.
 */

import { useStaleBuild } from './buildFreshness';

export function StaleBuildNotice() {
  const stale = useStaleBuild();

  if (!stale) return null;

  return (
    <div className="build-notice" role="status">
      <span>A newer UI is available.</span>
      <button type="button" onClick={() => window.location.reload()}>
        Refresh
      </button>
    </div>
  );
}
