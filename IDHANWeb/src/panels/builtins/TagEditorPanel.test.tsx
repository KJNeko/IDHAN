import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps, RecordId} from '../../host/types';
import type {VerboseTag} from '../../api/types';
import {tagEditorPanel} from './TagEditorPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

function tag(id: number, domain = 1, explicit = true): VerboseTag {
    return {
        tag_id: id, tag_domain_id: domain, explicit,
        aliased_from: [], inherited_from: explicit ? [] : [10]
    };
}

function fixture(selection: RecordId[] = [42], savedSettings: Record<string, unknown> = {}) {
    let onSelection: (ids: readonly RecordId[]) => void = () => {
    };
    const unsubscribe = vi.fn();
    const tags = {
        listDomains: vi.fn(async () => [{tag_domain_id: 1, domain_name: 'Primary'}, {
            tag_domain_id: 2,
            domain_name: 'Other'
        }]),
        activeVerbose: vi.fn<PanelProps['host']['tags']['activeVerbose']>(async () => [tag(1), tag(2, 1, false)]),
        resolve: vi.fn(async () => new Map([[1, 'artist:alice'], [2, 'landscape'], [10, 'mountain'], [3, 'record:second']])),
        autocomplete: vi.fn<PanelProps['host']['tags']['autocomplete']>(async () => []),
        addToRecords: vi.fn(async () => {
        }), removeFromRecords: vi.fn(async () => {
        }),
    };
    const settings = {
        get: () => savedSettings, set: vi.fn((next: Record<string, unknown>) => {
            savedSettings = next;
        })
    };
    const host = {
        tags, settings, selection: {
            get: () => selection, subscribe: (listener: typeof onSelection) => {
                onSelection = listener;
                return unsubscribe;
            }
        }
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = tagEditorPanel.component;
    return {
        tags, settings, view, root, unsubscribe,
        select: (ids: RecordId[]) => onSelection(ids),
        mount: () => act(() => root.render(<Panel host={host}/>)),
        unmount: () => act(() => root.unmount()),
    };
}

function enterDraft(view: HTMLElement, text: string) {
    const input = view.querySelector<HTMLInputElement>('.tag-add input')!;
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, text);
    input.dispatchEvent(new Event('input', {bubbles: true}));
}

it('shows edit scope and provenance, preserves a failed draft, and applies changes across the selection', async () => {
    const f = fixture([41, 42]);
    f.tags.addToRecords.mockRejectedValueOnce(new Error('Permission denied'));
    try {
        await f.mount();
        expect(f.view.textContent).toContain('Editing 2 records · showing #42 · 2 tags');
        expect(f.view.querySelectorAll('.tag-remove')).toHaveLength(1);
        expect(f.view.querySelector('.tag-provenance')!.textContent).toContain('(from: mountain)');
        expect(f.view.querySelector('.tag-provenance')!.closest('li')!.title).toContain('mountain');
        await act(() => enterDraft(f.view, 'new:tag'));
        const input = f.view.querySelector('input')!;
        await act(() => input.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Enter',
            bubbles: true,
            cancelable: true
        })));
        expect(f.view.querySelector<HTMLInputElement>('input')!.value).toBe('new:tag');
        expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('Permission denied');
        expect(f.tags.addToRecords).toHaveBeenLastCalledWith([41, 42], ['new:tag'], 1);
        await act(() => input.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Enter',
            bubbles: true,
            cancelable: true
        })));
        expect(f.view.querySelector<HTMLInputElement>('input')!.value).toBe('');
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
        await act(() => f.view.querySelector<HTMLButtonElement>('.tag-remove')!.click());
        expect(f.tags.removeFromRecords).toHaveBeenCalledWith([41, 42], [1], 1);
    } finally {
        await f.unmount();
    }
    expect(f.unsubscribe).toHaveBeenCalledOnce();
});

