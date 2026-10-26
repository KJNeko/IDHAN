import {describe, expect, it} from 'vitest';
import {
    type CellGrid,
    classifyDifference,
    clusterRegions,
    type DiffSurvey,
    markBoxes,
    NEAR_IDENTICAL_SHARE,
    regionAt,
    regionWindow,
} from './imageDifference';

/**
 * Each character represents a 32px cell: '.' is empty; other characters mark the entire cell as
 * changed.
 */
function gridOf(picture: string[], cell = 32): CellGrid {
    const columns = picture[0]!.length;
    const rows = picture.length;
    const size = columns * rows;
    const grid: CellGrid = {
        columns,
        rows,
        counts: new Int32Array(size),
        minX: new Int32Array(size).fill(columns * cell),
        minY: new Int32Array(size).fill(rows * cell),
        maxX: new Int32Array(size).fill(-1),
        maxY: new Int32Array(size).fill(-1),
    };

    picture.forEach((line, row) => {
        [...line].forEach((mark, column) => {
            if (mark === '.') return;
            const index = row * columns + column;
            grid.counts[index] = Number.parseInt(mark, 36);
            grid.minX[index] = column * cell;
            grid.minY[index] = row * cell;
            grid.maxX[index] = column * cell + cell - 1;
            grid.maxY[index] = row * cell + cell - 1;
        });
    });

    return grid;
}

function surveyOf(over: Partial<DiffSurvey>): DiffSurvey {
    return {
        width: 1000,
        height: 1000,
        changed: 0,
        notable: 0,
        regions: [],
        dropped: 0,
        overview: new Uint8ClampedArray(1),
        overviewWidth: 1,
        overviewHeight: 1,
        ...over,
    };
}

describe('clusterRegions', () => {
    it('finds nothing in a grid with no differences', () => {
        expect(clusterRegions(gridOf(['...', '...'])).regions).toEqual([]);
    });

    it('keeps separated differences apart', () => {
        const {regions} = clusterRegions(gridOf([
            '1..',
            '...',
            '..1',
        ]));

        expect(regions).toHaveLength(2);
    });

    it('joins cells that touch only at a corner', () => {
        const {regions} = clusterRegions(gridOf([
            '1..',
            '.1.',
            '..1',
        ]));

        expect(regions).toHaveLength(1);
        expect(regions[0]).toMatchObject({x: 0, y: 0, width: 96, height: 96});
    });

    it('bounds a region by the pixels that differed, not the cells they sat in', () => {
        const grid = gridOf(['1']);
        grid.minX[0] = 10;
        grid.minY[0] = 12;
        grid.maxX[0] = 14;
        grid.maxY[0] = 13;

        expect(clusterRegions(grid).regions[0]).toMatchObject({x: 10, y: 12, width: 5, height: 2});
    });

    it('sums the pixels of every cell it joined', () => {
        const {regions} = clusterRegions(gridOf([
            '23',
            '4.',
        ]));

        expect(regions[0]?.pixels).toBe(9);
    });

    it('reports the biggest regions first and counts what the limit dropped', () => {
        const {regions, dropped} = clusterRegions(gridOf([
            '1.9',
            '...',
            '5.3',
        ]), 2);

        expect(regions.map((region) => region.pixels)).toEqual([9, 5]);
        expect(dropped).toBe(2);
    });
});

describe('classifyDifference', () => {
    it('calls a pair with no changed byte identical', () => {
        expect(classifyDifference(surveyOf({}))).toEqual({kind: 'identical'});
    });

    it('separates a re-encode from a real edit', () => {
        const verdict = classifyDifference(surveyOf({changed: 1_000_000, notable: 0}));
        expect(verdict).toEqual({kind: 'noise', changed: 1_000_000});
    });

    it('reads a small edit on an otherwise matching image as near-identical', () => {
        const verdict = classifyDifference(surveyOf({
            changed: 5_000,
            notable: 5_000,
            regions: [{x: 0, y: 0, width: 50, height: 100, pixels: 5_000}],
        }));

        expect(verdict.kind).toBe('near-identical');
        expect(verdict).toMatchObject({regions: 1});
    });

    it('counts the regions the cap dropped in the verdict', () => {
        const verdict = classifyDifference(surveyOf({
            changed: 5_000,
            notable: 5_000,
            regions: [{x: 0, y: 0, width: 50, height: 100, pixels: 5_000}],
            dropped: 3,
        }));

        expect(verdict).toMatchObject({kind: 'near-identical', regions: 4});
    });

    it('stops calling a pair near-identical once too much of it moved', () => {
        const notable = Math.round(1000 * 1000 * (1 - NEAR_IDENTICAL_SHARE)) + 1;
        const verdict = classifyDifference(surveyOf({changed: notable, notable}));

        expect(verdict.kind).toBe('different');
    });
});

describe('regionWindow', () => {
    it('grows a single changed pixel into a window worth looking at', () => {
        const window = regionWindow({x: 500, y: 500, width: 1, height: 1, pixels: 1}, 1000, 1000);

        expect(window.width).toBe(96);
        expect(window.height).toBe(96);
        expect(window.x).toBe(453);
        expect(window.y).toBe(453);
    });

    it('keeps the window inside the image when the region sits on an edge', () => {
        const window = regionWindow({x: 0, y: 0, width: 2, height: 2, pixels: 4}, 1000, 1000);

        expect(window.x).toBe(0);
        expect(window.y).toBe(0);
    });

    it('never asks for more than the image has', () => {
        const window = regionWindow({x: 0, y: 0, width: 40, height: 40, pixels: 1}, 50, 30);

        expect(window).toMatchObject({x: 0, y: 0, width: 50, height: 30});
    });
});

describe('regionAt', () => {
    const survey = surveyOf({
        regions: [
            {x: 0, y: 0, width: 10, height: 10, pixels: 100},
            {x: 900, y: 900, width: 10, height: 10, pixels: 50},
        ],
    });

    it('picks the region a point lands in', () => {
        expect(regionAt(survey, 905, 905)).toBe(1);
    });

    it('picks the nearest region for a point in empty space', () => {
        expect(regionAt(survey, 100, 100)).toBe(0);
    });

    it('has nothing to pick when no region was found', () => {
        expect(regionAt(surveyOf({}), 10, 10)).toBeNull();
    });
});

describe('markBoxes', () => {
    const region = {x: 100, y: 100, width: 50, height: 50, pixels: 10};

    it('scales a region into a frame the image exactly fills', () => {
        const [box] = markBoxes([region], {width: 500, height: 500}, {width: 1000, height: 1000}, 0);

        expect(box).toEqual({left: 50, top: 50, width: 25, height: 25});
    });

    it('follows the letterboxing when the frame is wider than the image', () => {
        // 1000x1000 into 800x400 fits at 0.4, leaving 200px of empty frame on each side.
        const [box] = markBoxes([region], {width: 800, height: 400}, {width: 1000, height: 1000}, 0);

        expect(box).toEqual({left: 200 + 40, top: 40, width: 20, height: 20});
    });

    it('leaves the margin around the change rather than over it', () => {
        const [box] = markBoxes([region], {width: 1000, height: 1000}, {width: 1000, height: 1000}, 3);

        expect(box).toEqual({left: 97, top: 97, width: 56, height: 56});
    });

    it('has nothing to place for an image or frame with no size', () => {
        expect(markBoxes([region], {width: 0, height: 0}, {width: 10, height: 10})).toEqual([]);
        expect(markBoxes([region], {width: 10, height: 10}, {width: 0, height: 0})).toEqual([]);
    });
});
