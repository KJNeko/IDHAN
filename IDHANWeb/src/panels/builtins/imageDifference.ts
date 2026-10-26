/**
 * Compare equal-size images at full resolution in strips. Store each overview block's maximum
 * channel delta to preserve isolated pixel changes, and cluster notable differences into
 * regions.
 */

/** 128 RGBA rows require about 4 MB per 8000px-wide image. */
const STRIP_ROWS = 128;

/** Occupancy-grid cell edge length in image pixels. */
const CELL = 32;

/**
 * Minimum channel delta for region detection; smaller differences count only toward changed
 * pixels.
 */
export const NOTABLE_DELTA = 12;

/** Limit overview dimensions without reducing comparison resolution. */
export const OVERVIEW_MAX_EDGE = 1024;

export const MAX_REGIONS = 24;

/** Amplify channel differences for display. */
export const DIFF_GAIN = 4;

/** Region crop padding in image pixels. */
const REGION_PAD = 24;

/** Minimum region crop edge length in image pixels. */
const REGION_MIN = 96;

/** Maximum crop edge length in image pixels; larger regions are cropped around their center. */
const REGION_MAX = 2048;

/** Minimum fraction of pixels below NOTABLE_DELTA for near-identical classification. */
export const NEAR_IDENTICAL_SHARE = 0.95;

export interface DiffRegion {
    x: number;
    y: number;
    width: number;
    height: number;
    /** Pixels inside the box differing by at least `NOTABLE_DELTA`. */
    pixels: number;
}

export interface DiffSurvey {
    width: number;
    height: number;
    /** Pixels whose bytes differ at all, alpha included. */
    changed: number;
    /** Pixels with at least one channel delta >= NOTABLE_DELTA. */
    notable: number;
    /** Descending notable-pixel count. */
    regions: DiffRegion[];
    /** Regions past `MAX_REGIONS` that were dropped. */
    dropped: number;
    /** Largest delta in each block of the image, laid out `overviewWidth` wide. */
    overview: Uint8ClampedArray;
    overviewWidth: number;
    overviewHeight: number;
}

/** `noise` indicates byte differences below NOTABLE_DELTA. */
export type DiffVerdict =
    | { kind: 'identical' }
    | { kind: 'noise'; changed: number }
    | { kind: 'near-identical'; share: number; regions: number }
    | { kind: 'different'; share: number };

export interface CellGrid {
    columns: number;
    rows: number;
    /** Number of notable pixels per cell. */
    counts: Int32Array;
    /** Exact bounds of the notable pixels in each cell, in image coordinates. */
    minX: Int32Array;
    minY: Int32Array;
    maxX: Int32Array;
    maxY: Int32Array;
}

function stripContext(width: number): CanvasRenderingContext2D {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = STRIP_ROWS;
    const context = canvas.getContext('2d', {willReadFrequently: true});
    if (!context) throw new Error('This browser did not provide a 2D canvas');
    return context;
}

function readStrip(
    context: CanvasRenderingContext2D,
    image: HTMLImageElement,
    top: number,
    rows: number,
    width: number,
): Uint8ClampedArray {
    context.clearRect(0, 0, width, rows);
    context.drawImage(image, 0, top, width, rows, 0, 0, width, rows);
    return context.getImageData(0, 0, width, rows).data;
}

/** Precompute block indices to avoid division in the pixel loop. */
function blockMap(length: number, blocks: number): Int32Array {
    const map = new Int32Array(length);
    for (let index = 0; index < length; index++) map[index] = Math.min(blocks - 1, ((index * blocks) / length) | 0);
    return map;
}