it('ignores an obsolete record response and shows every domain without a selector', async () => {
    const f = fixture();
    let finishFirst: (rows: VerboseTag[]) => void = () => {
    };
    f.tags.activeVerbose.mockImplementationOnce(() => new Promise(resolve => {
        finishFirst = resolve;
    }))
        .mockResolvedValue([tag(3)]);
    try {
        await f.mount();
        const firstSignal = f.tags.activeVerbose.mock.calls[0]![1]!;
        await act(() => f.select([43]));
        expect(firstSignal.aborted).toBe(true);
        expect(f.view.querySelector('.tag-list')!.textContent).toContain('record:second');
        await act(() => finishFirst([tag(1)]));
        expect(f.view.querySelector('.tag-list')!.textContent).not.toContain('artist:alice');
        expect(f.view.querySelector('.tag-editor-domain select')).toBeNull();
        const domains = f.view.querySelectorAll('.tag-editor-domain');
        expect(domains).toHaveLength(2);
        expect(domains[1]!.querySelector('.tag-add input')).not.toBeNull();
        expect(domains[1]!.querySelector('.tag-list')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it('retries domain and tag loading failures without presenting either as empty data', async () => {
    const f = fixture();
    f.tags.listDomains.mockRejectedValueOnce(new Error('Domains unavailable'));
    f.tags.activeVerbose.mockRejectedValueOnce(new Error('Tags unavailable'));
    try {
        await f.mount();
        expect(f.view.textContent).toContain('Domains unavailable');
        expect(f.view.textContent).not.toContain('No tag domains.');
        await act(() => f.view.querySelector<HTMLButtonElement>('.tag-editor-error button')!.click());
        expect(f.view.textContent).toContain('Tags unavailable');
        expect(f.view.textContent).not.toContain('No tags in this domain.');
        await act(() => f.view.querySelector<HTMLButtonElement>('.tag-editor-error button')!.click());
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
        expect(f.view.querySelectorAll('.tag-item')).toHaveLength(2);
    } finally {
        await f.unmount();
    }
});

it('ignores repeated submissions while an addition is in flight', async () => {
    const f = fixture();
    let finishAdd: () => void = () => {
    };
    f.tags.addToRecords.mockImplementationOnce(() => new Promise<void>(resolve => {
        finishAdd = resolve;
    }));
    try {
        await f.mount();
        await act(() => enterDraft(f.view, 'new:tag'));
        await act(() => {
            const form = f.view.querySelector('form')!;
            form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
            form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
        });
        expect(f.tags.addToRecords).toHaveBeenCalledOnce();
        expect(f.view.querySelector<HTMLInputElement>('input')!.disabled).toBe(true);
        await act(() => finishAdd());
        expect(f.view.querySelector<HTMLInputElement>('input')!.value).toBe('');
    } finally {
        await f.unmount();
    }
});


it('adds a suggested tag by clicking it and clears the submitted prefix', async () => {
    vi.useFakeTimers();
    const f = fixture();
    f.tags.autocomplete.mockResolvedValue([{tag_id: 1, text: 'artist:alice', count: 12}]);
    try {
        await f.mount();
        await act(() => enterDraft(f.view, 'art'));
        await act(() => vi.advanceTimersByTime(150));
        expect(f.tags.autocomplete).toHaveBeenCalledWith('art', {domain: 1, limit: 100}, expect.any(AbortSignal));
        await act(() => f.view.querySelector<HTMLButtonElement>('.suggestion')!.click());
        expect(f.tags.addToRecords).toHaveBeenCalledWith([42], ['artist:alice'], 1);
        expect(f.view.querySelector<HTMLInputElement>('input')!.value).toBe('');
        expect(f.view.querySelector('.search-suggestions')).toBeNull();
    } finally {
        await f.unmount();
        vi.useRealTimers();
    }
});


it('keeps every domain visible, scopes mutations correctly, and preserves other drafts and errors', async () => {
    const f = fixture([41, 42]);
    f.tags.activeVerbose.mockResolvedValue([tag(1), tag(3, 2)]);
    f.tags.addToRecords.mockRejectedValueOnce(new Error('Other domain denied'));
    try {
        await f.mount();
        expect(f.tags.activeVerbose).toHaveBeenCalledOnce();
        expect(f.tags.resolve).toHaveBeenCalledOnce();
        const primary = f.view.querySelector<HTMLElement>('[aria-label="Primary"]')!;
        const other = f.view.querySelector<HTMLElement>('[aria-label="Other"]')!;
        expect(primary.querySelector('.tag-list')!.textContent).toContain('artist:alice');
        expect(primary.textContent).not.toContain('record:second');
        expect(other.querySelector('.tag-list')!.textContent).toContain('record:second');
        await act(() => {
            enterDraft(primary, 'primary:draft');
            enterDraft(other, 'other:draft');
        });
        const form = other.querySelector('form')!;
        await act(() => form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true})));
        expect(f.tags.addToRecords).toHaveBeenLastCalledWith([41, 42], ['other:draft'], 2);
        expect(other.querySelector('[role="alert"]')!.textContent).toContain('Other domain denied');
        expect(primary.querySelector('[role="alert"]')).toBeNull();
        expect(primary.querySelector<HTMLInputElement>('input')!.value).toBe('primary:draft');
        expect(other.querySelector<HTMLInputElement>('input')!.value).toBe('other:draft');
        await act(() => form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true})));
        expect(other.querySelector('[role="alert"]')).toBeNull();
        expect(other.querySelector<HTMLInputElement>('input')!.value).toBe('');
        expect(primary.querySelector<HTMLInputElement>('input')!.value).toBe('primary:draft');
        await act(() => other.querySelector<HTMLButtonElement>('.tag-remove')!.click());
        expect(f.tags.removeFromRecords).toHaveBeenCalledWith([41, 42], [3], 2);
    } finally {
        await f.unmount();
    }
});

