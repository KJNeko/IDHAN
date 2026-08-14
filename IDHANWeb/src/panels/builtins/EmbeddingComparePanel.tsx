/**
 * Embedding Compare: two images side by side, and how far a phrase sits from each.
 *
 * Search ranks a collection against one query, so it cannot answer whether a phrase describes A or B
 * better. Terms here carry no weight and no sign: each is scored on its own, and cosine distance is
 * scale-invariant in the query vector, so a weight could not move a number on this screen.
 */

import { useCallback, useEffect, useMemo, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import { compareTermLabel, parseCompareTerm, type CompareTerm } from './embeddingTerms';
import {
    barFraction,
    buildCompareRows,
    COMPARE_SORT_MODES,
    deltaScale,
    indexRowsByLabel,
    orderTerms,
    type CompareRow,
    type CompareSortMode,
} from './compareRows';

interface EmbeddingModel {
  model_id: number;
  model_name: string;
  dimensions: number;
  available: boolean;
  supports_text: boolean;
}

interface CompareResponse {
  record_ids: number[];
  distances: number[][];
  pair_distance?: number;
  query_ms: number;
}

type Config = {
  modelName: string;
  slotA: number | null;
  slotB: number | null;
  terms: CompareTerm[];
    sortMode: CompareSortMode;
};

const DEFAULT_CONFIG: Config = {modelName: '', slotA: null, slotB: null, terms: [], sortMode: 'delta'};

/** Matches MAX_COMPARE_TERMS on the server. Enforced here too so seeding can say what it dropped. */
const MAX_TERMS = 64;

const SORT_VALUES = new Set<string>(COMPARE_SORT_MODES.map((mode) => mode.value));

function readConfig(raw: Partial<Config>): Config {
  return {
    modelName: typeof raw.modelName === 'string' ? raw.modelName : DEFAULT_CONFIG.modelName,
    slotA: typeof raw.slotA === 'number' ? raw.slotA : null,
    slotB: typeof raw.slotB === 'number' ? raw.slotB : null,
    terms: Array.isArray(raw.terms) ? raw.terms : DEFAULT_CONFIG.terms,
      sortMode:
          typeof raw.sortMode === 'string' && SORT_VALUES.has(raw.sortMode) ? raw.sortMode : DEFAULT_CONFIG.sortMode,
  };
}

/** v1 stored a `sortByDelta` boolean, which is the 'delta' and 'entered' modes under another name. */
function migrateConfig(config: unknown, fromVersion: number): Config {
    const raw = (config ?? {}) as Partial<Config> & { sortByDelta?: unknown };

    if (fromVersion < 2 && raw.sortMode === undefined) {
        return readConfig({...raw, sortMode: raw.sortByDelta === false ? 'entered' : 'delta'});
    }

    return readConfig(raw);
}

/** What a set of visible numbers was computed for, so a config edited since the run can be spotted. */
function signatureOf(config: Config): string {
  return JSON.stringify([config.modelName, config.slotA, config.slotB, config.terms.filter((t) => t.enabled)]);
}

interface Results {
  rows: CompareRow[];
  pairDistance: number | null;
  queryMs: number;
  signature: string;
}

/** One image slot: what is in it, and the two ways to put something there. */
function SlotCard({
  side,
  recordId,
  host,
  onChange,
}: {
  side: 'A' | 'B';
  recordId: number | null;
  host: PanelProps['host'];
  onChange: (id: number | null) => void;
}) {
  const [draft, setDraft] = useState('');

  const commitDraft = () => {
    const parsed = parseCompareTerm(draft);
    if (parsed?.kind !== 'record') {
      host.ui.toast('Type a record id, e.g. record:1234 or #1234.', { kind: 'error' });
      return;
    }
    onChange(parsed.recordId);
    setDraft('');
  };

  const setFromSelection = () => {
    const first = host.selection.get()[0];
    if (first === undefined) {
      host.ui.toast('Nothing is selected.', { kind: 'error' });
      return;
    }
    onChange(first);
  };

  return (
    <div className="cmp-slot">
        {/* thumbnailUrl's default size, which other panels also request, so a record already shown in
          the grid costs no second thumbnail here. */}
      <div className="cmp-slot-thumb">
        {recordId === null ? (
          <span className="muted">empty</span>
        ) : (
            <img src={host.records.thumbnailUrl(recordId)} alt={`record ${recordId}`}/>
        )}
      </div>

      <div className="cmp-slot-id">
        <strong>{side}</strong>
        <span className="muted">{recordId === null ? 'no record' : `#${recordId}`}</span>
        {recordId !== null && (
          <button type="button" className="cmp-slot-clear" onClick={() => onChange(null)} title="Clear this slot">
            &times;
          </button>
        )}
      </div>

      <div className="cmp-slot-actions">
        <button type="button" className="toolbar-button" onClick={setFromSelection}>
          Set from selection
        </button>
        <input
          type="text"
          className="search-input"
          value={draft}
          placeholder="record:1234"
          spellCheck={false}
          autoComplete="off"
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              commitDraft();
            }
          }}
        />
      </div>
    </div>
  );
}