/** Cluster 8-connected cells using exact notable-pixel bounds. */
export function clusterRegions(grid: CellGrid, limit: number = MAX_REGIONS): { regions: DiffRegion[]; dropped: number } {
    const {columns, rows, counts, minX, minY, maxX, maxY} = grid;
    const seen = new Uint8Array(counts.length);
    const found: DiffRegion[] = [];

    for (let start = 0; start < counts.length; start++) {
        if (seen[start] === 1 || counts[start] === 0) continue;

        let left = minX[start]!;
        let top = minY[start]!;
        let right = maxX[start]!;
        let bottom = maxY[start]!;
        let pixels = 0;

        seen[start] = 1;
        const stack = [start];

        while (stack.length > 0) {
            const cell = stack.pop()!;
            pixels += counts[cell]!;
            if (minX[cell]! < left) left = minX[cell]!;
            if (minY[cell]! < top) top = minY[cell]!;
            if (maxX[cell]! > right) right = maxX[cell]!;
            if (maxY[cell]! > bottom) bottom = maxY[cell]!;

            const column = cell % columns;
            const row = (cell / columns) | 0;

            for (let dy = -1; dy <= 1; dy++) {
                for (let dx = -1; dx <= 1; dx++) {
                    const nx = column + dx;
                    const ny = row + dy;
                    if (nx < 0 || ny < 0 || nx >= columns || ny >= rows) continue;
                    const neighbour = ny * columns + nx;
                    if (seen[neighbour] === 1 || counts[neighbour] === 0) continue;
                    seen[neighbour] = 1;
                    stack.push(neighbour);
                }
            }
        }

        found.push({x: left, y: top, width: right - left + 1, height: bottom - top + 1, pixels});
    }

    found.sort((one, other) => other.pixels - one.pixels);
    return {regions: found.slice(0, limit), dropped: Math.max(0, found.length - limit)};
}

export function classifyDifference(survey: DiffSurvey): DiffVerdict {
    if (survey.changed === 0) return {kind: 'identical'};
    if (survey.notable === 0) return {kind: 'noise', changed: survey.changed};

    const share = 1 - survey.notable / (survey.width * survey.height);
    if (share >= NEAR_IDENTICAL_SHARE)
        return {kind: 'near-identical', share, regions: survey.regions.length + survey.dropped};
    return {kind: 'different', share};
}

/** Yield between full-resolution strips to keep the UI responsive. */
export async function surveyDifference(
    a: HTMLImageElement,
    b: HTMLImageElement,
    cancelled: () => boolean,
    onProgress?: (done: number) => void,
): Promise<DiffSurvey> {
    const width = a.naturalWidth;
    const height = a.naturalHeight;
    if (width === 0 || height === 0) throw new Error('The file could not be measured as an image');
    if (width !== b.naturalWidth || height !== b.naturalHeight)
        throw new Error('The two files are different sizes, so their pixels do not line up');

    const scale = Math.min(1, OVERVIEW_MAX_EDGE / Math.max(width, height));
    const overviewWidth = Math.max(1, Math.round(width * scale));
    const overviewHeight = Math.max(1, Math.round(height * scale));
    const overview = new Uint8ClampedArray(overviewWidth * overviewHeight);

    const columns = Math.ceil(width / CELL);
    const cellRows = Math.ceil(height / CELL);
    const grid: CellGrid = {
        columns,
        rows: cellRows,
        counts: new Int32Array(columns * cellRows),
        minX: new Int32Array(columns * cellRows).fill(width),
        minY: new Int32Array(columns * cellRows).fill(height),
        maxX: new Int32Array(columns * cellRows).fill(-1),
        maxY: new Int32Array(columns * cellRows).fill(-1),
    };

    const overviewOfColumn = blockMap(width, overviewWidth);
    const cellOfColumn = blockMap(width, columns);
    const left = stripContext(width);
    const right = stripContext(width);

    let changed = 0;
    let notable = 0;

    for (let top = 0; top < height; top += STRIP_ROWS) {
        if (cancelled()) break;

        const rows = Math.min(STRIP_ROWS, height - top);
        const leftData = readStrip(left, a, top, rows, width);
        const rightData = readStrip(right, b, top, rows, width);

        for (let row = 0; row < rows; row++) {
            const y = top + row;
            const overviewRow = Math.min(overviewHeight - 1, ((y * overviewHeight) / height) | 0) * overviewWidth;
            const cellRow = ((y / CELL) | 0) * columns;
            let index = row * width * 4;

            for (let x = 0; x < width; x++, index += 4) {
                const dr = Math.abs(leftData[index]! - rightData[index]!);
                const dg = Math.abs(leftData[index + 1]! - rightData[index + 1]!);
                const db = Math.abs(leftData[index + 2]! - rightData[index + 2]!);
                const da = Math.abs(leftData[index + 3]! - rightData[index + 3]!);

                let delta = dr > dg ? dr : dg;
                if (db > delta) delta = db;
                if (da > delta) delta = da;
                if (delta === 0) continue;

                changed++;
                const block = overviewRow + overviewOfColumn[x]!;
                if (delta > overview[block]!) overview[block] = delta;
                if (delta < NOTABLE_DELTA) continue;

                notable++;
                const cell = cellRow + cellOfColumn[x]!;
                grid.counts[cell]!++;
                if (x < grid.minX[cell]!) grid.minX[cell] = x;
                if (y < grid.minY[cell]!) grid.minY[cell] = y;
                if (x > grid.maxX[cell]!) grid.maxX[cell] = x;
                if (y > grid.maxY[cell]!) grid.maxY[cell] = y;
            }
        }

        onProgress?.(Math.min(1, (top + rows) / height));
        await new Promise((resolve) => {
            setTimeout(resolve, 0);
        });
    }

    const {regions, dropped} = clusterRegions(grid);
    return {width, height, changed, notable, regions, dropped, overview, overviewWidth, overviewHeight};
}

