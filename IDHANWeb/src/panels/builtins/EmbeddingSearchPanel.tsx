/**
 * Embedding Search — searches by meaning rather than by tag.
 *
 * A query is a signed weighted sum of unit vectors: text phrases through the model's text tower, and
 * reference records through their stored embeddings. Positive terms pull the query toward them,
 * negative terms push it away. The ordered result set is published to `host.results`, the same
 * channel the tag Search panel uses, so the grid and viewer page against it unchanged.
 *
 * Reference images are records already in the collection, never uploads: their vectors are already
 * in the table, so a reference costs a lookup rather than a model call.
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import { parseTermInput, termsToRequest, termsToTokens, type Term } from './embeddingTerms';

interface EmbeddingModel {
  model_id: number;
  model_name: string;
  dimensions: number;
  /** Whether a loaded module is currently routing this model name. */
  available: boolean;
  /** Whether that module has a text tower. False means record references only. */
  supports_text: boolean;
}

interface SearchResponse {
  record_ids: number[];
  distances: number[];
  query_ms: number;
}

type Config = {
  modelName: string;
  terms: Term[];
  limit: number;
};

const DEFAULT_CONFIG: Config = { modelName: '', terms: [], limit: 200 };

function readConfig(raw: Partial<Config>): Config {
  return {
    modelName: typeof raw.modelName === 'string' ? raw.modelName : DEFAULT_CONFIG.modelName,
    terms: Array.isArray(raw.terms) ? raw.terms : DEFAULT_CONFIG.terms,
    limit: typeof raw.limit === 'number' && raw.limit > 0 ? raw.limit : DEFAULT_CONFIG.limit,
  };
}

/** One term row: its label, weight, sign, and the controls that edit them in place. */
function TermRow({
  term,
  onChange,
  onRemove,
}: {
  term: Term;
  onChange: (next: Term) => void;
  onRemove: () => void;
}) {
  const label = term.kind === 'text' ? term.text : `record ${term.recordId}`;

  return (
    <div className={`embed-term${term.enabled ? '' : ' is-disabled'}`}>
      <input
        type="checkbox"
        checked={term.enabled}
        onChange={(e) => onChange({ ...term, enabled: e.target.checked })}
        title="Include this term in the query"
      />

      <span className="embed-term-label" title={label}>
        {label}
      </span>

      <button
        type="button"
        className="embed-term-sign"
        onClick={() => onChange({ ...term, positive: !term.positive })}
        title={term.positive ? 'Positive: pull results toward this' : 'Negative: push results away from this'}
      >
        {term.positive ? '+' : '-'}
      </button>

      <input
        type="number"
        className="embed-term-weight"
        value={term.weight}
        step={0.1}
        min={0}
        onChange={(e) => {
          const next = Number(e.target.value);
          if (Number.isFinite(next)) onChange({ ...term, weight: next });
        }}
        title="How strongly this term pulls the query"
      />

      <button type="button" onClick={onRemove} title="Remove this term">
        x
      </button>
    </div>
  );
}

export function EmbeddingSearchPanel({ host }: PanelProps) {
  const [config, setConfigState] = useState<Config>(() => readConfig(host.settings.get() as Partial<Config>));
  const [models, setModels] = useState<EmbeddingModel[]>([]);
  const [draft, setDraft] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

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

  useEffect(() => {
    let cancelled = false;

    void (async () => {
      try {
        const res = await host.http.fetch('/embeddings/models');
        if (!res.ok) throw new Error(`models request failed: ${res.status}`);

        const list: EmbeddingModel[] = await res.json();
        if (cancelled) return;

        setModels(list);

        // Pick a default only when nothing is chosen, so a saved layout keeps its model even if that
        // model is momentarily unavailable.
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

  const addTextTerm = useCallback(() => {
    const parsed = parseTermInput(draft);
    if (!parsed) return;

    update({ terms: [...config.terms, parsed] });
    setDraft('');
  }, [draft, config.terms, update]);

  const addSelectionAsReferences = useCallback(() => {
    const ids = host.selection.get();
    if (ids.length === 0) return;

    // Deduplicated against what is already there: adding the same record twice would double its
    // weight silently rather than visibly.
    const existing = new Set(config.terms.filter((t) => t.kind === 'record').map((t) => t.recordId));

    const added: Term[] = ids
      .filter((id) => !existing.has(id))
      .map((id) => ({ kind: 'record', recordId: id, weight: 1, positive: true, enabled: true }));

    if (added.length > 0) update({ terms: [...config.terms, ...added] });
  }, [host, config.terms, update]);

  const run = useCallback(async () => {
    setError(null);

    const body = {
      model_name: config.modelName,
      terms: termsToRequest(config.terms),
      limit: config.limit,
    };

    if (body.terms.length === 0) {
      setError('Add at least one term.');
      return;
    }

    setBusy(true);
    try {
      const res = await host.http.fetch('/embeddings/search', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });

      const text = await res.text();

      if (!res.ok) {
        // The server's messages name the offending record or model, so they are worth showing
        // verbatim rather than replacing with a generic failure.
        setError(text || `search failed: ${res.status}`);
        return;
      }

      const data: SearchResponse = JSON.parse(text);

      host.results.set({
        ids: Int32Array.from(data.record_ids),
        queryMs: data.query_ms,
        query: termsToTokens(config.terms),
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }, [host, config]);

  return (
    <div className="embed-search-panel">
      <label className="embed-field">
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

      <div className="embed-add-row">
        <input
          type="text"
          value={draft}
          disabled={!textEnabled}
          placeholder={textEnabled ? 'catgirl:0.5' : 'This model has no text encoder'}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              addTextTerm();
            }
          }}
        />
        <button type="button" onClick={addTextTerm} disabled={!textEnabled}>
          Add
        </button>
      </div>

      {/* Shown rather than hidden: a disabled input with a reason beats a control that silently
          is not there when the model changes. */}
      {selected && !textEnabled && (
        <p className="embed-note">
          {selected.available
            ? 'This model ships without a text encoder. Use reference records instead.'
            : 'No loaded module provides this model, so text terms cannot be embedded.'}
        </p>
      )}

      <button type="button" onClick={addSelectionAsReferences}>
        Add selection as reference
      </button>

      <div className="embed-term-list">
        {config.terms.length === 0 && <p className="embed-note">No terms yet.</p>}
        {config.terms.map((term, index) => (
          <TermRow
            key={term.kind === 'text' ? `t:${term.text}:${index}` : `r:${term.recordId}`}
            term={term}
            onChange={(next) => update({ terms: config.terms.map((t, i) => (i === index ? next : t)) })}
            onRemove={() => update({ terms: config.terms.filter((_, i) => i !== index) })}
          />
        ))}
      </div>

      <label className="embed-field">
        <span>Limit</span>
        <input
          type="number"
          value={config.limit}
          min={1}
          onChange={(e) => {
            const next = Number(e.target.value);
            if (Number.isFinite(next) && next > 0) update({ limit: next });
          }}
        />
      </label>

      <button type="button" className="embed-run" onClick={() => void run()} disabled={busy}>
        {busy ? 'Searching...' : 'Search'}
      </button>

      {error && <p className="embed-error">{error}</p>}
    </div>
  );
}

export const embeddingSearchPanel = {
  type: 'embedding-search',
  title: 'Embedding Search',
  description: 'Search by meaning: weighted text phrases and reference records.',
  component: EmbeddingSearchPanel,
  defaultConfig: DEFAULT_CONFIG,
  configVersion: 1,
} as const;
