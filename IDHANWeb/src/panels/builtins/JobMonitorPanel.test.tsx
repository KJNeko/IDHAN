import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import {jobMonitorPanel} from './JobMonitorPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('labels continuous maintenance separately from a finite scan', async () => {
    const host = {
        http: {
            fetch: vi.fn().mockResolvedValue({
                ok: true, json: async () => ({
                    jobs: [
                        {
                            job_id: 1, job_name: 'Hamming distance maintenance', status: 'running', continuous: true,
                            progress: {
                                kind: 'background_service', service: 'hamming_distance', phase: 'waiting',
                                message: 'Waiting for the next Hamming distance sweep', processed_units: 3,
                                failed_count: 0, updated_at: 1700000000
                            }
                        },
                        {job_id: 2, job_name: 'Cluster scan', status: 'running', continuous: false},
                    ]
                })
            })
        }
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = jobMonitorPanel.component;
    try {
        await act(async () => {
            root.render(<Panel host={host}/>);
        });
        const rows = view.querySelectorAll('.job-item');
        expect(rows).toHaveLength(2);
        expect(rows[0].querySelector('.job-continuous')?.textContent).toBe('Continuous');
        expect(rows[0].textContent).toContain('Waiting for the next Hamming distance sweep');
        expect(rows[1].querySelector('.job-continuous')).toBeNull();
    } finally {
        await act(() => root.unmount());
    }
});
