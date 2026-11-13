export interface ImportFile {
    file: File;
    path: string;
}

export interface ImportSelection {
    sources: readonly (File | FileSystemEntry)[];
    errors?: readonly string[];
}

export interface CollectedImportFiles {
    files: ImportFile[];
    errors: string[];
}

export function describeImportError(error: unknown): string {
    if (!(error instanceof Error)) return String(error);
    return error.name === 'Error' ? error.message : `${error.name}: ${error.message}`;
}

/** Capture entries during the drop event, before the browser protects its drag data store. */
export function captureDroppedFiles(data: DataTransfer): ImportSelection {
    const items = Array.from(data.items ?? []).filter(item => item.kind === 'file');
    if (items.length === 0) return {sources: Array.from(data.files)};
    const sources: (File | FileSystemEntry)[] = [];
    const errors: string[] = [];
    for (const item of items) {
        try {
            const entry = item.webkitGetAsEntry?.();
            const source = entry ?? item.getAsFile();
            if (source) sources.push(source);
            else errors.push('The browser could not access a dropped file or folder. Try Choose folder or select the files again.');
        } catch (error) {
            errors.push(`Could not access a dropped file or folder: ${describeImportError(error)}`);
        }
    }
    return {sources, errors};
}

/** Continue reading batches until exhaustion; browsers may return only 100 entries per batch. */
async function collectEntry(entry: FileSystemEntry, parent: string, result: CollectedImportFiles): Promise<void> {
    const path = parent ? `${parent}/${entry.name}` : entry.name;
    try {
        if (entry.isFile) {
            const file = await new Promise<File>((resolve, reject) => (entry as FileSystemFileEntry).file(resolve, reject));
            result.files.push({file, path});
            return;
        }
        if (!entry.isDirectory) throw new Error('The browser did not identify this entry as a file or folder.');
        const reader = (entry as FileSystemDirectoryEntry).createReader();
        while (true) {
            const batch = await new Promise<FileSystemEntry[]>((resolve, reject) => reader.readEntries(resolve, reject));
            if (batch.length === 0) return;
            for (const child of batch) await collectEntry(child, path, result);
        }
    } catch (error) {
        result.errors.push(`Could not read "${path}": ${describeImportError(error)}. Check that it still exists and your browser has permission to read it.`);
    }
}

export async function collectImportFiles(selection: ImportSelection): Promise<CollectedImportFiles> {
    const result: CollectedImportFiles = {files: [], errors: [...(selection.errors ?? [])]};
    for (const source of selection.sources) {
        if ('isDirectory' in source) await collectEntry(source, '', result);
        else result.files.push({file: source, path: source.webkitRelativePath || source.name});
    }
    return result;
}
