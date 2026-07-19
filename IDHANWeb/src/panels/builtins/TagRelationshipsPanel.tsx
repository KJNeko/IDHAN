/**
 * Tag Relationships — add or remove parent and alias relationships between two tags, in one domain.
 *
 * A deliberate limitation: the server exposes only create/remove for these (POST /tags/parents|alias
 * /create|remove, keyed by ?tag_domain_id=); there is no endpoint to *list* a tag's existing
 * relationships, so this panel is an editor, not a browser. Siblings are omitted because that endpoint
 * is currently disabled server-side. The panel says so rather than pretending otherwise.
 *
 * Parent: the child tag gains the parent tag. Alias: the alias tag resolves to the aliased (canonical)
 * tag. Both directions are named explicitly in the UI so the asymmetric pairs aren't guesswork.
 */

import { useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import type { AutocompleteResult, TagDomain } from '../../api/types';
import { TagPicker } from './TagPicker';

type Kind = 'parent' | 'alias';

function TagRelationshipsPanel({ host }: PanelProps) {
  const [domains, setDomains] = useState<TagDomain[]>([]);
  const [domainId, setDomainId] = useState<number | null>(null);
  const [kind, setKind] = useState<Kind>('parent');
  const [left, setLeft] = useState<AutocompleteResult | null>(null); // child / alias
  const [right, setRight] = useState<AutocompleteResult | null>(null); // parent / aliased
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let cancelled = false;
    host.tags
      .listDomains()
      .then((list) => {
        if (cancelled) return;
        setDomains(list);
        setDomainId((current) => current ?? list[0]?.tag_domain_id ?? null);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [host]);

  async function submit(action: 'create' | 'remove') {
    if (domainId === null || left === null || right === null || busy) return;
    if (left.tag_id === right.tag_id) {
      host.ui.toast('Pick two different tags.', { kind: 'error' });
      return;
    }
    const path = kind === 'parent' ? 'parents' : 'alias';
    const body =
      kind === 'parent'
        ? [{ child_id: left.tag_id, parent_id: right.tag_id }]
        : [{ alias_id: left.tag_id, aliased_id: right.tag_id }];
    setBusy(true);
    try {
      const res = await host.http.fetch(`/tags/${path}/${action}?tag_domain_id=${domainId}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      if (res.status === 409) {
        host.ui.toast('Rejected: this would create a conflict or cycle.', { kind: 'error' });
      } else if (!res.ok) {
        const reason = (await res.text()).slice(0, 200);
        throw new Error(reason || `${action} → ${res.status}`);
      } else {
        const verb = action === 'create' ? 'Added' : 'Removed';
        host.ui.toast(`${verb} ${kind}: ${left.text} → ${right.text}`, { kind: 'success' });
        setLeft(null);
        setRight(null);
      }
    } catch (err) {
      host.ui.toast(`Failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  const leftLabel = kind === 'parent' ? 'Child' : 'Alias';
  const rightLabel = kind === 'parent' ? 'Parent' : 'Aliased (canonical)';
  const arrow = kind === 'parent' ? 'gains parent' : 'resolves to';

  if (domainId === null) {
    return (
      <div className="panel-body">
        <p className="muted">No tag domains exist. Create one in the Tag Domain Manager first.</p>
      </div>
    );
  }

  return (
    <div className="panel-body tag-relationships">
      <div className="rel-row">
        <label className="tag-domain">
          Domain
          <select value={domainId} onChange={(e) => setDomainId(Number(e.target.value))}>
            {domains.map((d) => (
              <option key={d.tag_domain_id} value={d.tag_domain_id}>
                {d.domain_name}
              </option>
            ))}
          </select>
        </label>
        <div className="rel-kind">
          <label className="log-check">
            <input type="radio" name="rel-kind" checked={kind === 'parent'} onChange={() => setKind('parent')} />
            Parent
          </label>
          <label className="log-check">
            <input type="radio" name="rel-kind" checked={kind === 'alias'} onChange={() => setKind('alias')} />
            Alias
          </label>
        </div>
      </div>

      <div className="rel-pair">
        <div className="rel-end">
          <span className="muted rel-end-label">{leftLabel}</span>
          <TagPicker host={host} value={left} onPick={setLeft} domain={domainId} placeholder={`${leftLabel} tag…`} disabled={busy} />
        </div>
        <span className="rel-arrow muted" title={arrow}>
          {arrow} →
        </span>
        <div className="rel-end">
          <span className="muted rel-end-label">{rightLabel}</span>
          <TagPicker host={host} value={right} onPick={setRight} domain={domainId} placeholder={`${rightLabel} tag…`} disabled={busy} />
        </div>
      </div>

      <div className="rel-actions">
        <button type="button" className="toolbar-button" disabled={busy || !left || !right} onClick={() => void submit('create')}>
          Add
        </button>
        <button type="button" className="toolbar-button danger" disabled={busy || !left || !right} onClick={() => void submit('remove')}>
          Remove
        </button>
      </div>

      <p className="muted rel-note">
        The server has no endpoint to list existing relationships, so this panel can only add or remove them, not show
        what's already set. Siblings are omitted — that endpoint is disabled server-side.
      </p>
    </div>
  );
}

export const tagRelationshipsPanel = {
  type: 'tag-relationships',
  title: 'Tag Relationships',
  description: 'Add or remove parent and alias relationships between tags.',
  component: TagRelationshipsPanel,
  configVersion: 1,
  singleton: true,
} as const;
