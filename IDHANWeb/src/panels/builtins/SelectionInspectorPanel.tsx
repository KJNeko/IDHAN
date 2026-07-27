/**
 * Selection Inspector — a small panel that exercises the cross-panel selection bus and per-instance
 * settings. It reflects the shared record selection, lets you clear it, and remembers (in its own
 * persisted config) whether to list the ids. In M4 the grid drives this selection for real.
 */

import { useEffect, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';

interface Config {
  showIds: boolean;
}

function SelectionInspectorPanel({ host }: PanelProps) {
  const [ids, setIds] = useState<readonly RecordId[]>(() => host.selection.get());
  const [showIds, setShowIds] = useState<boolean>(() => (host.settings.get() as Partial<Config>).showIds ?? true);

  useEffect(() => host.selection.subscribe(setIds), [host]);

  function toggleShowIds() {
    const next = !showIds;
    setShowIds(next);
    host.settings.set({ showIds: next });
  }

  return (
    <div className="panel-body">
      <p>
        <strong>{ids.length}</strong> record{ids.length === 1 ? '' : 's'} selected.
      </p>
      <div className="panel-row">
        <button type="button" className="link-button" onClick={() => host.selection.set([])} disabled={ids.length === 0}>
          Clear
        </button>
        <label className="panel-checkbox">
          <input type="checkbox" checked={showIds} onChange={toggleShowIds} /> Show ids
        </label>
      </div>
      {showIds && ids.length > 0 && <p className="muted mono">{ids.slice(0, 200).join(', ')}{ids.length > 200 ? ' …' : ''}</p>}
    </div>
  );
}

export const selectionInspectorPanel = {
  type: 'selection-inspector',
  title: 'Selection Inspector',
  description: 'Shows the current cross-panel record selection.',
  component: SelectionInspectorPanel,
  defaultConfig: { showIds: true } satisfies Config,
  configVersion: 1,
} as const;
