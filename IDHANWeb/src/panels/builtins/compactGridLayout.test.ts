import {describe, expect, it} from 'vitest';
import {compactGridLayout, imageRatio, COMPACT_GAP} from './compactGridLayout';

describe('compact thumbnail packing', () => {
    it('uses equal widths and preserves mixed aspect ratios', () => {
        const ratios = [0.5, 2, 1, 3, 0.25];
        const {tiles} = compactGridLayout(ratios, 500, 128);
        tiles.forEach((tile, index) => {
            expect(tile.width / tile.height).toBeCloseTo(ratios[index]!);
            expect(tile.width).toBe(tiles[0]!.width);
            expect(tile.width).toBeLessThanOrEqual(128);
        });
    });

    it('packs into space above the bottom of taller neighbors', () => {
        const {tiles} = compactGridLayout([0.5, 2, 2], 261, 128);
        expect(tiles[0]).toMatchObject({x: 0, y: 0, width: 128, height: 256});
        expect(tiles[1]).toMatchObject({x: 133, y: 0, width: 128, height: 64});
        expect(tiles[2]).toMatchObject({x: 133, y: 69});
    });

    it('keeps 5px clearance and stays inside narrow and wide panels', () => {
        for (const width of [40, 128, 261, 640]) {
            const ratios = Array.from({length: 100}, (_, index) => [0.5, 1, 2, 0.75, 3][index % 5]!);
            const {tiles, height} = compactGridLayout(ratios, width, 128);
            for (const [index, tile] of tiles.entries()) {
                expect(tile.x).toBeGreaterThanOrEqual(0);
                expect(tile.x + tile.width).toBeLessThanOrEqual(width + 0.001);
                expect(tile.y + tile.height).toBeLessThanOrEqual(height);
                for (const other of tiles.slice(index + 1)) {
                    const separated = tile.x + tile.width + COMPACT_GAP <= other.x + 0.001
                        || other.x + other.width + COMPACT_GAP <= tile.x + 0.001
                        || tile.y + tile.height + COMPACT_GAP <= other.y + 0.001
                        || other.y + other.height + COMPACT_GAP <= tile.y + 0.001;
                    expect(separated).toBe(true);
                }
            }
        }
    });

    it('handles empty results and unknown dimensions', () => {
        expect(compactGridLayout([], 500, 128)).toEqual({tiles: [], height: 0});
        expect(imageRatio(undefined, 100)).toBe(1);
        expect(imageRatio(0, 100)).toBe(1);
        expect(imageRatio(-1, 100)).toBe(1);
        expect(imageRatio(Infinity, 100)).toBe(1);
        expect(imageRatio(200, 100)).toBe(2);
    });
});
