/**
 * Search: the panel that drives everything downstream. The user builds a query out of tag chips (text
 * tags, `-negation`, and `system:` predicates), picks a sort, and runs it. The ordered
 * id set the server returns is published to `host.results`, which the grid and viewer page against.
 *
 * Only debounce and per-keystroke abort live here. Caching and prefix-extension reuse live in the
 * host (autocompleteCache), which also enforces a 2-char minimum. `system:` predicates never reach
 * the server: they are a fixed catalogue matched locally (systemPredicates) and listed above the
 * tag matches. The one exception is the value of a `system:mime` term, which comes from the mime
 * table (host/mimeCatalogue) and is fetched once per session.
 */

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import type { PanelProps } from '../../host/types';
import type {AutocompleteResult, SearchStep, SortOrder} from '../../api/types';
import {
    matchMimes,
    matchSystemPredicates,
    mimeArgument,
    systemCompletion,
    type MimeSuggestion,
    type SystemPredicate,
} from './systemPredicates';
import {loadMimes, peekMimes, type MimeCatalogue} from '../../host/mimeCatalogue';

/** Matches the sort keys POST /search understands (parseSortType.hpp). */
export const SORT_OPTIONS = [
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
    showBreakdown: boolean;
};

const DEFAULT_CONFIG: Config = {tags: [], sortBy: 'import_time', sortOrder: 'desc', showBreakdown: false};
const DEBOUNCE_MS = 130;

function readConfig(raw: Partial<Config>): Config {
  return {
    tags: Array.isArray(raw.tags) ? raw.tags : DEFAULT_CONFIG.tags,
    sortBy: SORT_OPTIONS.some((o) => o.value === raw.sortBy) ? (raw.sortBy as SortBy) : DEFAULT_CONFIG.sortBy,
    sortOrder: raw.sortOrder === 'asc' ? 'asc' : DEFAULT_CONFIG.sortOrder,
      showBreakdown: raw.showBreakdown === true,
  };
}

/** One term, as the breakdown table shows it: what its query cost against what it actually did. */
type TermRow = {
    term: string;
    /** Rows the term's own query returned. */
    fetched: number;
    /** Rows this term took out of the running result. Null for the term that established it. */
    removed: number | null;
    /** Rows still in the running result after this term, or the rows excluded when `inverted`. */
    left: number;
    /** True when `left` counts exclusions rather than matches (a search made only of negations). */
    inverted: boolean;
    micros: number;
};

/**
 * Pairs each `fetch` step with the `fold` step of the same name, so a row can show what a term cost
 * next to what it accomplished. They are matched by label rather than by position because the
 * fetches run concurrently and are recorded in completion order, not query order.
 */
function toTermRows(steps: SearchStep[]): TermRow[] {
    const rows: TermRow[] = [];
    let previous: number | null = null;

    for (const fold of steps.filter((s) => s.kind === 'fold')) {
        const fetch = steps.find((s) => s.kind === 'fetch' && s.step === fold.step);

        const removed =
            previous === null ? null : Math.max(0, fold.inverted ? fold.rows - previous : previous - fold.rows);

        rows.push({
            term: fold.step,
            fetched: fetch?.rows ?? 0,
            removed,
            left: fold.rows,
            inverted: fold.inverted,
            micros: fetch?.micros ?? 0,
        });
        previous = fold.rows;
    }

    return rows;
}

/**
 * Always milliseconds. Sub-ms values keep three decimals rather than collapsing to `0.0 ms`, since an
 * index-only tag lookup runs well under a millisecond.
 */
function formatMs(micros: number): string {
    if (micros <= 0) return '';
    const ms = micros / 1000;
    if (ms < 1) return `${ms.toFixed(3)} ms`;
    if (ms < 100) return `${ms.toFixed(1)} ms`;
    return `${Math.round(ms)} ms`;
}

/** The tag text to autocomplete: the current token minus a leading `-`. `system:` tokens don't autocomplete. */
function queryToken(input: string): string | null {
  const trimmed = input.trim();
  if (trimmed.length === 0 || trimmed.startsWith('system:')) return null;
  return trimmed.startsWith('-') ? trimmed.slice(1) : trimmed;
}

/** One row of the dropdown: a matched tag, a `system:` predicate name, or a mime for its value. */
type Suggestion =
    | { kind: 'tag'; tag: AutocompleteResult }
    | { kind: 'system'; predicate: SystemPredicate }
    | { kind: 'mime'; mime: MimeSuggestion };

function suggestionKey(suggestion: Suggestion): string {
    switch (suggestion.kind) {
        case 'tag':
            return `t${suggestion.tag.tag_id}`;
        case 'system':
            return `s${suggestion.predicate.text}`;
        case 'mime':
            return `m${suggestion.mime.text}`;
    }
}

/** Loads the mime table the first time a `system:mime` value is typed, then holds it for the session. */
function useMimeCatalogue(active: boolean): MimeCatalogue {
    const [catalogue, setCatalogue] = useState<MimeCatalogue>(peekMimes);

    useEffect(() => {
        if (!active || catalogue.entries.length > 0) return;
        let live = true;
        void loadMimes().then((loaded) => {
            if (live) setCatalogue(loaded);
        });
        return () => {
            live = false;
        };
    }, [active, catalogue]);

    return catalogue;
}

