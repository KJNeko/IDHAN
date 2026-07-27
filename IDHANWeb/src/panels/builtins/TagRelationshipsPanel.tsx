/**
 * Tag Relationships — edit one tag's relationships.
 *
 * Search a subject tag, then edit its aliases, parents, and children within the selected domain. The
 * relationship endpoints return ids (GET /tags/{domain}/{tag}/relationships); ids are resolved to
 * text only for display. Every domain is fetched at once: the editor works on the selected domain,
 * and the bottom section lists every relationship across all domains (read-only). Siblings are not
 * exposed for now.
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import type { AutocompleteResult, TagDomain, TagRelationships } from '../../api/types';
import { TagPicker } from './TagPicker';

const EMPTY_RELS: TagRelationships = {
  parents: [],
  children: [],
  older_siblings: [],
  younger_siblings: [],
  aliases: [],
  aliased: [],
};

/** Lists longer than this collapse by default in the all-domains view. */
const COLLAPSE_THRESHOLD = 25;

function TrashIcon() {
  return (
    <svg viewBox="0 0 16 16" width="13" height="13" aria-hidden="true" focusable="false">
      <path
        fill="currentColor"
        d="M6.5 1a1 1 0 0 0-1 1V3H3a.5.5 0 0 0 0 1h.6l.6 9.1A2 2 0 0 0 6.8 15h2.4a2 2 0 0 0 2-1.9L11.4 4h.6a.5.5 0 0 0 0-1h-2.5V2a1 1 0 0 0-1-1h-2zm0 2V2h2v1h-2zM6 5.5a.5.5 0 0 1 1 0v6a.5.5 0 0 1-1 0v-6zm3 0a.5.5 0 0 1 1 0v6a.5.5 0 0 1-1 0v-6z"
      />
    </svg>
  );
}

/** A titled add/remove box for one relationship direction. */
function RelationBox({
  host,
  domainId,
  title,
  hint,
  ids,
  label,
  onAdd,
  onRemove,
  busy,
}: {
  host: PanelProps['host'];
  domainId: number;
  title: string;
  hint: string;
  ids: number[];
  label: (id: number) => string;
  onAdd: (tag: AutocompleteResult) => void;
  onRemove: (id: number) => void;
  busy: boolean;
}) {
  return (
    <div className="rel-box">
      <div className="rel-box-title">
        {title} <span className="muted rel-box-hint">{hint}</span>
      </div>
      {ids.length > 0 && (
        <ul className="rel-list">
          {ids.map((id) => (
            <li key={id} className="rel-item">
              <button type="button" className="rel-del" disabled={busy} onClick={() => onRemove(id)} title="Remove">
                <TrashIcon />
              </button>
              <span className="grow" title={`#${id}`}>
                {label(id)}
              </span>
            </li>
          ))}
        </ul>
      )}
      <TagPicker
        host={host}
        value={null}
        onPick={(tag) => tag && onAdd(tag)}
        domain={domainId}
        placeholder={`Add ${title.toLowerCase()}…`}
        disabled={busy}
      />
    </div>
  );
}

