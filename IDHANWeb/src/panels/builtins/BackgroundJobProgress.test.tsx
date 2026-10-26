import {renderToStaticMarkup} from 'react-dom/server';
import {expect, it} from 'vitest';
import {BackgroundJobProgress, type BackgroundJobProgressData} from './BackgroundJobProgress';

const progress: BackgroundJobProgressData = {
    kind: 'background_service', service: 'phash_backfill', phase: 'waiting',
    message: 'Waiting for the next sweep', processed_units: 12,
    failed_count: 1, last_error: 'Cannot open cluster', updated_at: 1700000000,
};

it('shows a waiting service as alive with its counters and latest failure', () => {
    const view = document.createElement('div');
    view.innerHTML = renderToStaticMarkup(<BackgroundJobProgress progress={progress}/>);
    expect(view.textContent).toContain('Waiting: Waiting for the next sweep');
    expect(view.textContent).toContain('12 records attempted · 1 failure');
    expect(view.textContent).not.toContain('cycles');
    expect(view.textContent).toContain('Latest failure: Cannot open cluster');
    expect(view.querySelector('time')?.dateTime).toBe('2023-11-14T22:13:20.000Z');
    expect(view.querySelector('progress')).toBeNull();
});

it('shows module maintenance activity without a completed-cycle counter', () => {
    const html = renderToStaticMarkup(<BackgroundJobProgress progress={{
        ...progress, service: 'module_workers',
        message: 'Waiting for the next module worker maintenance pass', last_error: undefined
    }}/>);
    expect(html).toContain('Waiting for the next module worker maintenance pass');
    expect(html).not.toContain('cycles');
    expect(html).not.toContain('records attempted');
    expect(html).not.toContain('Latest failure');
});
