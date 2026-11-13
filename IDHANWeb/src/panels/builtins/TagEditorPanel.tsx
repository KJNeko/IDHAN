import {PanelShortcut} from '../PanelShortcut';

import {useEffect, useMemo, useRef, useState} from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import type { AutocompleteResult, TagDomain, VerboseTag } from '../../api/types';

const DEBOUNCE_MS = 130;

type DomainSort = 'tag-count' | 'name-asc' | 'name-desc';
type TagSort = 'name-asc' | 'name-desc';

type Config = {
    domainSort: DomainSort;
    tagSort: TagSort;
};

const DEFAULT_CONFIG: Config = {domainSort: 'tag-count', tagSort: 'name-asc'};

function readConfig(raw: unknown): Config {
    const saved = raw !== null && typeof raw === 'object' ? raw as Partial<Config> : {};
    return {
        domainSort: saved.domainSort === 'name-asc' || saved.domainSort === 'name-desc' ? saved.domainSort : DEFAULT_CONFIG.domainSort,
        tagSort: saved.tagSort === 'name-desc' ? saved.tagSort : DEFAULT_CONFIG.tagSort,
    };
}

interface ResolvedTag extends VerboseTag {
  text: string;
  aliasedFromText: string[];
  inheritedFromText: string[];
}

interface TagTreeNode {
    tag: ResolvedTag;
    children: TagTreeNode[];
    otherOrigins: string[];
    missingOrigins: string[];
    nested: boolean;
}

function compareTags(a: ResolvedTag, b: ResolvedTag): number {
    return a.text.localeCompare(b.text) || a.tag_id - b.tag_id;
}

/** Arrange the server's resolved provenance for display; never infer additional relationships. */
function buildTagTree(tags: ResolvedTag[]): TagTreeNode[] {
    const nodes: TagTreeNode[] = [...tags].sort(compareTags).map(tag => ({
        tag, children: [], otherOrigins: [], missingOrigins: [], nested: false,
    }));
    const byId = new Map(nodes.map(node => [node.tag.tag_id, node]));
    // An inherited origin can use the original alias id rather than the effective tag id.
    for (const node of nodes) {
        for (const alias of node.tag.aliased_from) {
            if (!byId.has(alias)) byId.set(alias, node);
        }
    }
    const candidates = new Map<TagTreeNode, Set<TagTreeNode>>();
    for (const implied of nodes) {
        for (let index = 0; index < implied.tag.inherited_from.length; index++) {
            const origin = implied.tag.inherited_from[index]!;
            const source = byId.get(origin);
            if (!source) {
                implied.missingOrigins.push(implied.tag.inheritedFromText[index] ?? `#${origin}`);
                continue;
            }
            if (source === implied) continue;
            const sources = candidates.get(implied) ?? new Set<TagTreeNode>();
            sources.add(source);
            candidates.set(implied, sources);
        }
    }
    const primaryParents = new Map<TagTreeNode, TagTreeNode>();
    for (const node of nodes) {
        const parents = [...(candidates.get(node) ?? [])].sort((a, b) => compareTags(a.tag, b.tag));
        for (const parent of parents) {
            let ancestor: TagTreeNode | undefined = parent;
            while (ancestor && ancestor !== node) ancestor = primaryParents.get(ancestor);
            if (ancestor === node) continue;
            primaryParents.set(node, parent);
            parent.children.push(node);
            node.nested = true;
            break;
        }
        node.otherOrigins = parents.filter(parent => parent !== primaryParents.get(node)).map(parent => parent.tag.text);
    }
    return nodes.filter(node => !node.nested);
}

/** Sort siblings after building the hierarchy, so the primary parent stays stable. */
function sortTagTree(nodes: TagTreeNode[], order: TagSort): TagTreeNode[] {
    for (const node of nodes) sortTagTree(node.children, order);
    return nodes.sort((a, b) => compareTagsForSort(a.tag, b.tag, order));
}

function compareTagsForSort(a: ResolvedTag, b: ResolvedTag, order: TagSort): number {
    if (order === 'name-desc') return -compareTags(a, b);
    return compareTags(a, b);
}

interface DomainTagGroup {
    domain: TagDomain;
    tags: ResolvedTag[] | null;
}

function groupDomains(domains: TagDomain[], tags: ResolvedTag[] | null, order: DomainSort): DomainTagGroup[] {
    const byDomain = new Map<number, ResolvedTag[]>();
    for (const tag of tags ?? []) {
        const rows = byDomain.get(tag.tag_domain_id) ?? [];
        rows.push(tag);
        byDomain.set(tag.tag_domain_id, rows);
    }
    const groups = domains.map(domain => ({
        domain,
        tags: tags === null ? null : byDomain.get(domain.tag_domain_id) ?? []
    }));
    return groups.sort((a, b) => {
        if (order === 'tag-count') {
            const countDifference = (b.tags?.length ?? 0) - (a.tags?.length ?? 0);
            if (countDifference !== 0) return countDifference;
        }
        const nameDifference = a.domain.domain_name.localeCompare(b.domain.domain_name);
        if (nameDifference !== 0) return order === 'name-desc' ? -nameDifference : nameDifference;
        return a.domain.tag_domain_id - b.domain.tag_domain_id;
    });
}