export function EmbeddingComparePanel({ host }: PanelProps) {
  const [config, setConfigState] = useState<Config>(() => readConfig(host.settings.get() as Partial<Config>));
  const [models, setModels] = useState<EmbeddingModel[]>([]);
  const [selection, setSelection] = useState<readonly RecordId[]>(() => host.selection.get());
  const [draft, setDraft] = useState('');
  const [results, setResults] = useState<Results | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [seeding, setSeeding] = useState(false);

  const update = useCallback(
    (next: Partial<Config>) => {
      setConfigState((current) => {
        const merged = { ...current, ...next };
        host.settings.set(merged);
        return merged;
      });
    },
    [host],
  );

  useEffect(() => host.selection.subscribe(setSelection), [host]);

  useEffect(() => {
    let cancelled = false;

    void (async () => {
      try {
        const res = await host.http.fetch('/embeddings/models');
        if (!res.ok) throw new Error(`models request failed: ${res.status}`);

        const list: EmbeddingModel[] = await res.json();
        if (cancelled) return;

        setModels(list);

        setConfigState((current) => {
          if (current.modelName) return current;
          const first = list.find((m) => m.available) ?? list[0];
          if (!first) return current;
          const merged = { ...current, modelName: first.model_name };
          host.settings.set(merged);
          return merged;
        });
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : String(e));
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [host]);

  const selected = models.find((m) => m.model_name === config.modelName);
  const textEnabled = selected?.supports_text ?? false;
  const bothSlotsSet = config.slotA !== null && config.slotB !== null;

  /** Appends terms, dropping ones already listed. Without weights, a repeat cannot mean anything. */
  const addTerms = useCallback(
    (incoming: CompareTerm[]) => {
      const existing = new Set(config.terms.map(compareTermLabel));

      const added = incoming.filter((term) => {
        const label = compareTermLabel(term);
        if (existing.has(label)) return false;
        existing.add(label);
        return true;
      });

      const room = MAX_TERMS - config.terms.length;
      const fitted = added.slice(0, Math.max(0, room));

      if (fitted.length > 0) update({ terms: [...config.terms, ...fitted] });
      return { added: fitted.length, dropped: added.length - fitted.length };
    },
    [config.terms, update],
  );

  const addDraftTerm = useCallback(() => {
    const parsed = parseCompareTerm(draft);
    if (!parsed) return;

    if (parsed.kind === 'text' && !textEnabled) {
      setError('This model cannot embed text. Add a record reference instead, e.g. record:1234.');
      return;
    }

    setError(null);
    const { dropped } = addTerms([parsed]);
    if (dropped > 0) host.ui.toast(`At the ${MAX_TERMS}-term limit; nothing was added.`, { kind: 'error' });
    setDraft('');
  }, [draft, textEnabled, addTerms, host]);

  const seedFromTags = useCallback(async () => {
    if (config.slotA === null || config.slotB === null) {
      setError('Set both A and B before seeding from their tags.');
      return;
    }

    setError(null);
    setSeeding(true);

    try {
      const [tagsA, tagsB] = await Promise.all([
        host.tags.activeVerbose(config.slotA),
        host.tags.activeVerbose(config.slotB),
      ]);

      const ids = [...new Set([...tagsA, ...tagsB].map((tag) => tag.tag_id))];
      const texts = await host.tags.resolve(ids);

      const incoming: CompareTerm[] = [...texts.values()].map((text) => ({ kind: 'text', text, enabled: true }));
      const { added, dropped } = addTerms(incoming);

      if (dropped > 0) {
        // Never a silent cap: a truncated list would read as "these are all the tags they share".
        host.ui.toast(`Added ${added} tags; dropped ${dropped} over the ${MAX_TERMS}-term limit.`, { kind: 'error' });
      } else {
        host.ui.toast(`Added ${added} tag${added === 1 ? '' : 's'}.`, { kind: 'success' });
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setSeeding(false);
    }
  }, [host, config.slotA, config.slotB, addTerms]);

  const run = useCallback(async () => {
    if (config.slotA === null || config.slotB === null) {
      setError('Set both A and B first.');
      return;
    }

    setError(null);

    const enabled = config.terms.filter((term) => term.enabled);

    const body = {
      model_name: config.modelName,
      record_ids: [config.slotA, config.slotB],
      terms: enabled.map((term) =>
        term.kind === 'text' ? { type: 'text', text: term.text } : { type: 'record', record_id: term.recordId },
      ),
    };

    setBusy(true);
    try {
      const res = await host.http.fetch('/embeddings/compare', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });

      const text = await res.text();

      if (!res.ok) {
        setError(text || `compare failed: ${res.status}`);
        return;
      }

      const data: CompareResponse = JSON.parse(text);

      setResults({
        rows: buildCompareRows(enabled, data.distances),
        pairDistance: typeof data.pair_distance === 'number' ? data.pair_distance : null,
        queryMs: data.query_ms,
        signature: signatureOf(config),
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }, [host, config]);

    const rowsByLabel = useMemo(() => indexRowsByLabel(results?.rows ?? []), [results]);

    const orderedTerms = useMemo(
        () => orderTerms(config.terms, rowsByLabel, config.sortMode),
        [config.terms, rowsByLabel, config.sortMode],
    );

    const scale = useMemo(() => deltaScale(results?.rows ?? []), [results]);
  const stale = results !== null && results.signature !== signatureOf(config);

  return (
    <div className="panel-body cmp-panel">
      <div className="embed-controls">
        <label className="embed-field embed-field-model">
          <span>Model</span>
          <select value={config.modelName} onChange={(e) => update({ modelName: e.target.value })}>
            {models.length === 0 && <option value="">No models registered</option>}
            {models.map((model) => (
              <option key={model.model_id} value={model.model_name}>
                {model.model_name} ({model.dimensions}d){model.available ? '' : ' - unavailable'}
              </option>
            ))}
          </select>
        </label>
      </div>

      <div className="cmp-slots">
        <SlotCard side="A" recordId={config.slotA} host={host} onChange={(id) => update({ slotA: id })} />

        <div className="cmp-slot-gap">
          <span className="muted">A &harr; B</span>
          {/* `!= null` rather than a truthiness check: a genuine distance of 0 (the same record in
              both slots) is a real answer and must not read as "no result". */}
          <span className="cmp-pair-distance">
            {results?.pairDistance != null ? results.pairDistance.toFixed(3) : '—'}
          </span>
          {selection.length === 2 && (
            <button
              type="button"
              className="toolbar-button"
              onClick={() => update({ slotA: selection[0] ?? null, slotB: selection[1] ?? null })}
            >
              Use selection (2)
            </button>
          )}
        </div>

        <SlotCard side="B" recordId={config.slotB} host={host} onChange={(id) => update({ slotB: id })} />
      </div>

      {/* Shown rather than hidden: the reason the box refuses phrases beats a control that silently
          is not there when the model changes. References still work either way. */}
      {selected && !textEnabled && (
        <p className="embed-note">
          {selected.available
            ? 'This model ships without a text encoder. Reference records only.'
            : 'No loaded module provides this model, so text terms cannot be embedded.'}
        </p>
      )}

      <div className="embed-term-head">
        <span>
          Terms
          {config.terms.length > 0 && <span className="muted"> {config.terms.length}</span>}
        </span>
        <button
          type="button"
          className="toolbar-button"
          onClick={() => void seedFromTags()}
          disabled={seeding || !bothSlotsSet}
        >
          {seeding ? 'Reading tags…' : 'Add from tags on A and B'}
        </button>
      </div>

      <div className="embed-add-row">
        <input
          type="text"
          className="search-input"
          value={draft}
          placeholder={textEnabled ? 'blonde hair, or record:1234' : 'record:1234 or #1234'}
          spellCheck={false}
          autoComplete="off"
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              addDraftTerm();
            }
          }}
        />
        <button type="button" className="toolbar-button" onClick={addDraftTerm}>
          Add
        </button>
      </div>

        <label className="cmp-sort">
            <span>Sort</span>
            <select value={config.sortMode} onChange={(e) => update({sortMode: e.target.value as CompareSortMode})}>
                {COMPARE_SORT_MODES.map((mode) => (
                    <option key={mode.value} value={mode.value}>
                        {mode.label}
                    </option>
                ))}
            </select>
        </label>

        {/* One list, not two: a term and its two distances are the same thing seen from either end.
          The numeric columns and the bar are absent until a comparison has produced them. */}
        <div className={`cmp-term-list${results ? ' has-results' : ''}`}>
        {config.terms.length === 0 ? (
          <p className="embed-note">
            No terms yet. Type a phrase, or a record id as <code>record:1234</code>.
          </p>
        ) : (
            orderedTerms.map((term) => {
                const label = compareTermLabel(term);
                const row = rowsByLabel.get(label);
                const index = config.terms.indexOf(term);

                return (
                    <div key={label} className={`cmp-term${term.enabled ? '' : ' is-disabled'}`}>
                        <input
                            type="checkbox"
                            checked={term.enabled}
                            onChange={(e) =>
                                update({
                                    terms: config.terms.map((t, i) => (i === index ? {
                                        ...t,
                                        enabled: e.target.checked
                                    } : t)),
                                })
                            }
                            title="Include this term in the comparison"
                        />

                        <span className={`embed-term-label${term.kind === 'record' ? ' is-record' : ''}`} title={label}>
                  {label}
                </span>

                        {results && (
                            <>
                                <span className="cmp-row-distance">{row ? row.distanceA.toFixed(3) : ''}</span>

                                <div className="cmp-bar">
                                    {row && (
                                        <>
                                            <div className="cmp-bar-half left">
                                                {row.delta < 0 && (
                                                    <div
                                                        className="cmp-bar-fill toward-a"
                                                        style={{width: `${barFraction(row.delta, scale) * 100}%`}}
                                                    />
                                                )}
                                            </div>
                                            <div className="cmp-bar-half right">
                                                {row.delta > 0 && (
                                                    <div
                                                        className="cmp-bar-fill toward-b"
                                                        style={{width: `${barFraction(row.delta, scale) * 100}%`}}
                                                    />
                                                )}
                                            </div>
                                        </>
                                    )}
                                </div>

                                <span className="cmp-row-distance">{row ? row.distanceB.toFixed(3) : ''}</span>

                                <span className={`cmp-row-delta${row && row.delta < 0 ? ' toward-a' : ' toward-b'}`}>
                      {row ? `${row.delta >= 0 ? '+' : ''}${row.delta.toFixed(3)}` : 'not run'}
                    </span>
                            </>
                        )}

                        <button
                            type="button"
                            className="embed-term-remove"
                            onClick={() => update({terms: config.terms.filter((_, i) => i !== index)})}
                            title="Remove this term"
                        >
                            &times;
                        </button>
                    </div>
                );
            })
        )}
      </div>

      <div className="embed-footer">
        {error ? (
          <p className="embed-error">{error}</p>
        ) : (
          results && (
            <p className="muted embed-summary">
              {results.rows.length} term{results.rows.length === 1 ? '' : 's'} · {results.queryMs} ms
              {stale && ' · changed since this ran'}
            </p>
          )
        )}
        <button type="button" className="embed-run" onClick={() => void run()} disabled={busy || !bothSlotsSet}>
          {busy ? 'Comparing…' : 'Compare'}
        </button>
      </div>
    </div>
  );
}

export const embeddingComparePanel = {
  type: 'embedding-compare',
  title: 'Embedding Compare',
  description: 'Two records side by side: how far each phrase sits from each image.',
  component: EmbeddingComparePanel,
  defaultConfig: DEFAULT_CONFIG,
    configVersion: 2,
    migrateConfig,
} as const;