it('allows editing another domain while a submission is pending', async () => {
    const f = fixture();
    let finishAdd: () => void = () => {
    };
    f.tags.addToRecords.mockImplementationOnce(() => new Promise<void>(resolve => {
        finishAdd = resolve;
    }));
    try {
        await f.mount();
        const primary = f.view.querySelector<HTMLElement>('[aria-label="Primary"]')!;
        const other = f.view.querySelector<HTMLElement>('[aria-label="Other"]')!;
        await act(() => enterDraft(primary, 'primary:tag'));
        await act(() => primary.querySelector('form')!.dispatchEvent(new Event('submit', {
            bubbles: true,
            cancelable: true
        })));
        expect(primary.querySelector<HTMLInputElement>('input')!.disabled).toBe(true);
        expect(other.querySelector<HTMLInputElement>('input')!.disabled).toBe(false);
        await act(() => enterDraft(other, 'other:tag'));
        await act(() => other.querySelector('form')!.dispatchEvent(new Event('submit', {
            bubbles: true,
            cancelable: true
        })));
        expect(f.tags.addToRecords).toHaveBeenLastCalledWith([42], ['other:tag'], 2);
        await act(() => finishAdd());
        expect(primary.querySelector<HTMLInputElement>('input')!.value).toBe('');
        expect(other.querySelector<HTMLInputElement>('input')!.value).toBe('');
    } finally {
        await f.unmount();
    }
});

it('requests suggestions in the domain of the input being edited', async () => {
    vi.useFakeTimers();
    const f = fixture();
    f.tags.autocomplete.mockResolvedValue([{tag_id: 3, text: 'record:second', count: 1}]);
    try {
        await f.mount();
        const other = f.view.querySelector<HTMLElement>('[aria-label="Other"]')!;
        await act(() => enterDraft(other, 'rec'));
        await act(() => vi.advanceTimersByTime(150));
        expect(f.tags.autocomplete).toHaveBeenCalledOnce();
        expect(f.tags.autocomplete).toHaveBeenCalledWith('rec', {domain: 2, limit: 100}, expect.any(AbortSignal));
        await act(() => other.querySelector<HTMLButtonElement>('.suggestion')!.click());
        expect(f.tags.addToRecords).toHaveBeenCalledWith([42], ['record:second'], 2);
    } finally {
        await f.unmount();
        vi.useRealTimers();
    }
});