interface TagTreeRowsProps {
    nodes: TagTreeNode[];
    busy: boolean;
    targetCount: number;
    onRemove: (tagId: number, text: string) => void;
}

function TagTreeRows({nodes, busy, targetCount, onRemove}: TagTreeRowsProps) {
    return nodes.map(node => {
        const {tag} = node;
        const aliases = [...new Map(tag.aliased_from.map((id, index) =>
            [id, tag.aliasedFromText[index] ?? `#${id}`] as const))];
        const aliasNames = aliases.map(([, text]) => text);
        const missingOrigins = [...new Set(node.missingOrigins)];
        const provenance = [
            aliases.length > 0 ? `Aliased from ${aliasNames.join(', ')}` : '',
            tag.inheritedFromText.length > 0 ? `Inherited from ${[...new Set(tag.inheritedFromText)].join(', ')}` : '',
        ].filter(Boolean).join('; ');
        return <li key={tag.tag_id} className="tag-branch" title={provenance || undefined}>
            <div className="tag-item">
        <span className="tag-text">
          {tag.text}
            {aliases.length > 0 &&
                <span className="tag-alias-origins" aria-label={`Original tags: ${aliasNames.join(', ')}`}>
            {aliases.map(([id, text]) => <span key={id} className="tag-alias-origin">
              <s title={`Replaced by ${tag.text}`}> {text}</s>
              <button type="button" className="tag-alias-remove" disabled={busy} onClick={() => onRemove(id, text)}
                      aria-label={`Remove ${text} from ${targetCount} record${targetCount === 1 ? '' : 's'}`}
                      title={`Remove original tag ${text} from ${targetCount} selected record${targetCount === 1 ? '' : 's'}`}>×</button>
            </span>)}
          </span>}
            {node.otherOrigins.length > 0 && <span
                className="tag-provenance"> ({node.nested ? 'also from' : 'from'}: {node.otherOrigins.join(', ')})</span>}
            {missingOrigins.length > 0 && <span className="tag-provenance"> (from: {missingOrigins.join(', ')})</span>}
        </span>
                {tag.explicit && <button type="button" className="tag-remove" disabled={busy}
                                         onClick={() => onRemove(tag.tag_id, tag.text)}
                                         aria-label={`Remove ${tag.text} from ${targetCount} record${targetCount === 1 ? '' : 's'}`}
                                         title={`Remove from ${targetCount} selected record${targetCount === 1 ? '' : 's'}`}>×</button>}
            </div>
            {node.children.length > 0 && <ul className="tag-list tag-children" aria-label={`Implied by ${tag.text}`}>
                <TagTreeRows nodes={node.children} busy={busy} targetCount={targetCount} onRemove={onRemove}/>
            </ul>}
        </li>;
    });
}

async function loadTags(host: PanelProps['host'], recordId: RecordId, signal: AbortSignal): Promise<ResolvedTag[]> {
    const verbose = await host.tags.activeVerbose(recordId, signal);
    if (signal.aborted) return [];
    const ids = [...new Set(verbose.flatMap(tag => [tag.tag_id, ...tag.aliased_from, ...tag.inherited_from]))];
    const resolved = await host.tags.resolve(ids);
    return verbose.map(tag => ({
        ...tag,
        text: resolved.get(tag.tag_id) ?? `#${tag.tag_id}`,
        aliasedFromText: tag.aliased_from.map(id => resolved.get(id) ?? `#${id}`),
        inheritedFromText: tag.inherited_from.map(id => resolved.get(id) ?? `#${id}`),
    })).sort((a, b) => a.text.localeCompare(b.text));
}

