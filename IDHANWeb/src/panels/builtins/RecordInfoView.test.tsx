import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it} from 'vitest';
import {RecordInfoView} from './RecordInfoView';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('shows all record fields together without a disclosure', async () => {
    const view = document.createElement('div');
    const root = createRoot(view);
    const hash = 'a'.repeat(64);
    try {
        await act(() => root.render(<RecordInfoView info={{
            record_id: 42, mime: 'image/jpeg', simple_type: 'image', extension: '.jpg',
            size: 2048, width: 1920, height: 1080, hashes: {sha256: hash},
            lossless: false, quality: 85, chroma_subsampling: '4:2:0', progressive: true,
            tags: [{domain: 'gallery', text: 'creator:someone'}, {domain: 'gallery', text: 'landscape'}],
            urls: ['https://example.test/gallery/42?full=true'],
            file_mtime: 1_000_000, extra: {encoder: 'Example encoder'}, parsed: false,
        }}/>));
        const overview = view.querySelector('.record-info-overview')!;
        expect(overview.textContent).toContain('#42');
        expect(overview.textContent).toContain('2 KiB');
        expect(overview.textContent).toContain('1,920 × 1,080');
        expect(overview.textContent).not.toContain(hash);
        expect(view.querySelector('details')).toBeNull();
        const details = view.querySelector('.info-list')!;
        expect(details.textContent).toContain(hash);
        expect(details.textContent).toContain('Example encoder');
        expect(details.textContent).toContain('.jpg');
        expect(details.textContent).not.toContain('..jpg');
        expect(view.textContent).toContain('Lossy · q85 · 4:2:0 · Progressive');
        expect(view.textContent).toContain('Tags (gallery)');
        expect(view.querySelectorAll('.info-tags li')).toHaveLength(2);
        const source = view.querySelector<HTMLAnchorElement>('.info-source')!;
        expect(source.textContent!.trim()).toBe('example.test/gallery/42?full=true');
        expect(source.href).toBe('https://example.test/gallery/42?full=true');
        expect(source.title).toBe(source.href);
        expect(view.querySelector('[role="status"]')!.textContent).toBe('Metadata not yet parsed');
    } finally {
        await act(() => root.unmount());
    }
});

it('does not invent missing values or make non-web source URLs actionable', async () => {
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<RecordInfoView info={{
            record_id: 7,
            size: Number.NaN, duration: Number.POSITIVE_INFINITY,
            urls: ['javascript:alert(1)', 'not a URL'],
        }}/>));
        expect(view.textContent).not.toMatch(/NaN|Infinity|undefined/);
        expect(view.querySelector('a')).toBeNull();
        expect(view.textContent).toContain('not a URL');
        expect(view.querySelector('details')).toBeNull();
    } finally {
        await act(() => root.unmount());
    }
});