/** Overlay padding in image pixels. */
const MARK_MARGIN = 3;

export interface Size {
    width: number;
    height: number;
}

/** Region bounds in rendered element coordinates. */
export interface MarkBox {
    left: number;
    top: number;
    width: number;
    height: number;
}

/** Project padded region bounds through object-fit: contain, accounting for letterboxing. */
export function markBoxes(
    regions: readonly DiffRegion[],
    frame: Size,
    image: Size,
    margin: number = MARK_MARGIN,
): MarkBox[] {
    if (image.width <= 0 || image.height <= 0 || frame.width <= 0 || frame.height <= 0) return [];

    const scale = Math.min(frame.width / image.width, frame.height / image.height);
    const offsetX = (frame.width - image.width * scale) / 2;
    const offsetY = (frame.height - image.height * scale) / 2;

    return regions.map((region) => ({
        left: offsetX + (region.x - margin) * scale,
        top: offsetY + (region.y - margin) * scale,
        width: (region.width + margin * 2) * scale,
        height: (region.height + margin * 2) * scale,
    }));
}

/** Pad the region crop, enforce minimum dimensions, and clamp to image bounds. */
export function regionWindow(region: DiffRegion, width: number, height: number): DiffRegion {
    const wide = Math.min(REGION_MAX, Math.max(REGION_MIN, region.width + REGION_PAD * 2));
    const tall = Math.min(REGION_MAX, Math.max(REGION_MIN, region.height + REGION_PAD * 2));
    const boxWidth = Math.min(width, wide);
    const boxHeight = Math.min(height, tall);
    const centreX = region.x + region.width / 2;
    const centreY = region.y + region.height / 2;

    return {
        x: Math.max(0, Math.min(width - boxWidth, Math.round(centreX - boxWidth / 2))),
        y: Math.max(0, Math.min(height - boxHeight, Math.round(centreY - boxHeight / 2))),
        width: boxWidth,
        height: boxHeight,
        pixels: region.pixels,
    };
}

