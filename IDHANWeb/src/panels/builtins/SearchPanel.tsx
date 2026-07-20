/**
 * Search — the panel that drives everything downstream. The user builds a query out of tag chips
 * (text tags, `-negation`, and `system:` predicates typed verbatim), picks a sort, and runs it. The
 * ordered id set the server returns is published to `host.results`, which the grid and viewer page
 * against.
 *
 * Autocomplete is deliberately thin here: debounce + per-keystroke abort live in the hook below, while
 * the caching and prefix-extension reuse that make it feel instant live in the host (autocompleteCache),
 * so this panel only wires the UI. A 2-char minimum is enforced host-side.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { PanelProps } from '../../host/types';
import type { AutocompleteResult, SortOrder } from '../../api/types';

/** Matches the sort keys POST /search understands (parseSortType.hpp). */
const SORT_OPTIONS = [
  { value: 'import_time', label: 'Import time' },
  { value: 'creation_time', label: 'Record creation time' },
  { value: 'size', label: 'File size' },
  { value: 'modified_time', label: 'File modified time' },
  { value: 'mime', label: 'Filetype' },
  { value: 'hash', label: 'Hash' },
  { value: 'random', label: 'Random' },
  { value: 'duration', label: 'Duration' },
  { value: 'framerate', label: 'Framerate' },
  { value: 'has_audio', label: 'Has audio' },
  { value: 'width', label: 'Width' },
  { value: 'height', label: 'Height' },
  { value: 'ratio', label: 'Aspect ratio' },
  { value: 'num_pixels', label: 'Resolution (pixels)' },
  { value: 'num_tags', label: 'Number of tags' },
] as const;

type SortBy = (typeof SORT_OPTIONS)[number]['value'];

type Config = {
  tags: string[];
  sortBy: SortBy;
  sortOrder: SortOrder;
};

const DEFAULT_CONFIG: Config = { tags: [], sortBy: 'import_time', sortOrder: 'desc' };
const DEBOUNCE_MS = 130;

function readConfig(raw: Partial<Config>): Config {
  return {
    tags: Array.isArray(raw.tags) ? raw.tags : DEFAULT_CONFIG.tags,
    sortBy: SORT_OPTIONS.some((o) => o.value === raw.sortBy) ? (raw.sortBy as SortBy) : DEFAULT_CONFIG.sortBy,
    sortOrder: raw.sortOrder === 'asc' ? 'asc' : DEFAULT_CONFIG.sortOrder,
  };
}

/** The tag text to autocomplete: the current token minus a leading `-`. `system:` tokens don't autocomplete. */
function queryToken(input: string): string | null {
  const trimmed = input.trim();
  if (trimmed.length === 0 || trimmed.startsWith('system:')) return null;
  return trimmed.startsWith('-') ? trimmed.slice(1) : trimmed;
}

/** Debounced, per-keystroke-abortable autocomplete. Cache/prefix-reuse is host-side; this is just UI glue. */
function useAutocomplete(host: PanelProps['host'], input: string): AutocompleteResult[] {
  const [suggestions, setSuggestions] = useState<AutocompleteResult[]>([]);

  useEffect(() => {
    const token = queryToken(input);
    if (token === null) {
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
        .catch(() => {
          // Aborted or transient; leave the last suggestions in place.
        });
    }, DEBOUNCE_MS);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [host, input]);

  return suggestions;
}