it('nests implicitly added tags beneath their source and keeps only explicit tags removable', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValue([
        tag(1), {...tag(10, 1, false), inherited_from: [1]}, {...tag(2, 1, false), inherited_from: [10]},
    ]);
    f.tags.resolve.mockResolvedValue(new Map([[1, 'character:gawr gura'], [10, 'series:hololive'], [2, 'company:cover']]));
    try {
        await f.mount();
        const domain = f.view.querySelector('[aria-label="Primary"]')!;
        const list = domain.querySelector('.tag-list')!;
        expect(list.querySelectorAll(':scope > .tag-branch')).toHaveLength(1);
        const root = list.querySelector(':scope > .tag-branch')!;
        expect(root.querySelector(':scope > .tag-item .tag-text')!.textContent).toBe('character:gawr gura');
        const child = root.querySelector(':scope > .tag-children > .tag-branch')!;
        expect(child.querySelector(':scope > .tag-item .tag-text')!.textContent).toBe('series:hololive');
        const leaf = child.querySelector(':scope > .tag-children > .tag-branch')!;
        expect(leaf.querySelector('.tag-text')!.textContent).toBe('company:cover');
        expect(domain.querySelectorAll('.tag-item')).toHaveLength(3);
        expect(domain.querySelectorAll('.tag-remove')).toHaveLength(1);
        expect(root.querySelector(':scope > .tag-item .tag-remove')).not.toBeNull();
        expect(leaf.querySelector('.tag-remove')).toBeNull();
        expect(domain.querySelector('details')).toBeNull();
        expect(domain.querySelector('.tag-badge')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it('shows multiple crossed-out originals to the right of their effective alias and associates inherited origins', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValue([
        {...tag(3, 1, false), inherited_from: [], aliased_from: [1, 10, 1]},
        {...tag(2, 1, false), inherited_from: [1]},
    ]);
    try {
        await f.mount();
        const domain = f.view.querySelector('[aria-label="Primary"]')!;
        const child = domain.querySelector('.tag-list > .tag-branch')!;
        expect(child.querySelector('.tag-text')!.textContent).toBe('record:second artist:alice× mountain×');
        expect(child.querySelector('.tag-children .tag-text')!.textContent).toBe('landscape');
        expect([...child.querySelectorAll('.tag-alias-origins s')].map(original => original.textContent!.trim()))
            .toEqual(['artist:alice', 'mountain']);
        expect(child.querySelector('.tag-text')!.firstChild!.textContent).toBe('record:second');
        expect(domain.querySelectorAll('.tag-item')).toHaveLength(2);
        expect(domain.querySelector('.tag-remove')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it('lists a shared implied tag once and names its additional source', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValue([
        tag(1), {...tag(2, 1, false), inherited_from: [1, 10, 1]}, tag(10),
    ]);
    try {
        await f.mount();
        const domain = f.view.querySelector('[aria-label="Primary"]')!;
        expect(domain.querySelectorAll('.tag-item')).toHaveLength(3);
        const child = domain.querySelector('.tag-children .tag-branch')!;
        expect(child.querySelector('.tag-text')!.textContent).toBe('landscape (also from: mountain)');
        expect(domain.querySelectorAll('.tag-remove')).toHaveLength(2);
    } finally {
        await f.unmount();
    }
});

it('keeps all tags visible for cyclic provenance and does not link different domains', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValue([
        {...tag(1), inherited_from: [2]}, {...tag(2, 1, false), inherited_from: [1]},
        {...tag(3, 2, false), inherited_from: [1]},
    ]);
    try {
        await f.mount();
        const primary = f.view.querySelector('[aria-label="Primary"]')!;
        const other = f.view.querySelector('[aria-label="Other"]')!;
        expect(primary.querySelectorAll('.tag-item')).toHaveLength(2);
        expect(other.querySelectorAll('.tag-item')).toHaveLength(1);
        expect(other.querySelector('.tag-children')).toBeNull();
        expect(other.textContent).toContain('(from: artist:alice)');
    } finally {
        await f.unmount();
    }
});


function changeSort(view: HTMLElement, label: string, value: string) {
    const select = view.querySelector<HTMLSelectElement>(`[aria-label="${label}"]`)!;
    select.value = value;
    select.dispatchEvent(new Event('change', {bubbles: true}));
}

function domainOrder(view: HTMLElement): string[] {
    return [...view.querySelectorAll('.tag-editor-domain')].map(domain => domain.getAttribute('aria-label')!);
}

it('defaults to most tags first, keeps empty domains visible, and uses names to break ties', async () => {
    const f = fixture();
    f.tags.listDomains.mockResolvedValue([
        {tag_domain_id: 1, domain_name: 'Primary'}, {tag_domain_id: 2, domain_name: 'Other'},
        {tag_domain_id: 3, domain_name: 'Empty'},
    ]);
    f.tags.activeVerbose.mockResolvedValue([tag(1), tag(2, 2), tag(3, 2)]);
    try {
        await f.mount();
        expect(f.view.querySelector<HTMLSelectElement>('[aria-label="Sort domains"]')!.value).toBe('tag-count');
        expect(domainOrder(f.view)).toEqual(['Other', 'Primary', 'Empty']);
        await act(() => changeSort(f.view, 'Sort domains', 'name-asc'));
        expect(domainOrder(f.view)).toEqual(['Empty', 'Other', 'Primary']);
        await act(() => changeSort(f.view, 'Sort domains', 'name-desc'));
        expect(domainOrder(f.view)).toEqual(['Primary', 'Other', 'Empty']);
        expect(f.tags.activeVerbose).toHaveBeenCalledOnce();
        expect(f.settings.set).toHaveBeenLastCalledWith({domainSort: 'name-desc', tagSort: 'name-asc'});
    } finally {
        await f.unmount();
    }
});

it('sorts tag siblings without changing their parent or losing alias annotations', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValue([
        {...tag(1, 1, false), inherited_from: [10]}, {...tag(3, 1, false), inherited_from: [10], aliased_from: [2]},
        tag(10), tag(2),
    ]);
    try {
        await f.mount();
        const list = f.view.querySelector('[aria-label="Implied by mountain"]')!;
        const texts = () => [...list.querySelectorAll(':scope > .tag-branch > .tag-item > .tag-text')].map(row => row.textContent);
        expect(texts()).toEqual(['artist:alice', 'record:second landscape×']);
        await act(() => changeSort(f.view, 'Sort tags', 'name-desc'));
        expect(texts()).toEqual(['record:second landscape×', 'artist:alice']);
        expect(list.closest('.tag-branch')!.querySelector(':scope > .tag-item .tag-text')!.textContent).toBe('mountain');
        expect(f.view.querySelectorAll('.tag-item')).toHaveLength(4);
        expect(f.tags.activeVerbose).toHaveBeenCalledOnce();
        expect(f.settings.set).toHaveBeenLastCalledWith({domainSort: 'tag-count', tagSort: 'name-desc'});
    } finally {
        await f.unmount();
    }
});

it('reorders domains after an edit while retaining drafts and nodes in other domains', async () => {
    const f = fixture();
    f.tags.activeVerbose.mockResolvedValueOnce([tag(1), tag(3, 2)])
        .mockResolvedValue([tag(1), tag(2), tag(3, 2)]);
    try {
        await f.mount();
        expect(domainOrder(f.view)).toEqual(['Other', 'Primary']);
        const other = f.view.querySelector<HTMLElement>('[aria-label="Other"]')!;
        const primary = f.view.querySelector<HTMLElement>('[aria-label="Primary"]')!;
        await act(() => {
            enterDraft(other, 'unfinished:draft');
            enterDraft(primary, 'landscape');
        });
        await act(() => primary.querySelector('input')!.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Enter', bubbles: true, cancelable: true,
        })));
        expect(domainOrder(f.view)).toEqual(['Primary', 'Other']);
        expect(f.view.querySelector('[aria-label="Other"]')).toBe(other);
        expect(other.querySelector<HTMLInputElement>('input')!.value).toBe('unfinished:draft');
        expect(f.tags.addToRecords).toHaveBeenCalledWith([42], ['landscape'], 1);
    } finally {
        await f.unmount();
    }
});

