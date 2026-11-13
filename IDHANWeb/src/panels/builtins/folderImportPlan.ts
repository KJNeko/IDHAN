import type {ImportFile} from './importFiles';

export interface FolderTagPlan {
    tags: string[];
    errors: string[];
}

export function folderNames(source: ImportFile, includeRoot: boolean): string[] {
    const folders = source.path.split('/').slice(0, -1);
    return includeRoot ? folders : folders.slice(1);
}

/** One tag per line allows tag names to contain spaces and commas. */
export function parseImportTags(value: string): string[] {
    return [...new Set(value.split('\n').map(tag => tag.trim()).filter(Boolean))];
}

/** Add mapped folder values to shared tags; filenames and file extensions are never included. */
export function planFolderTags(source: ImportFile, namespaces: readonly string[], includeRoot: boolean, sharedTags: readonly string[] = []): FolderTagPlan {
    const folders = folderNames(source, includeRoot);
    const tags: string[] = [...sharedTags];
    const errors: string[] = [];
    namespaces.forEach((raw, index) => {
        const namespace = raw.trim();
        if (!namespace) return;
        if ([':', '/', '{', '}', '\n', '\r'].some(character => namespace.includes(character))) {
            errors.push(`Folder level ${index + 1}: enter a namespace such as artist or character, without punctuation.`);
            return;
        }
        const folder = folders[index]?.trim();
        if (!folder) {
            errors.push(`Missing folder level ${index + 1} for namespace "${namespace}".`);
            return;
        }
        tags.push(`${namespace}:${folder}`);
    });
    return {tags: [...new Set(tags)], errors};
}
