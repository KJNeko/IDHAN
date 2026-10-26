import {afterEach, expect, it, vi} from 'vitest';
import {api} from './client';

afterEach(() => vi.unstubAllGlobals());

it('sends selected metadata rescan IDs using the server records field', async () => {
    const fetch = vi.fn(async () => Response.json({job_id: 9}));
    vi.stubGlobal('fetch', fetch);
    await api.rescanMetadata([12, 34]);
    expect(fetch).toHaveBeenCalledWith('/jobs/metadata/rescan', expect.objectContaining({
        method: 'POST', body: JSON.stringify({records: [12, 34]}),
    }));
});
