import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import type {JobStatus} from '../../api/types';
import {api, watchJobs} from '../../api/client';
import {jobMonitorPanel} from './JobMonitorPanel';

vi.mock('../../api/client', () => ({watchJobs: vi.fn(), api: {reorganizeUrls: vi.fn()}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('renders streamed maintenance and scan progress, releases its subscription, and never polls', async () => {
    const close = vi.fn();
    let onJobs: (jobs: JobStatus[]) => void = () => {
    };
    vi.mocked(watchJobs).mockImplementation(handlers => {
        onJobs = handlers.onJobs;
        return close;
    });
    const fetch = vi.fn();
    const host = {http: {fetch}} as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = jobMonitorPanel.component;
    try {
        await act(() => root.render(<Panel host={host}/>));
        await act(() => onJobs([
            {
                job_id: 1,
                job_name: 'Hamming distance maintenance',
                status: 'running',
                continuous: true,
                completed: false,
                progress: {
                    kind: 'background_service', service: 'hamming_distance', phase: 'waiting',
                    message: 'Queue is empty; next Hamming distance sweep in 300 seconds', processed_units: 3,
                    failed_count: 0, updated_at: 1700000000,
                },
            },
            {job_id: 2, job_name: 'Cluster scan', status: 'running', continuous: false, completed: false},
        ]));
        const rows = view.querySelectorAll('.job-item');
        expect(rows).toHaveLength(2);
        expect(rows[0].querySelector('.job-continuous')?.textContent).toBe('Continuous');
        expect(rows[0].textContent).toContain('next Hamming distance sweep in 300 seconds');
        expect(rows[1].querySelector('.job-continuous')).toBeNull();
        await act(() => onJobs([{job_id: 2, job_name: 'Cluster scan', status: 'completed', completed: true}]));
        expect(view.querySelectorAll('.job-item')).toHaveLength(1);
        expect(view.textContent).toContain('completed');
        expect(fetch).not.toHaveBeenCalled();
        vi.mocked(api.reorganizeUrls).mockResolvedValue({job_id: 3});
        await act(() => view.querySelector('.job-launcher')!.dispatchEvent(new Event('submit', {
            bubbles: true,
            cancelable: true
        })));
        expect(api.reorganizeUrls).toHaveBeenCalledOnce();
        await act(() => onJobs([{
            job_id: 3, job_name: 'Reorganizing URLs', status: 'running', completed: false,
            progress: {
                kind: 'url_reorganization', phase: 'waiting', message: 'Waiting for exclusive access to URL writes',
                processed_count: 0, sorted_count: 0, merged_count: 0,
            },
        }]));
        expect(view.textContent).toContain('Waiting for exclusive access to URL writes');
        await act(() => onJobs([{
            job_id: 3, job_name: 'Reorganizing URLs', status: 'running', completed: false,
            progress: {
                kind: 'url_reorganization', phase: 'sorting', message: 'Checking URLs against URL class sorting rules',
                processed_count: 4, total_count: 8, sorted_count: 2, merged_count: 0,
            },
        }]));
        expect(view.textContent).toContain('Checked: 4 / 8');
        expect(view.querySelector('progress')?.value).toBe(4);
        await act(() => onJobs([{
            job_id: 3, job_name: 'Reorganizing URLs', status: 'running', completed: false,
            progress: {
                kind: 'url_reorganization', phase: 'merging', message: 'Combining record mappings',
                processed_count: 8, total_count: 8, sorted_count: 4, unique_count: 4,
                merge_candidate_count: 4, merged_count: 1, committed_batch_count: 2, mappings_processed_count: 1500,
            },
        }]));
        expect(view.textContent).toContain('Combining record mappings');
        expect(view.textContent).toContain('Merged: 1 · Planned merges: 4');
        expect(view.textContent).toContain('Committed batches: 2 · Mappings processed: 1,500');
        await act(() => onJobs([{
            job_id: 3, job_name: 'Reorganizing URLs', status: 'completed', completed: true,
            response: {kind: 'url_reorganization', total_count: 8, unique_count: 4, merged_count: 4, sorted_count: 4},
        }]));
        expect(view.textContent).toContain('Total: 8 · Unique: 4 · Merged: 4 · Sorted: 4');
        await act(() => onJobs([{
            job_id: 3, job_name: 'Reorganizing URLs', status: 'completed', completed: true,
            progress: {
                kind: 'url_reorganization', phase: 'completed', message: 'URL reorganization complete',
                processed_count: 8, total_count: 8, sorted_count: 4, unique_count: 4,
                merge_candidate_count: 4, merged_count: 4,
            },
            response: {kind: 'url_reorganization', total_count: 8, unique_count: 4, merged_count: 4, sorted_count: 4},
        }]));
        expect(view.textContent).toContain('URL reorganization complete');
        expect(view.textContent).toContain('Total: 8 · Unique: 4 · Merged: 4');
        expect(view.textContent).not.toContain('Planned merges');
        await act(() => onJobs([
            {job_id: 4, status: 'completed', completed: true, response: {scanned_count: 20, failed_count: 2}},
            {
                job_id: 5,
                status: 'completed',
                completed: true,
                response: {model_name: 'images', embedded: 10, failed: 1, skipped: 3}
            },
        ]));
        expect(view.textContent).toContain('Scanned: 20 · Failed: 2');
        expect(view.textContent).toContain('Embedded: 10 · Failed: 1 · Skipped: 3');
        await act(() => onJobs([
            {job_id: 6, status: 'completed', completed: true, response: 'Finished'},
            {
                job_id: 7,
                status: 'completed',
                completed: true,
                response: {model_name: 'offline', error: 'Model unavailable'}
            },
        ]));
        expect(view.textContent).toContain('Model unavailable');
    } finally {
        await act(() => root.unmount());
    }
    expect(close).toHaveBeenCalledOnce();
});
