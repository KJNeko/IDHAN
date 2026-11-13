import {expect, it, vi} from 'vitest';
import {captureDroppedFiles, collectImportFiles} from './importFiles';
import {planFolderTags} from './folderImportPlan';

export function fileEntry(name: string, contents = 'bytes'): FileSystemFileEntry {
    return {
        name, isFile: true, isDirectory: false,
        file: (success: (file: File) => void) => success(new File([contents], name)),
    } as FileSystemFileEntry;
}

export function directoryEntry(name: string, batches: FileSystemEntry[][]): FileSystemDirectoryEntry {
    return {
        name, isFile: false, isDirectory: true, createReader: () => {
            let index = 0;
            return {readEntries: (success: (entries: FileSystemEntry[]) => void) => success(batches[index++] ?? [])};
        }
    } as FileSystemDirectoryEntry;
}

it('reads every directory batch and nested folder without treating directories as file bodies', async () => {
    const first = Array.from({length: 100}, (_, index) => fileEntry(`image-${index}.png`));
    const nested = directoryEntry('character', [[fileEntry('last.png')]]);
    const root = directoryEntry('artist', [first, [nested]]);
    const captured = captureDroppedFiles({
        items: [{kind: 'file', webkitGetAsEntry: () => root}],
        files: [new File([], 'artist')]
    } as unknown as DataTransfer);
    const result = await collectImportFiles(captured);
    expect(result.errors).toEqual([]);
    expect(result.files).toHaveLength(101);
    expect(result.files.at(-1)!.path).toBe('artist/character/last.png');
    expect(result.files.at(-1)!.file.name).toBe('last.png');
});

it('captures drag entries synchronously and reports unreadable paths while preserving readable siblings', async () => {
    const denied = {
        name: 'private.jpg', isFile: true, isDirectory: false,
        file: (_: unknown, error: (reason: DOMException) => void) => error(new DOMException('Permission denied', 'NotReadableError')),
    } as unknown as FileSystemFileEntry;
    const getter = vi.fn(() => directoryEntry('folder', [[denied, fileEntry('ok.jpg')]]));
    const selection = captureDroppedFiles({
        items: [{kind: 'file', webkitGetAsEntry: getter}],
        files: []
    } as unknown as DataTransfer);
    expect(getter).toHaveBeenCalledOnce();
    getter.mockImplementation(() => {
        throw new Error('Drag event expired');
    });
    const result = await collectImportFiles(selection);
    expect(result.files.map(source => source.file.name)).toEqual(['ok.jpg']);
    expect(result.errors[0]).toContain('folder/private.jpg');
    expect(result.errors[0]).toContain('NotReadableError: Permission denied');
});

it('keeps genuine empty files and supports ordinary file drops without directory APIs', async () => {
    const file = new File([], 'empty.txt');
    const result = await collectImportFiles(captureDroppedFiles({files: [file]} as unknown as DataTransfer));
    expect(result.files).toEqual([{file, path: 'empty.txt'}]);
});

it('maps only folder segments to tags and supports skipping the selected root folder', () => {
    const source = {file: new File(['x'], 'whatever.jpg'), path: 'artist-name/character-name/whatever.jpg'};
    expect(planFolderTags(source, ['artist', 'character'], true)).toEqual({
        tags: ['artist:artist-name', 'character:character-name'], errors: [],
    });
    expect(planFolderTags({...source, path: `collection/${source.path}`}, ['artist', 'character'], false)).toEqual({
        tags: ['artist:artist-name', 'character:character-name'], errors: [],
    });
    expect(planFolderTags(source, ['', 'character'], true).tags).toEqual(['character:character-name']);
});

it('reports missing mapped levels instead of silently assigning incorrect tags', () => {
    const source = {file: new File(['x'], 'image.jpg'), path: 'artist/image.jpg'};
    expect(planFolderTags(source, ['artist', 'character'], true).errors[0]).toContain('Missing folder level 2');
    expect(planFolderTags(source, ['artist:'], true).errors[0]).toContain('Folder level 1');
});
