/**
 * Tag Editor — views the focused record's active tags with provenance (explicit vs. aliased-in vs.
 * inherited-from-a-parent, via /records/{id}/tags/active/verbose) and edits tags across the whole
 * selection. Viewing one record while editing many is the standard booru model; the header makes the
 * edit scope explicit.
 *
 * The verbose endpoint returns ids only, so text comes from the shared, coalesced tag-info cache
 * (host.tags.resolve). Only explicitly-applied tags can be removed — aliased/inherited tags are
 * computed, so they show as read-only with a badge.
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import type { AutocompleteResult, TagDomain, VerboseTag } from '../../api/types';

const DEBOUNCE_MS = 130;

interface ResolvedTag extends VerboseTag {
  text: string;
  aliasedFromText: string[];
  inheritedFromText: string[];
}

function badge(tag: VerboseTag): { label: string; className: string } | null {
  if (tag.explicit) return null;
  if (tag.inherited_from.length > 0) return { label: 'inherited', className: 'inherited' };
  if (tag.aliased_from.length > 0) return { label: 'alias', className: 'alias' };
  return null;
}

function TagEditorPanel({ host }: PanelProps) {
  const [domains, setDomains] = useState<TagDomain[]>([]);
  const [domainId, setDomainId] = useState<number | null>(null);
  const [selection, setSelection] = useState<readonly RecordId[]>(() => host.selection.get());
  const [tags, setTags] = useState<ResolvedTag[] | null>(null);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [input, setInput] = useState('');
  const [suggestions, setSuggestions] = useState<AutocompleteResult[]>([]);
  const [highlight, setHighlight] = useState(-1);

  const focused = selection.length > 0 ? selection[selection.length - 1]! : null;

  useEffect(() => host.selection.subscribe(setSelection), [host]);

  // Domains, once.
  useEffect(() => {
    let cancelled = false;
    host.tags
      .listDomains()
      .then((list) => {
        if (cancelled) return;
        setDomains(list);
        setDomainId((current) => current ?? list[0]?.tag_domain_id ?? null);
      })
      .catch(() => {
        // Leave the picker empty; the panel still renders a helpful message.
      });
    return () => {
      cancelled = true;
    };
  }, [host]);

  const refresh = useCallback(async () => {
    if (focused === null || domainId === null) {
      setTags(null);
      return;
    }
    setLoading(true);
    try {
      const verbose = await host.tags.activeVerbose(focused);
      const inDomain = verbose.filter((t) => t.tag_domain_id === domainId);
      const allIds = inDomain.flatMap((t) => [t.tag_id, ...t.aliased_from, ...t.inherited_from]);
      const resolved = await host.tags.resolve(allIds);
      const rows: ResolvedTag[] = inDomain.map((t) => ({
        ...t,
        text: resolved.get(t.tag_id) ?? `#${t.tag_id}`,
        aliasedFromText: t.aliased_from.map((id) => resolved.get(id) ?? `#${id}`),
        inheritedFromText: t.inherited_from.map((id) => resolved.get(id) ?? `#${id}`),
      }));
      rows.sort((a, b) => a.text.localeCompare(b.text));
      setTags(rows);
    } catch (error) {
      host.ui.toast(`Could not load tags: ${error instanceof Error ? error.message : String(error)}`, { kind: 'error' });
      setTags(null);
    } finally {
      setLoading(false);
    }
  }, [host, focused, domainId]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // Debounced, abortable autocomplete for the add box (cache/prefix-reuse live host-side).
  useEffect(() => {
    const token = input.trim();
    if (token.length < 2) {
      setSuggestions([]);
      return;
    }
    const controller = new AbortController();
    const timer = setTimeout(() => {
      host.tags
        .autocomplete(token, { limit: 100 }, controller.signal)
        .then((results) => {
          if (!controller.signal.aborted) setSuggestions(results);
        })
        .catch(() => {});
    }, DEBOUNCE_MS);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [host, input]);

  const editTargets = selection.length > 0 ? [...selection] : focused !== null ? [focused] : [];

  async function addTag(text: string) {
    const tag = text.trim();
    if (tag.length === 0 || domainId === null || editTargets.length === 0) return;
    setBusy(true);
    setInput('');
    setSuggestions([]);
    setHighlight(-1);
    try {
      await host.tags.addToRecords(editTargets, [tag], domainId);
      host.ui.toast(`Added "${tag}" to ${editTargets.length} record${editTargets.length === 1 ? '' : 's'}`, {
        kind: 'success',
      });
      await refresh();
    } catch (error) {
      host.ui.toast(`Add failed: ${error instanceof Error ? error.message : String(error)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  async function removeTag(tag: ResolvedTag) {
    if (domainId === null || editTargets.length === 0) return;
    setBusy(true);
    try {
      await host.tags.removeFromRecords(editTargets, [tag.tag_id], domainId);
      host.ui.toast(`Removed "${tag.text}" from ${editTargets.length} record${editTargets.length === 1 ? '' : 's'}`, {
        kind: 'success',
      });
      await refresh();
    } catch (error) {
      host.ui.toast(`Remove failed: ${error instanceof Error ? error.message : String(error)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  function onKeyDown(event: React.KeyboardEvent<HTMLInputElement>) {
    switch (event.key) {
      case 'ArrowDown':
        if (suggestions.length > 0) {
          event.preventDefault();
          setHighlight((h) => (h + 1) % suggestions.length);
        }
        break;
      case 'ArrowUp':
        if (suggestions.length > 0) {
          event.preventDefault();
          setHighlight((h) => (h <= 0 ? suggestions.length - 1 : h - 1));
        }
        break;
      case 'Enter': {
        event.preventDefault();
        const chosen = highlight >= 0 ? suggestions[highlight]?.text : undefined;
        void addTag(chosen ?? input);
        break;
      }
      case 'Escape':
        setInput('');
        setSuggestions([]);
        setHighlight(-1);
        break;
    }
  }

  if (focused === null) {
    return (
      <div className="panel-body">
        <p className="muted">Select a record to edit its tags.</p>
      </div>
    );
  }

  return (
    <div className="panel-body tag-editor">
      <div className="tag-editor-head">
        <span className="muted">
          Editing {editTargets.length.toLocaleString()} record{editTargets.length === 1 ? '' : 's'}
          {selection.length > 1 && `; tags shown for #${focused}`}
        </span>
        {domains.length > 0 && (
          <label className="tag-domain">
            Domain
            <select value={domainId ?? ''} onChange={(e) => setDomainId(Number(e.target.value))}>
              {domains.map((d) => (
                <option key={d.tag_domain_id} value={d.tag_domain_id}>
                  {d.domain_name}
                </option>
              ))}
            </select>
          </label>
        )}
      </div>

      <div className="tag-add">
        <input
          className="search-input"
          value={input}
          placeholder="Add a tag…"
          disabled={busy || domainId === null}
          onChange={(e) => {
            setInput(e.target.value);
            setHighlight(-1);
          }}
          onKeyDown={onKeyDown}
          spellCheck={false}
          autoComplete="off"
        />
        {suggestions.length > 0 && (
          <ul className="search-suggestions">
            {suggestions.map((s, i) => (
              <li key={s.tag_id}>
                <button
                  type="button"
                  className={`suggestion${i === highlight ? ' active' : ''}`}
                  onMouseEnter={() => setHighlight(i)}
                  onMouseDown={(e) => {
                    e.preventDefault();
                    void addTag(s.text);
                  }}
                >
                  <span className="suggestion-text">{s.text}</span>
                  {s.count !== undefined && <span className="suggestion-count">{s.count.toLocaleString()}</span>}
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>

      {loading && tags === null && <p className="muted">Loading tags…</p>}
      {tags !== null && tags.length === 0 && <p className="muted">No tags in this domain.</p>}
      {tags !== null && tags.length > 0 && (
        <ul className="tag-list">
          {tags.map((tag) => {
            const b = badge(tag);
            const provenance = [
              tag.aliasedFromText.length > 0 ? `aliased from ${tag.aliasedFromText.join(', ')}` : '',
              tag.inheritedFromText.length > 0 ? `inherited from ${tag.inheritedFromText.join(', ')}` : '',
            ]
              .filter(Boolean)
              .join('; ');
            return (
              <li key={`${tag.tag_domain_id}:${tag.tag_id}`} className="tag-item" title={provenance || undefined}>
                <span className="tag-text">{tag.text}</span>
                {b && <span className={`tag-badge ${b.className}`}>{b.label}</span>}
                {tag.explicit && (
                  <button type="button" className="tag-remove" disabled={busy} onClick={() => void removeTag(tag)} title="Remove">
                    ×
                  </button>
                )}
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}

export const tagEditorPanel = {
  type: 'tag-editor',
  title: 'Tag Editor',
  description: "View a record's tags with provenance and edit tags across the selection.",
  component: TagEditorPanel,
  configVersion: 1,
} as const;
