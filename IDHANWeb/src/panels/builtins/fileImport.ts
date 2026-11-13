import {ApiError, readApiError} from '../../api/client';
import type {HostApi, RecordId} from '../../host/types';
import {formatBytes} from './RecordInfoView';
import {describeImportError} from './importFiles';

export interface FileImportResult {
    recordId: RecordId;
    outcome: 'imported' | 'exists' | 'deleted';
}

export async function uploadImportFile(host: HostApi, file: File, force: boolean, signal?: AbortSignal): Promise<FileImportResult> {
    const params = new URLSearchParams({filename: file.name});
    if (force) params.set('force_import', 'true');
    const context = `"${file.name}" (${formatBytes(file.size)}), POST /file/import`;
    let responseStatus: number | undefined;
    let data: unknown;
    try {
        const response = await host.http.fetch(`/file/import?${params}`, {
            method: 'POST', headers: {'Content-Type': file.type || 'application/octet-stream'}, body: file, signal,
        });
        responseStatus = response.status;
        if (!response.ok) throw await readApiError(response, 'POST /file/import');
        data = await response.json();
    } catch (error) {
        if (error instanceof ApiError) throw new Error(`Upload failed for ${context}, HTTP ${error.status}: ${error.message}`);
        if (responseStatus !== undefined) throw new Error(`Could not read the import response for ${context}, HTTP ${responseStatus}: ${describeImportError(error)}`);
        throw new Error(`Could not upload ${context}. No HTTP response was received. The browser may be unable to read the local file or reach the server. Check file permissions and server/network connectivity. Browser error: ${describeImportError(error)}`);
    }
    if (data === null || typeof data !== 'object') throw new Error(`Invalid import result for ${context}, HTTP ${responseStatus}: Expected a JSON object.`);
    const result = data as { status?: unknown; record_id?: unknown; reason?: unknown; error?: unknown };
    const reason = typeof result.reason === 'string' ? result.reason : typeof result.error === 'string' ? result.error : null;
    if (result.status === 4) throw new Error(`Import rejected for ${context}, HTTP ${responseStatus}: ${reason ?? 'The server did not report a reason.'}`);
    if (typeof result.record_id !== 'number' || !Number.isInteger(result.record_id) || result.record_id <= 0) {
        throw new Error(`Invalid import result for ${context}, HTTP ${responseStatus}: ${reason ?? 'The server did not return a valid record ID.'}`);
    }
    if (result.status !== 1 && result.status !== 2 && result.status !== 3) {
        throw new Error(`Invalid import result for ${context}, HTTP ${responseStatus}: Unknown import status ${String(result.status)}.`);
    }
    return {
        recordId: result.record_id,
        outcome: result.status === 2 ? 'exists' : result.status === 3 ? 'deleted' : 'imported'
    };
}

/** Bound uploads and tagging requests to avoid opening a connection per file. */
export async function runImportPool<T>(items: readonly T[], worker: (item: T) => Promise<void>, limit = 3): Promise<void> {
    let next = 0;
    const run = async () => {
        while (next < items.length) await worker(items[next++]!);
    };
    await Promise.all(Array.from({length: Math.min(limit, items.length)}, run));
}
