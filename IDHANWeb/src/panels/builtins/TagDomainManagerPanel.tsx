/**
 * Tag Domain Manager: list, create, and delete tag service domains (GET/POST/DELETE /tags/domain/*).
 * Domains are the top-level partition tags live in; the Tag Editor and Relationships panels pick one.
 * Deleting a domain removes its mappings, so it confirms first.
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import type { TagDomain } from '../../api/types';

function TagDomainManagerPanel({ host }: PanelProps) {
  const [domains, setDomains] = useState<TagDomain[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [name, setName] = useState('');
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const list = await host.tags.listDomains();
      setDomains(list);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [host]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  async function create() {
    const trimmed = name.trim();
    if (trimmed.length === 0) return;
    setBusy(true);
    try {
      const res = await host.http.fetch('/tags/domain/create', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: trimmed }),
      });
      if (res.status === 409) {
        host.ui.toast(`A domain named "${trimmed}" already exists.`, { kind: 'error' });
      } else if (!res.ok) {
        throw new Error(`create → ${res.status}`);
      } else {
        host.ui.toast(`Created domain "${trimmed}".`, { kind: 'success' });
        setName('');
        await refresh();
      }
    } catch (err) {
      host.ui.toast(`Create failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  async function remove(domain: TagDomain) {
    if (!window.confirm(`Delete domain "${domain.domain_name}"? This removes its tag mappings and cannot be undone.`)) {
      return;
    }
    setBusy(true);
    try {
      const res = await host.http.fetch(`/tags/domain/${domain.tag_domain_id}/delete`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`delete → ${res.status}`);
      host.ui.toast(`Deleted domain "${domain.domain_name}".`, { kind: 'success' });
      await refresh();
    } catch (err) {
      host.ui.toast(`Delete failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="panel-body domain-manager">
      <div className="domain-add">
        <input
          className="search-input"
          value={name}
          placeholder="New domain name…"
          disabled={busy}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') void create();
          }}
          spellCheck={false}
          autoComplete="off"
        />
        <button type="button" className="toolbar-button" disabled={busy || name.trim().length === 0} onClick={() => void create()}>
          Create
        </button>
      </div>

      {error && <p className="error">{error}</p>}
      {domains !== null && domains.length === 0 && <p className="muted">No tag domains yet.</p>}
      {domains !== null && domains.length > 0 && (
        <ul className="domain-list">
          {domains.map((d) => (
            <li key={d.tag_domain_id} className="domain-row">
              <span className="domain-name grow">{d.domain_name}</span>
              <span className="muted domain-id">#{d.tag_domain_id}</span>
              <button type="button" className="tag-remove" disabled={busy} onClick={() => void remove(d)} title="Delete domain">
                ×
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export const tagDomainManagerPanel = {
  type: 'tag-domain-manager',
  title: 'Tag Domain Manager',
  description: 'List, create, and delete tag service domains.',
  component: TagDomainManagerPanel,
  configVersion: 1,
  singleton: true,
} as const;