function TagRelationshipsPanel({ host }: PanelProps) {
  const [domains, setDomains] = useState<TagDomain[]>([]);
  const [domainId, setDomainId] = useState<number | null>(null);
  const [subject, setSubject] = useState<AutocompleteResult | null>(null);
  const [busy, setBusy] = useState(false);

  const [relsByDomain, setRelsByDomain] = useState<Map<number, TagRelationships>>(new Map());
  const [text, setText] = useState<Map<number, string>>(new Map());
  const [error, setError] = useState<string | null>(null);
  const [openOverrides, setOpenOverrides] = useState<Map<string, boolean>>(new Map());

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

  const load = useCallback(
    async (signal?: AbortSignal) => {
      if (subject === null || domains.length === 0) {
        setRelsByDomain(new Map());
        setText(new Map());
        return;
      }
      try {
        const entries = await Promise.all(
          domains.map(async (d) => [d.tag_domain_id, await host.tags.relationships(d.tag_domain_id, subject.tag_id, signal)] as const),
        );
        if (signal?.aborted) return;
        const ids = entries.flatMap(([, r]) => [...r.parents, ...r.children, ...r.aliases, ...r.aliased]);
        const resolved = await host.tags.resolve(ids);
        if (signal?.aborted) return;
        setRelsByDomain(new Map(entries));
        setText(resolved);
        setError(null);
      } catch (err) {
        if (signal?.aborted) return;
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [host, domains, subject],
  );

  useEffect(() => {
    const controller = new AbortController();
    void load(controller.signal);
    return () => controller.abort();
  }, [load]);

  async function mutate(path: 'parents' | 'alias', action: 'create' | 'remove', body: Record<string, number>) {
    if (domainId === null || busy) return;
    setBusy(true);
    try {
      const res = await host.http.fetch(`/tags/${path}/${action}?tag_domain_id=${domainId}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify([body]),
      });
      if (res.status === 409) {
        host.ui.toast('Rejected: this would create a conflict or cycle.', { kind: 'error' });
      } else if (!res.ok) {
        const reason = (await res.text()).slice(0, 200);
        throw new Error(reason || `${action} → ${res.status}`);
      } else {
        await load();
      }
    } catch (err) {
      host.ui.toast(`Failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  const label = (id: number) => text.get(id) ?? `#${id}`;
  const domainName = (id: number) => domains.find((d) => d.tag_domain_id === id)?.domain_name ?? `Domain #${id}`;

  const isOpen = (key: string, count: number) => openOverrides.get(key) ?? count <= COLLAPSE_THRESHOLD;
  const toggle = (key: string, currentlyOpen: boolean) =>
    setOpenOverrides((prev) => new Map(prev).set(key, !currentlyOpen));

  if (domainId === null) {
    return (
      <div className="panel-body">
        <p className="muted">No tag domains exist. Create one in the Tag Domain Manager first.</p>
      </div>
    );
  }

  const edit = subject ? (relsByDomain.get(domainId) ?? EMPTY_RELS) : EMPTY_RELS;
  const sid = subject?.tag_id ?? 0;

  const allSections = subject
    ? [...relsByDomain.entries()]
        .map(([dId, r]) => ({
          dId,
          name: domainName(dId),
          groups: [
            { title: 'Parents', ids: r.parents },
            { title: 'Children', ids: r.children },
            { title: 'Alias for', ids: r.aliased },
            { title: 'Aliased by', ids: r.aliases },
          ].filter((g) => g.ids.length > 0),
        }))
        .filter((s) => s.groups.length > 0)
    : [];

  return (
    <div className="panel-body tag-relationships">
      <p className="rel-instant-notice">Changes apply instantly — there is no save step.</p>
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
      </div>

      <div className="rel-end">
        <span className="muted rel-end-label">Tag</span>
        <TagPicker host={host} value={subject} onPick={setSubject} domain={domainId} placeholder="Search a tag…" disabled={busy} />
      </div>

      {error && <p className="error">{error}</p>}

      {subject && (
        <div className="rel-editor">
          <RelationBox
            host={host}
            domainId={domainId}
            title="Alias for"
            hint="this tag is an alias of"
            ids={edit.aliased}
            label={label}
            busy={busy}
            onAdd={(t) => void mutate('alias', 'create', { alias_id: sid, aliased_id: t.tag_id })}
            onRemove={(id) => void mutate('alias', 'remove', { alias_id: sid, aliased_id: id })}
          />
          <RelationBox
            host={host}
            domainId={domainId}
            title="Parents"
            hint="this tag's parents"
            ids={edit.parents}
            label={label}
            busy={busy}
            onAdd={(t) => void mutate('parents', 'create', { child_id: sid, parent_id: t.tag_id })}
            onRemove={(id) => void mutate('parents', 'remove', { child_id: sid, parent_id: id })}
          />
          <RelationBox
            host={host}
            domainId={domainId}
            title="Children"
            hint="tags with this tag as parent"
            ids={edit.children}
            label={label}
            busy={busy}
            onAdd={(t) => void mutate('parents', 'create', { child_id: t.tag_id, parent_id: sid })}
            onRemove={(id) => void mutate('parents', 'remove', { child_id: id, parent_id: sid })}
          />
        </div>
      )}

      {subject && (
        <div className="rel-all">
          <div className="rel-all-head muted">All relationships (every domain)</div>
          {relsByDomain.size > 0 && allSections.length === 0 && <p className="muted">No relationships in any domain.</p>}
          {allSections.map((section) => (
            <div key={section.dId} className="rel-domain-section">
              <div className="rel-domain-name">{section.name}</div>
              {section.groups.map((g) => {
                const key = `${section.dId}:${g.title}`;
                const open = isOpen(key, g.ids.length);
                return (
                  <div key={g.title} className="rel-group">
                    <button type="button" className="rel-group-toggle" onClick={() => toggle(key, open)}>
                      <span className="rel-caret">{open ? '▾' : '▸'}</span>
                      <span className="rel-group-title">{g.title}</span>
                      <span className="muted rel-group-count">{g.ids.length}</span>
                    </button>
                    {open && (
                      <div className="rel-chips">
                        {g.ids.map((id) => (
                          <span key={id} className="rel-chip" title={`#${id}`}>
                            {label(id)}
                          </span>
                        ))}
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export const tagRelationshipsPanel = {
  type: 'tag-relationships',
  title: 'Tag Relationships',
  description: "Edit a tag's aliases, parents, and children, and view its relationships across domains.",
  component: TagRelationshipsPanel,
  configVersion: 1,
  singleton: true,
} as const;
