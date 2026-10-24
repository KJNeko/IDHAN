import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import type {JobStatus} from '../../api/types';
import {watchJobs} from '../../api/client';
import {jobMonitorPanel} from './JobMonitorPanel';

vi.mock('../../api/client', () => ({watchJobs: vi.fn()}));
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
    } finally {
        await act(() => root.unmount());
    }
    expect(close).toHaveBeenCalledOnce();
});