it('restores saved sort options and falls back safely for invalid saved settings', async () => {
    const restored = fixture([42], {domainSort: 'name-desc', tagSort: 'name-desc'});
    try {
        await restored.mount();
        expect(restored.view.querySelector<HTMLSelectElement>('[aria-label="Sort domains"]')!.value).toBe('name-desc');
        expect(restored.view.querySelector<HTMLSelectElement>('[aria-label="Sort tags"]')!.value).toBe('name-desc');
    } finally {
        await restored.unmount();
    }
    const fallback = fixture([42], {domainSort: 'invalid', tagSort: 'invalid'});
    try {
        await fallback.mount();
        expect(fallback.view.querySelector<HTMLSelectElement>('[aria-label="Sort domains"]')!.value).toBe('tag-count');
        expect(fallback.view.querySelector<HTMLSelectElement>('[aria-label="Sort tags"]')!.value).toBe('name-asc');
    } finally {
        await fallback.unmount();
    }
});

it('removes each original alias id independently and retains errors until retry succeeds', async () => {
    const f = fixture([41, 42]);
    const canonical = {...tag(3, 1, false), inherited_from: [], aliased_from: [1, 10, 1]};
    const inherited = {...tag(2, 1, false), inherited_from: [3]};
    f.tags.resolve.mockResolvedValue(new Map([
        [1, 'gawr gura'], [10, 'gura'], [3, 'character:gawr gura'], [2, 'series:hololive'],
    ]));
    f.tags.activeVerbose.mockResolvedValue([canonical, inherited]);
    f.tags.removeFromRecords.mockRejectedValueOnce(new Error('Permission denied'))
        .mockImplementationOnce(async () => {
            f.tags.activeVerbose.mockResolvedValue([{...canonical, aliased_from: [10]}, inherited]);
        }).mockImplementationOnce(async () => {
        f.tags.activeVerbose.mockResolvedValue([]);
    });
    try {
        await f.mount();
        expect(f.view.querySelectorAll('.tag-alias-remove')).toHaveLength(2);
        expect(f.view.querySelector('.tag-remove')).toBeNull();
        const removeGawr = f.view.querySelector<HTMLButtonElement>('[aria-label="Remove gawr gura from 2 records"]')!;
        await act(() => removeGawr.click());
        expect(f.tags.removeFromRecords).toHaveBeenLastCalledWith([41, 42], [1], 1);
        expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('Permission denied');
        expect(f.view.querySelectorAll('.tag-alias-remove')).toHaveLength(2);
        await act(() => removeGawr.click());
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
        expect(f.view.querySelectorAll('.tag-alias-remove')).toHaveLength(1);
        expect(f.view.querySelector('.tag-text')!.firstChild!.textContent).toBe('character:gawr gura');
        expect(f.view.querySelector('[aria-label="Implied by character:gawr gura"]')!.textContent).toContain('series:hololive');
        await act(() => f.view.querySelector<HTMLButtonElement>('[aria-label="Remove gura from 2 records"]')!.click());
        expect(f.tags.removeFromRecords).toHaveBeenLastCalledWith([41, 42], [10], 1);
        expect(f.view.querySelector('.tag-list')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it('removes a directly applied effective tag without removing its alias origins', async () => {
    const f = fixture();
    const canonical = {...tag(3), aliased_from: [1]};
    f.tags.activeVerbose.mockResolvedValue([canonical]);
    f.tags.removeFromRecords.mockImplementationOnce(async () => {
        f.tags.activeVerbose.mockResolvedValue([{...canonical, explicit: false}]);
    });
    try {
        await f.mount();
        expect(f.view.querySelector('.tag-remove')).not.toBeNull();
        expect(f.view.querySelector('.tag-alias-remove')).not.toBeNull();
        await act(() => f.view.querySelector<HTMLButtonElement>('.tag-remove')!.click());
        expect(f.tags.removeFromRecords).toHaveBeenCalledWith([42], [3], 1);
        expect(f.view.querySelector('.tag-remove')).toBeNull();
        expect(f.view.querySelector('.tag-alias-remove')).not.toBeNull();
        expect(f.view.querySelector('.tag-text')!.firstChild!.textContent).toBe('record:second');
    } finally {
        await f.unmount();
    }
});
