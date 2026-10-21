/**
 * Rank records using available image and encoding metadata. Compare a field only when both
 * records report it. Prefer smaller files when the aggregate score is zero.
 */

import type {RecordMetadata} from '../../api/types';
import {EMBEDDED_BLOCKS} from './RecordInfoView';

export interface QualityReason {
    label: string;
    /** Points to the first record. Negative favours the second. */
    points: number;
}

export interface QualityVerdict {
    /** Positive favors the first record; negative favors the second. */
    score: number;
    reasons: QualityReason[];
    /** File size resolved a zero aggregate quality score. */
    tiebreak: boolean;
}

const POINTS = {
    resolutionMuch: 50,
    resolution: 20,
    lossless: 30,
    qualityMuch: 20,
    quality: 10,
    subsampling: 10,
    channels: 10,
    embedded: 5,
    size: 1,
} as const;

/** Pixel-count ratio threshold for the larger resolution weight. */
const MUCH_LARGER = 2;

/** IJG quality difference threshold for the larger quality weight. */
const MUCH_HIGHER_QUALITY = 15;

/** Rank chroma resolution. Exclude greyscale because channel count already scores it. */
const SUBSAMPLING_RANK: Record<string, number> = {
    '4:4:4': 3,
    '4:2:2': 2,
    '4:4:0': 2,
    '4:2:0': 1,
};

function pixelsOf(metadata: RecordMetadata): number | undefined {
    if (!metadata.width || !metadata.height) return undefined;
    return metadata.width * metadata.height;
}

export function encodingLabel(metadata: RecordMetadata | undefined): string | null {
    if (metadata?.lossless === undefined) return null;

    const parts = [metadata.lossless ? 'Lossless' : 'Lossy'];
    if (metadata.quality !== undefined) parts.push(`q${metadata.quality}`);
    if (metadata.chroma_subsampling) parts.push(metadata.chroma_subsampling);
    if (metadata.progressive) parts.push('progressive');

    return parts.join(' · ');
}

export function compareQuality(first?: RecordMetadata, second?: RecordMetadata): QualityVerdict {
    const reasons: QualityReason[] = [];
    if (!first || !second) return {score: 0, reasons, tiebreak: false};

    function add(label: string, points: number) {
        reasons.push({label, points});
    }

    const firstPixels = pixelsOf(first);
    const secondPixels = pixelsOf(second);
    if (firstPixels !== undefined && secondPixels !== undefined && firstPixels !== secondPixels) {
        const ratio = Math.max(firstPixels, secondPixels) / Math.min(firstPixels, secondPixels);
        const weight = ratio >= MUCH_LARGER ? POINTS.resolutionMuch : POINTS.resolution;
        add('Higher resolution', firstPixels > secondPixels ? weight : -weight);
    }

    if (first.lossless !== undefined && second.lossless !== undefined && first.lossless !== second.lossless)
        add('Lossless', first.lossless ? POINTS.lossless : -POINTS.lossless);

    if (first.quality !== undefined && second.quality !== undefined && first.quality !== second.quality) {
        const gap = Math.abs(first.quality - second.quality);
        const weight = gap >= MUCH_HIGHER_QUALITY ? POINTS.qualityMuch : POINTS.quality;
        add('Higher encoder quality', first.quality > second.quality ? weight : -weight);
    }

    const firstChroma = SUBSAMPLING_RANK[first.chroma_subsampling ?? ''];
    const secondChroma = SUBSAMPLING_RANK[second.chroma_subsampling ?? ''];
    if (firstChroma !== undefined && secondChroma !== undefined && firstChroma !== secondChroma)
        add('Less chroma subsampling', firstChroma > secondChroma ? POINTS.subsampling : -POINTS.subsampling);

    if (first.channels !== undefined && second.channels !== undefined && first.channels !== second.channels)
        add('More channels', first.channels > second.channels ? POINTS.channels : -POINTS.channels);

    // Weight nonempty COM text as an embedded metadata block.
    if (first.comment !== undefined && second.comment !== undefined) {
        const firstNames = first.comment.trim() !== '';
        const secondNames = second.comment.trim() !== '';
        if (firstNames !== secondNames) add('Names its encoder', firstNames ? POINTS.embedded : -POINTS.embedded);
    }

    for (const {key, label} of EMBEDDED_BLOCKS) {
        if (first[key] === true && second[key] === false) add(`Carries ${label}`, POINTS.embedded);
        if (second[key] === true && first[key] === false) add(`Carries ${label}`, -POINTS.embedded);
    }

    const score = reasons.reduce((total, reason) => total + reason.points, 0);
    if (score !== 0) return {score, reasons, tiebreak: false};

    if (first.size === undefined || second.size === undefined || first.size === second.size)
        return {score: 0, reasons, tiebreak: false};

    const points = first.size < second.size ? POINTS.size : -POINTS.size;
    add('Smaller file', points);
    return {score: points, reasons, tiebreak: true};
}