function SearchPanel({ host }: PanelProps) {
  const initial = readConfig(host.settings.get() as Partial<Config>);
  const [tags, setTags] = useState<string[]>(initial.tags);
  const [sortBy, setSortBy] = useState<SortBy>(initial.sortBy);
  const [sortOrder, setSortOrder] = useState<SortOrder>(initial.sortOrder);
  const [input, setInput] = useState('');
  const [highlight, setHighlight] = useState(-1);
  const [running, setRunning] = useState(false);
  const [summary, setSummary] = useState<string | null>(null);

  const suggestions = useAutocomplete(host, input);
  const negated = input.trim().startsWith('-');

  const persist = useCallback(
    (next: Partial<Config>) => host.settings.set(next),
    [host],
  );

  const runSearch = useCallback(
    async (queryTags: string[], by: SortBy, order: SortOrder) => {
      setRunning(true);
      try {
        const response = await host.search.run({
          tags: queryTags,
          sort: { by, order },
        });
        const ids = Int32Array.from(response.record_ids);
        host.results.set({ ids, queryMs: response.query_ms, query: queryTags });
        setSummary(`${ids.length.toLocaleString()} result${ids.length === 1 ? '' : 's'} · ${response.query_ms} ms`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        host.ui.toast(`Search failed: ${message}`, { kind: 'error' });
        setSummary(null);
      } finally {
        setRunning(false);
      }
    },
    [host],
  );

  // Restore results on (re)mount when a prior query was persisted; a fresh panel stays idle until asked.
  const bootRef = useRef(false);
  useEffect(() => {
    if (bootRef.current) return;
    bootRef.current = true;
    if (initial.tags.length > 0) void runSearch(initial.tags, initial.sortBy, initial.sortOrder);
    // initial.* is captured once from settings on first render; intentionally a mount-only effect.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  function commitTags(next: string[]) {
    setTags(next);
    persist({ tags: next });
  }

  function addTag(tag: string) {
    const trimmed = tag.trim();
    if (trimmed.length === 0 || tags.includes(trimmed)) {
      setInput('');
      setHighlight(-1);
      return;
    }
    commitTags([...tags, trimmed]);
    setInput('');
    setHighlight(-1);
  }

  function acceptSuggestion(result: AutocompleteResult) {
    addTag(negated ? `-${result.text}` : result.text);
  }

  function removeTag(tag: string) {
    commitTags(tags.filter((t) => t !== tag));
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
      case 'Enter':
        event.preventDefault();
        if (highlight >= 0 && suggestions[highlight]) {
          acceptSuggestion(suggestions[highlight]);
        } else if (input.trim().length > 0) {
          addTag(input);
        } else {
          void runSearch(tags, sortBy, sortOrder);
        }
        break;
      case 'Backspace':
        if (input.length === 0 && tags.length > 0) {
          event.preventDefault();
          removeTag(tags[tags.length - 1]!);
        }
        break;
      case 'Escape':
        setInput('');
        setHighlight(-1);
        break;
    }
  }

  function changeSort(by: SortBy, order: SortOrder) {
    setSortBy(by);
    setSortOrder(order);
    persist({ sortBy: by, sortOrder: order });
    void runSearch(tags, by, order);
  }

  return (
    <div className="panel-body search-panel">
      <div className="search-chips">
        {tags.map((tag) => (
          <button
            key={tag}
            type="button"
            className={`chip${tag.startsWith('-') ? ' negated' : ''}${tag.startsWith('system:') ? ' system' : ''}`}
            onClick={() => removeTag(tag)}
            title="Remove"
          >
            {tag} <span className="chip-x">×</span>
          </button>
        ))}
      </div>

      <div className="search-input-wrap">
        <input
          className="search-input"
          value={input}
          placeholder="Add a tag, -negation, or system: predicate…"
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
                    acceptSuggestion(s);
                  }}
                >
                  <span className="suggestion-text">
                    {negated ? '-' : ''}
                    {s.text}
                  </span>
                  {s.count !== undefined && <span className="suggestion-count">{s.count.toLocaleString()}</span>}
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>

      <div className="search-controls">
        <label className="search-sort">
          Sort
          <select value={sortBy} onChange={(e) => changeSort(e.target.value as SortBy, sortOrder)}>
            {SORT_OPTIONS.map((o) => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </select>
        </label>
        <button
          type="button"
          className="search-order"
          onClick={() => changeSort(sortBy, sortOrder === 'desc' ? 'asc' : 'desc')}
          disabled={sortBy === 'random'}
          title={sortBy === 'random' ? 'Direction has no effect on a random sort' : sortOrder === 'desc' ? 'Descending' : 'Ascending'}
        >
          {sortBy === 'random' ? '⇅' : sortOrder === 'desc' ? '↓' : '↑'}
        </button>
        <button type="button" className="search-run" onClick={() => void runSearch(tags, sortBy, sortOrder)} disabled={running}>
          {running ? 'Searching…' : 'Search'}
        </button>
      </div>

      {summary && <p className="muted search-summary">{summary}</p>}
    </div>
  );
}

export const searchPanel = {
  type: 'search',
  title: 'Search',
  description: 'Build a tag query and publish the ordered result set to the grid.',
  component: SearchPanel,
  defaultConfig: DEFAULT_CONFIG,
  configVersion: 1,
} as const;