function TagEditorPanel({host}: PanelProps) {
    const [config, setConfig] = useState(() => readConfig(host.settings.get()));
  const [domains, setDomains] = useState<TagDomain[]>([]);
  const [selection, setSelection] = useState<readonly RecordId[]>(() => host.selection.get());
    const [tagState, setTagState] = useState<{
        recordId: RecordId | null; rows: ResolvedTag[] | null; error?: string;
    }>({recordId: null, rows: null});
    const [reload, setReload] = useState(0);
    const [domainReload, setDomainReload] = useState(0);
    const [domainsLoading, setDomainsLoading] = useState(true);
    const [domainError, setDomainError] = useState<string | null>(null);
  const focused = selection.length > 0 ? selection[selection.length - 1]! : null;
    const hasDomains = domains.length > 0;
    const tags = tagState.recordId === focused ? tagState.rows : null;
    const loadError = tagState.recordId === focused ? tagState.error : undefined;
    const groups = useMemo(() => groupDomains(domains, tags, config.domainSort), [domains, tags, config.domainSort]);

    function updateConfig(next: Partial<Config>) {
        const updated = {...config, ...next};
        setConfig(updated);
        host.settings.set(updated);
    }

  useEffect(() => host.selection.subscribe(setSelection), [host]);

  useEffect(() => {
      const controller = new AbortController();
      setDomainsLoading(true);
      setDomainError(null);
      host.tags.listDomains(controller.signal).then(list => {
          if (!controller.signal.aborted) setDomains(list);
      }).catch((error: unknown) => {
          if (!controller.signal.aborted) setDomainError(`Could not load tag domains: ${error instanceof Error ? error.message : String(error)}`);
      }).finally(() => {
          if (!controller.signal.aborted) setDomainsLoading(false);
      });
      return () => controller.abort();
  }, [host, domainReload]);

  useEffect(() => {
      if (focused === null || !hasDomains) return;
      const controller = new AbortController();
      setTagState(current => ({recordId: focused, rows: current.recordId === focused ? current.rows : null}));
      void loadTags(host, focused, controller.signal).then(rows => {
          if (!controller.signal.aborted) setTagState({recordId: focused, rows});
      }).catch((error: unknown) => {
          if (!controller.signal.aborted) setTagState(current => {
              const rows = current.recordId === focused ? current.rows : null;
              return {
                  recordId: focused, rows,
                  error: `Could not ${rows === null ? 'load' : 'refresh'} tags: ${error instanceof Error ? error.message : String(error)}`
              };
          });
      });
      return () => controller.abort();
  }, [host, focused, hasDomains, reload]);

    if (focused === null) {
		return <div className="panel-body"><p className="muted">No record selected.</p></div>;
    }

    return (
        <div className="panel-body tag-editor">
            <div className="tag-editor-head">
        <span className="muted tag-editor-scope">
          {selection.length > 1 ? `Editing ${selection.length.toLocaleString()} records · showing #${focused}` : `#${focused}`}
            {tags !== null && ` · ${tags.length.toLocaleString()} tag${tags.length === 1 ? '' : 's'}`}
        </span>
                <div className="tag-editor-sorts">
                    <label className="tag-domain">Domains
                        <select aria-label="Sort domains" value={config.domainSort}
                                onChange={event => updateConfig({domainSort: event.target.value as DomainSort})}>
                            <option value="tag-count">Tag Count</option>
                            <option value="name-asc">Name A–Z</option>
                            <option value="name-desc">Name Z–A</option>
                        </select>
                    </label>
                    <label className="tag-domain">Tags
                        <select aria-label="Sort tags" value={config.tagSort}
                                onChange={event => updateConfig({tagSort: event.target.value as TagSort})}>
                            <option value="name-asc">Name A–Z</option>
                            <option value="name-desc">Name Z–A</option>
                        </select>
                    </label>
                </div>
            </div>
            {domainError && <div className="tag-editor-error">
                <p className="error" role="alert">{domainError}</p>
                <button type="button" className="toolbar-button"
                        onClick={() => setDomainReload(current => current + 1)}>Retry
                </button>
            </div>}
            {domainsLoading && !hasDomains && <p className="muted" role="status">Loading domains…</p>}
            {!domainsLoading && !domainError && !hasDomains &&
                <div className="panel-row"><span className="muted">No tag domains.</span><PanelShortcut
                    type="tag-domain-manager">Add tag domain</PanelShortcut></div>}
            {loadError && <div className="tag-editor-error">
                <p className="error" role="alert">{loadError}</p>
                <button type="button" className="toolbar-button"
                        onClick={() => setReload(current => current + 1)}>Retry
                </button>
            </div>}
            {hasDomains && tags === null && !loadError && <p className="muted" role="status">Loading tags…</p>}
            <div className="tag-editor-domains">
                {groups.map(({domain, tags: domainTags}) => <DomainTagEditor key={domain.tag_domain_id} host={host}
                                                                             domain={domain}
                                                                             selection={selection} tags={domainTags}
                                                                             tagSort={config.tagSort}
                                                                             onChanged={() => setReload(current => current + 1)}/>)}
            </div>
        </div>
    );
}

interface DomainTagEditorProps {
    host: PanelProps['host'];
    domain: TagDomain;
    selection: readonly RecordId[];
    tags: ResolvedTag[] | null;
    tagSort: TagSort;
    onChanged: () => void;
}

