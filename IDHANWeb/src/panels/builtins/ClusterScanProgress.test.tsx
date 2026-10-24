import {renderToStaticMarkup} from 'react-dom/server';
import {expect, it} from 'vitest';
import {ClusterScanProgress, type ClusterScanProgressData} from './ClusterScanProgress';

const progress: ClusterScanProgressData = {
    kind: 'cluster_scan', phase: 'scanning_files', checked_records: 0, counted_files: 12,
    processed_files: 4, processed_bytes: 1024, failed_count: 1, active_files: 1,
    total_files: 12, remaining_files: 8,
    tasks: [{task_id: 5, path: '/cluster/ab/abc.jpg', stage: 'parsing_metadata'}],
};

function render(data: ClusterScanProgressData): HTMLDivElement {
    const container = document.createElement('div');
    container.innerHTML = renderToStaticMarkup(<ClusterScanProgress progress={data}/>);
    return container;
}

it('shows remaining work, failures and the file stage as a nested task', () => {
    const view = render(progress);
    expect(view.textContent).toContain('4 / ≈12 files processed · 8 remaining · 1 failure');
    expect(view.querySelector('progress')?.value).toBe(4);
    expect(view.querySelector('progress')?.max).toBe(12);
    expect(view.querySelector('details')?.textContent).toContain('Parsing metadata');
    expect(view.querySelector('details')?.textContent).toContain('/cluster/ab/abc.jpg');
});

it('distinguishes storing metadata from parsing it', () => {
    const view = render({
        ...progress,
        tasks: [{...progress.tasks[0], stage: 'storing_metadata'}],
    });
    expect(view.querySelector('details')?.textContent).toContain('Storing metadata');
    expect(view.querySelector('details')?.textContent).not.toContain('Parsing metadata');
});

it('keeps early-stop remaining work visible and handles unknown and empty totals', () => {
    const stopped = render({...progress, phase: 'stopped', active_files: 0, tasks: []});
    expect(stopped.textContent).toContain('Stopped early');
    expect(stopped.textContent).toContain('8 remaining');
    expect(stopped.querySelector('details')).toBeNull();
    const unknown = render({...progress, total_files: undefined, remaining_files: undefined});
    expect(unknown.querySelector('progress')?.hasAttribute('value')).toBe(false);
    const empty = render({...progress, phase: 'completed', processed_files: 0, total_files: 0, remaining_files: 0});
    expect(empty.querySelector('progress')?.position).toBe(1);
    expect(empty.textContent).toContain('0 / 0 files processed · 0 remaining');
});

it('reports preflight and counting work without inventing a total', () => {
    expect(render({...progress, phase: 'checking_missing_files', checked_records: 128}).textContent)
        .toContain('128 assigned records checked');
    expect(render({...progress, phase: 'counting_files', counted_files: 150}).textContent)
        .toContain('150 files counted');
});