/** Compute a difference crop at one output pixel per source pixel. */
export function paintRegion(
    canvas: HTMLCanvasElement,
    a: HTMLImageElement,
    b: HTMLImageElement,
    view: DiffRegion,
): void {
    function crop(image: HTMLImageElement): ImageData {
        const offscreen = document.createElement('canvas');
        offscreen.width = view.width;
        offscreen.height = view.height;
        const context = offscreen.getContext('2d', {willReadFrequently: true});
        if (!context) throw new Error('This browser did not provide a 2D canvas');
        context.drawImage(image, view.x, view.y, view.width, view.height, 0, 0, view.width, view.height);
        return context.getImageData(0, 0, view.width, view.height);
    }

    const left = crop(a);
    const right = crop(b);
    const output = new ImageData(view.width, view.height);

    for (let index = 0; index < output.data.length; index += 4) {
        const delta = Math.max(
            Math.abs(left.data[index]! - right.data[index]!),
            Math.abs(left.data[index + 1]! - right.data[index + 1]!),
            Math.abs(left.data[index + 2]! - right.data[index + 2]!),
        );
        const lit = Math.min(255, delta * DIFF_GAIN);
        output.data[index] = lit;
        output.data[index + 1] = lit;
        output.data[index + 2] = lit;
        output.data[index + 3] = 255;
    }

    canvas.width = view.width;
    canvas.height = view.height;
    const context = canvas.getContext('2d');
    if (!context) throw new Error('This browser did not provide a 2D canvas');
    context.putImageData(output, 0, 0);
}

export function paintOverview(canvas: HTMLCanvasElement, survey: DiffSurvey, selected: number | null): void {
    const {overviewWidth: width, overviewHeight: height} = survey;
    const output = new ImageData(width, height);

    for (let block = 0; block < survey.overview.length; block++) {
        const lit = Math.min(255, survey.overview[block]! * DIFF_GAIN);
        const index = block * 4;
        output.data[index] = lit;
        output.data[index + 1] = lit;
        output.data[index + 2] = lit;
        output.data[index + 3] = 255;
    }

    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext('2d');
    if (!context) throw new Error('This browser did not provide a 2D canvas');
    context.putImageData(output, 0, 0);

    context.lineWidth = 1;
    survey.regions.forEach((region, index) => {
        const box = {
            x: (region.x / survey.width) * width,
            y: (region.y / survey.height) * height,
            width: Math.max(2, (region.width / survey.width) * width),
            height: Math.max(2, (region.height / survey.height) * height),
        };
        context.strokeStyle = index === selected ? '#ffb648' : '#4a80ff';
        context.strokeRect(box.x - 1.5, box.y - 1.5, box.width + 3, box.height + 3);
    });
}

export function regionAt(survey: DiffSurvey, x: number, y: number): number | null {
    let nearest: number | null = null;
    let best = Infinity;

    survey.regions.forEach((region, index) => {
        const dx = Math.max(region.x - x, 0, x - (region.x + region.width));
        const dy = Math.max(region.y - y, 0, y - (region.y + region.height));
        const distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            nearest = index;
        }
    });

    return nearest;
}

/**
 * Compare decoded RGBA bytes in strips, yielding between strips and stopping at the first
 * mismatch.
 */
export async function pixelsIdentical(
    a: HTMLImageElement,
    b: HTMLImageElement,
    cancelled: () => boolean,
): Promise<boolean> {
    const width = a.naturalWidth;
    const height = a.naturalHeight;
    if (width === 0 || height === 0) throw new Error('The file could not be measured as an image');
    if (width !== b.naturalWidth || height !== b.naturalHeight) return false;

    const left = stripContext(width);
    const right = stripContext(width);

    for (let top = 0; top < height; top += STRIP_ROWS) {
        if (cancelled()) return false;

        const rows = Math.min(STRIP_ROWS, height - top);
        const leftData = readStrip(left, a, top, rows, width);
        const rightData = readStrip(right, b, top, rows, width);

        for (let index = 0; index < rows * width * 4; index++) {
            if (leftData[index] !== rightData[index]) return false;
        }

        await new Promise((resolve) => {
            setTimeout(resolve, 0);
        });
    }

    return true;
}