/** Debounced, per-keystroke-abortable autocomplete. Caching and prefix reuse are host-side. */
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
    const [showBreakdown, setShowBreakdown] = useState(initial.showBreakdown);
    const [steps, setSteps] = useState<SearchStep[] | null>(null);

    const breakdownRef = useRef(showBreakdown);
    breakdownRef.current = showBreakdown;

    const tagSuggestions = useAutocomplete(host, input);
    const systemSuggestions = useMemo(() => matchSystemPredicates(input), [input]);
    const mimeArg = useMemo(() => mimeArgument(input), [input]);
    const mimes = useMimeCatalogue(mimeArg !== null);
    const mimeSuggestions = useMemo(() => (mimeArg ? matchMimes(mimeArg, mimes) : []), [mimeArg, mimes]);
    const suggestions = useMemo<Suggestion[]>(
        () => [
            ...mimeSuggestions.map((mime) => ({kind: 'mime' as const, mime})),
            ...systemSuggestions.map((predicate) => ({kind: 'system' as const, predicate})),
            ...tagSuggestions.map((tag) => ({kind: 'tag' as const, tag})),
        ],
        [mimeSuggestions, systemSuggestions, tagSuggestions],
    );
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
            debug: breakdownRef.current,
        });
        const ids = Int32Array.from(response.record_ids);
        host.results.set({ ids, queryMs: response.query_ms, query: queryTags });
        setSummary(`${ids.length.toLocaleString()} result${ids.length === 1 ? '' : 's'} · ${response.query_ms} ms`);
          setSteps(response.stats ?? null);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        host.ui.toast(`Search failed: ${message}`, { kind: 'error' });
        setSummary(null);
          setSteps(null);
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

    function acceptSuggestion(suggestion: Suggestion) {
        if (suggestion.kind === 'tag') {
            addTag(negated ? `-${suggestion.tag.text}` : suggestion.tag.text);
            return;
        }
        if (suggestion.kind === 'mime') {
            addTag(suggestion.mime.text);
            return;
        }
        if (suggestion.predicate.kind === 'flag') {
            addTag(suggestion.predicate.text);
            return;
        }
        // An argument predicate is rejected by the server until a value follows it, so complete the
        // name and leave the caret in the box.
        setInput(systemCompletion(suggestion.predicate));
        setHighlight(-1);
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

    function toggleBreakdown() {
        const next = !showBreakdown;
        setShowBreakdown(next);
        breakdownRef.current = next;
        persist({showBreakdown: next});

        if (next && summary !== null) void runSearch(tags, sortBy, sortOrder);
        if (!next) setSteps(null);
    }

    const termRows = steps ? toTermRows(steps) : [];
    const pageStep = steps?.find((s) => s.kind === 'page') ?? null;

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
                <li key={suggestionKey(s)}>
                <button
                  type="button"
                  className={`suggestion${i === highlight ? ' active' : ''}${s.kind === 'tag' ? '' : ' system'}`}
                  onMouseEnter={() => setHighlight(i)}
                  onMouseDown={(e) => {
                    e.preventDefault();
                    acceptSuggestion(s);
                  }}
                >
                    {s.kind === 'mime' && (
                        <>
                            <span className="suggestion-text">{s.mime.value}</span>
                            {s.mime.hint && <span className="suggestion-hint">{s.mime.hint}</span>}
                        </>
                    )}
                    {s.kind === 'system' && (
                        <>
                            <span className="suggestion-text">{s.predicate.text}</span>
                            {s.predicate.hint && <span className="suggestion-hint">{s.predicate.hint}</span>}
                        </>
                    )}
                    {s.kind === 'tag' && (
                        <>
                      <span className="suggestion-text">
                        {negated ? '-' : ''}
                          {s.tag.text}
                      </span>
                            {s.tag.count !== undefined && (
                                <span className="suggestion-count">{s.tag.count.toLocaleString()}</span>
                            )}
                        </>
                    )}
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
          <button
              type="button"
              className={`search-breakdown-toggle${showBreakdown ? ' on' : ''}`}
              onClick={toggleBreakdown}
              aria-pressed={showBreakdown}
              title="Show how many records each term matched and removed"
          >
              Breakdown
        </button>
        <button type="button" className="search-run" onClick={() => void runSearch(tags, sortBy, sortOrder)} disabled={running}>
          {running ? 'Searching…' : 'Search'}
        </button>
      </div>

      {summary && <p className="muted search-summary">{summary}</p>}

        {showBreakdown && (
            <div className="search-breakdown">
                {termRows.length === 0 && pageStep === null ? (
                    <p className="muted">Run a search to see how each term narrowed it.</p>
                ) : (
                    <table className="breakdown-table">
                        <thead>
                        <tr>
                            <th scope="col">Term</th>
                            <th scope="col">Matched</th>
                            <th scope="col">Removed</th>
                            <th scope="col">Left</th>
                            <th scope="col">Time</th>
                        </tr>
                        </thead>
                        <tbody>
                        {termRows.map((row) => (
                            <tr
                                key={row.term}
                                className={row.removed === 0 ? 'no-effect' : undefined}
                                title={row.removed === 0 ? 'This term matched records but removed none from the result' : undefined}
                            >
                                <th scope="row">{row.term}</th>
                                <td>{row.fetched.toLocaleString()}</td>
                                <td>{row.removed === null ? '—' : row.removed.toLocaleString()}</td>
                                <td>
                                    {row.inverted && <span className="breakdown-not">not </span>}
                                    {row.left.toLocaleString()}
                                </td>
                                <td>{formatMs(row.micros)}</td>
                            </tr>
                        ))}
                        </tbody>
                        {pageStep && (
                            <tfoot>
                            <tr>
                                <th scope="row">Returned</th>
                                <td colSpan={2}/>
                                <td>{pageStep.rows.toLocaleString()}</td>
                                <td>{formatMs(pageStep.micros ?? 0)}</td>
                            </tr>
                            </tfoot>
                        )}
                    </table>
                )}
            </div>
        )}
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