function DomainTagEditor({host, domain, selection, tags, tagSort, onChanged}: DomainTagEditorProps) {
    const domainId = domain.tag_domain_id;
    const tree = useMemo(() => sortTagTree(buildTagTree(tags ?? []), tagSort), [tags, tagSort]);
    const focused = selection.length > 0 ? selection[selection.length - 1]! : null;
    const [mutationError, setMutationError] = useState<string | null>(null);
    const mutationPending = useRef(false);
    const [busy, setBusy] = useState(false);
    const [input, setInput] = useState('');
    const [active, setActive] = useState(false);
    const [suggestions, setSuggestions] = useState<AutocompleteResult[]>([]);
    const [autocompleteError, setAutocompleteError] = useState<string | null>(null);
    const [highlight, setHighlight] = useState(-1);

  useEffect(() => {
      setSuggestions([]);
      setAutocompleteError(null);
      setHighlight(-1);
    const token = input.trim();
      if (token.length < 2 || focused === null || busy || !active) return;
    const controller = new AbortController();
    const timer = setTimeout(() => {
      host.tags
          .autocomplete(token, {domain: domainId, limit: 100}, controller.signal)
        .then((results) => {
          if (!controller.signal.aborted) setSuggestions(results);
        })
          .catch((error: unknown) => {
              if (!controller.signal.aborted) setAutocompleteError(`Tag suggestions unavailable: ${error instanceof Error ? error.message : String(error)}`);
          });
    }, DEBOUNCE_MS);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [host, input, domainId, focused, busy, active]);

    const editTargets = [...selection];

  async function addTag(text: string) {
    const tag = text.trim();
      if (tag.length === 0 || editTargets.length === 0 || mutationPending.current) return;
      const submittedInput = input;
      mutationPending.current = true;
    setBusy(true);
      setMutationError(null);
      try {
          await host.tags.addToRecords(editTargets, [tag], domainId);
          setInput(current => current === submittedInput ? '' : current);
          setSuggestions([]);
          setHighlight(-1);
          onChanged();
    } catch (error) {
          setMutationError(`Could not add "${tag}" to ${editTargets.length} record${editTargets.length === 1 ? '' : 's'}: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
          mutationPending.current = false;
      setBusy(false);
    }
  }

    async function removeTag(tagId: number, text: string) {
        const applied = tags?.some(tag => (tag.explicit && tag.tag_id === tagId) || tag.aliased_from.includes(tagId));
        if (!applied || editTargets.length === 0 || mutationPending.current) return;
        mutationPending.current = true;
    setBusy(true);
        setMutationError(null);
    try {
        await host.tags.removeFromRecords(editTargets, [tagId], domainId);
        onChanged();
    } catch (error) {
        setMutationError(`Could not remove "${text}" from ${editTargets.length} record${editTargets.length === 1 ? '' : 's'}: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
        mutationPending.current = false;
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

    return (
        <section className="tag-editor-domain" aria-label={domain.domain_name}>
            <header className="tag-editor-domain-head">
                <h2>{domain.domain_name}</h2>
                {tags !== null && <span className="muted">{tags.length.toLocaleString()}</span>}
            </header>
            <form className="tag-add"
                  onFocus={() => setActive(true)}
                  onBlur={event => {
                      if (!event.currentTarget.contains(event.relatedTarget)) setActive(false);
                  }}
                  onSubmit={event => {
                      event.preventDefault();
                      void addTag(input);
                  }}>
        <input
            aria-label={`Add tag to ${domain.domain_name}`}
          className="search-input"
          value={input}
          placeholder="Add a tag…"
            disabled={busy}
          onChange={(e) => {
            setInput(e.target.value);
              setActive(true);
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
                  disabled={busy}
                  onClick={() => void addTag(s.text)}
                  onMouseEnter={() => setHighlight(i)}
                  onMouseDown={(e) => {
                    e.preventDefault();
                  }}
                >
                  <span className="suggestion-text">{s.text}</span>
                  {s.count !== undefined && <span className="suggestion-count">{s.count.toLocaleString()}</span>}
                </button>
              </li>
            ))}
          </ul>
        )}
            </form>

            {mutationError && <p className="error" role="alert">{mutationError}</p>}
            {autocompleteError && <p className="error" role="alert">{autocompleteError}</p>}
      {tags !== null && tags.length > 0 && (
          <ul className="tag-list" aria-label={`Tags in ${domain.domain_name} for record ${focused}`}>
              <TagTreeRows nodes={tree} busy={busy} targetCount={editTargets.length}
                           onRemove={(tagId, text) => void removeTag(tagId, text)}/>
        </ul>
      )}
        </section>
  );
}

export const tagEditorPanel = {
  type: 'tag-editor',
  title: 'Tag Editor',
	description: 'Record tags.',
  component: TagEditorPanel,
    defaultConfig: DEFAULT_CONFIG,
  configVersion: 1,
} as const;
