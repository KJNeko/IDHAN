/**
 * A single-tag autocomplete picker that resolves to a concrete tag id (not just text). Shared by the
 * Tag Relationships panel, which needs two of them to name the two ends of a parent/alias pair. The
 * Tag Editor keeps its own inline box because it adds *text* (letting the server create tags on the
 * fly); relationship endpoints need existing ids, so this variant yields the picked AutocompleteResult.
 */

import { useEffect, useState } from 'react';
import type { HostApi } from '../../host/types';
import type { AutocompleteResult } from '../../api/types';

const DEBOUNCE_MS = 130;

interface TagPickerProps {
  host: HostApi;
  value: AutocompleteResult | null;
  onPick: (tag: AutocompleteResult | null) => void;
  domain?: number;
  placeholder?: string;
  disabled?: boolean;
}

export function TagPicker({ host, value, onPick, domain, placeholder, disabled }: TagPickerProps) {
  const [input, setInput] = useState('');
  const [suggestions, setSuggestions] = useState<AutocompleteResult[]>([]);
  const [highlight, setHighlight] = useState(-1);

  // 2-char minimum: 1-char prefixes scan the whole tag table server-side.
  useEffect(() => {
    const token = input.trim();
    if (token.length < 2) {
      setSuggestions([]);
      return;
    }
    const controller = new AbortController();
    const timer = setTimeout(() => {
      host.tags
        .autocomplete(token, { domain, limit: 50 }, controller.signal)
        .then((results) => {
          if (!controller.signal.aborted) setSuggestions(results);
        })
        .catch(() => {});
    }, DEBOUNCE_MS);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [host, input, domain]);

  function choose(tag: AutocompleteResult) {
    onPick(tag);
    setInput('');
    setSuggestions([]);
    setHighlight(-1);
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
        const chosen = highlight >= 0 ? suggestions[highlight] : suggestions[0];
        if (chosen) {
          event.preventDefault();
          choose(chosen);
        }
        break;
      }
      case 'Escape':
        setInput('');
        setSuggestions([]);
        setHighlight(-1);
        break;
    }
  }

  if (value) {
    return (
      <span className="tagpick-chip">
        <span className="tagpick-chip-text" title={`#${value.tag_id}`}>
          {value.text}
        </span>
        <button type="button" className="tagpick-clear" disabled={disabled} onClick={() => onPick(null)} title="Clear">
          ×
        </button>
      </span>
    );
  }

  return (
    <div className="tagpick">
      <input
        className="search-input"
        value={input}
        placeholder={placeholder ?? 'Find a tag…'}
        disabled={disabled}
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
                  choose(s);
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
  );
}
