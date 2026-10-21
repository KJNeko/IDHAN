import {describe, expect, it} from 'vitest';
import type {RecordMetadata} from '../../api/types';
import {compareQuality, encodingLabel} from './recordQuality';

function record(fields: Partial<RecordMetadata>): RecordMetadata {
    return {record_id: 1, hashes: {sha256: '0'.repeat(64)}, ...fields};
}

const JPEG = {width: 1000, height: 1000, size: 1_000_000, lossless: false, quality: 80} as const;

describe('compareQuality', () => {
    it('prefers the larger resolution, and much more so when it is not close', () => {
        const modest = compareQuality(
            record({...JPEG, width: 1200}),
            record(JPEG),
        );
        expect(modest.score).toBe(20);
        expect(modest.reasons).toEqual([{label: 'Higher resolution', points: 20}]);

        const doubled = compareQuality(record({...JPEG, width: 2000}), record(JPEG));
        expect(doubled.score).toBe(50);
    });

    it('prefers a lossless copy, and stacks that with its resolution', () => {
        const png = record({...JPEG, width: 2000, lossless: true, quality: undefined});
        const verdict = compareQuality(png, record(JPEG));

        expect(verdict.score).toBe(80);
        expect(verdict.reasons.map((reason) => reason.label)).toEqual(['Higher resolution', 'Lossless']);
    });

    it('reads a quality gap as a re-save until it is wide', () => {
        expect(compareQuality(record({...JPEG, quality: 90}), record(JPEG)).score).toBe(10);
        expect(compareQuality(record({...JPEG, quality: 95}), record(JPEG)).score).toBe(20);
        expect(compareQuality(record(JPEG), record({...JPEG, quality: 95})).score).toBe(-20);
    });

    it('counts a block only when the other copy is known to lack it', () => {
        const carries = record({...JPEG, has_icc_profile: true});

        expect(compareQuality(carries, record({...JPEG, has_icc_profile: false})).score).toBe(5);
        // Unknown fields do not affect scoring.
        expect(compareQuality(carries, record(JPEG)).score).toBe(0);
    });

    it('lets the smaller file win only once nothing else separates the two', () => {
        const smaller = compareQuality(record({...JPEG, size: 500_000}), record(JPEG));
        expect(smaller.score).toBe(1);
        expect(smaller.tiebreak).toBe(true);

        const informative = compareQuality(
            record({...JPEG, has_exif: true}),
            record({...JPEG, size: 500_000, has_exif: false}),
        );
        expect(informative.score).toBe(5);
        expect(informative.tiebreak).toBe(false);
    });

    it('ranks chroma modes, and leaves greyscale to the channel count', () => {
        const full = record({...JPEG, chroma_subsampling: '4:4:4', channels: 3});
        expect(compareQuality(full, record({...JPEG, chroma_subsampling: '4:2:0', channels: 3})).score).toBe(10);

        // Channel count accounts for greyscale; subsampling must not score it again.
        const grey = record({...JPEG, chroma_subsampling: 'greyscale', channels: 1});
        expect(compareQuality(full, grey).score).toBe(10);
        expect(compareQuality(full, grey).reasons).toEqual([{label: 'More channels', points: 10}]);
    });

    it('prefers the copy that still names its encoder', () => {
        const sai = record({...JPEG, comment: 'Paint Tool -SAI- JPEG Encoder v1.00'});

        expect(compareQuality(sai, record({...JPEG, comment: ''})).score).toBe(5);
        expect(compareQuality(sai, record(JPEG)).score).toBe(0);
        expect(compareQuality(sai, record({...JPEG, comment: '   '})).score).toBe(5);
    });

    /**
     * Regression fixture: equal dimensions and quantization tables, but the larger file lacks
     * EXIF and COM metadata.
     */
    it('picks the SAI original over a larger q100 re-encode of it', () => {
        const shared = {
            width: 1800, height: 2800, channels: 3, mime: 'image/jpeg',
            lossless: false, quality: 100, chroma_subsampling: '4:4:4', progressive: false,
            has_gps: false, has_icc_profile: false, has_iptc: false, has_xmp: false,
        } as const;
        const reEncode = record({...shared, size: 1_904_184, has_exif: false, comment: ''});
        const original = record({
            ...shared, size: 1_792_456, has_exif: true, comment: 'Paint Tool -SAI- JPEG Encoder v1.00',
        });

        const verdict = compareQuality(original, reEncode);
        expect(verdict.score).toBe(10);
        expect(verdict.reasons.map((reason) => reason.label)).toEqual(['Names its encoder', 'Carries EXIF']);
        expect(verdict.tiebreak).toBe(false);
    });

    it('decides nothing without both sides', () => {
        expect(compareQuality(record(JPEG), undefined).score).toBe(0);
        expect(compareQuality(undefined, undefined).reasons).toEqual([]);
    });
});

describe('encodingLabel', () => {
    it('leads with lossy or lossless and appends what the format gave up', () => {
        expect(encodingLabel(record({lossless: true}))).toBe('Lossless');
        expect(
            encodingLabel(record({lossless: false, quality: 85, chroma_subsampling: '4:2:0', progressive: true})),
        ).toBe('Lossy · q85 · 4:2:0 · progressive');
    });

    it('says nothing about an image nobody has read the encoding of', () => {
        expect(encodingLabel(record({}))).toBeNull();
        expect(encodingLabel(undefined)).toBeNull();
    });
});
