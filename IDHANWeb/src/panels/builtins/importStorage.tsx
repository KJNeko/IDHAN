import {useCallback, useEffect, useRef, useState} from 'react';
import {readApiError} from '../../api/client';
import type {HostApi} from '../../host/types';

interface StorageState {
    canImport: boolean;
    message: string | null;
}

/** Check again before submission: a cluster may have changed since the panel opened. */
export function useImportStorage(host: HostApi) {
    const [state, setState] = useState<StorageState>({canImport: false, message: 'Checking writable storage…'});
    const latestRequest = useRef(0);

    const check = useCallback(async (signal?: AbortSignal): Promise<boolean> => {
        const request = ++latestRequest.current;
        try {
            const response = await host.http.fetch('/clusters/list', {signal});
            if (!response.ok) throw await readApiError(response, 'GET /clusters/list');
            const clusters = await response.json() as { readonly: boolean }[];
            const canImport = clusters.some((cluster) => cluster.readonly === false);
            if (signal?.aborted || request !== latestRequest.current) return false;
            const message = canImport ? null : clusters.length === 0
                ? 'No storage clusters are configured. Add a writable cluster in Cluster Manager to import files.'
                : 'All storage clusters are read-only. Mark at least one cluster as writable in Cluster Manager to import files.';
            setState({canImport, message});
            return canImport;
        } catch (error) {
            if (!signal?.aborted && request === latestRequest.current) {
                const reason = error instanceof Error ? error.message : String(error);
                setState({canImport: false, message: `Cannot check writable storage: ${reason}`});
            }
            return false;
        }
    }, [host]);

    useEffect(() => {
        const controller = new AbortController();
        const refresh = () => {
            void check(controller.signal);
        };
        refresh();
        window.addEventListener('focus', refresh);
        return () => {
            controller.abort();
            latestRequest.current++;
            window.removeEventListener('focus', refresh);
        };
    }, [check]);

    return {...state, check};
}

export function ImportStorageNotice({storage}: { storage: ReturnType<typeof useImportStorage> }) {
    if (storage.message === null) return null;
    return (
        <p className="muted" role="status">
            {storage.message}{' '}
            <button type="button" className="toolbar-button" onClick={() => void storage.check()}>Refresh storage
            </button>
        </p>
    );
}
