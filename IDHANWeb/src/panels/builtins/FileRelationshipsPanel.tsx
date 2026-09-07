/**
 * File Relationships: searches the perceptual-hash neighbourhood of one record, sent here from the
 * thumbnail grid's context menu. Picking a neighbour pins it as a B-side comparison against that
 * record, and the pair can then be marked as duplicates or alternatives.
 *
 * Neither the A side nor the B side touches the app-wide selection, so the grid and viewer stay
 * where the user left them while a duplicate queue is worked through here.
 *
 * The record's existing relationships are fetched only to annotate results and to disable actions
 * that are already in force, so they never block the search from rendering.
 */

import {useEffect, useRef, useState} from 'react';
import type {
    Dispatch,
    MouseEvent as ReactMouseEvent,
    PointerEvent as ReactPointerEvent,
    ReactNode,
    SetStateAction,
} from 'react';
import type {FileRelationships, RecordMetadata, SimilarRecord} from '../../api/types';
import type {PanelProps, RecordId} from '../../host/types';
import {EMBEDDED_BLOCKS, formatBytes, formatDuration} from './RecordInfoView';
import {useRecordMenu} from './recordActions';
import {
    classifyDifference,
    type DiffSurvey,
    type MarkBox,
    markBoxes,
    pixelsIdentical,
    NOTABLE_DELTA,
    paintOverview,
    paintRegion,
    regionAt,
    regionWindow,
    surveyDifference,
} from './imageDifference';
import {createLookaheadQueue} from './lookaheadQueue';
import {lastRelationshipsFocus, RELATIONSHIPS_FOCUS_TOPIC} from './relationshipsFocus';
import {compareQuality, encodingLabel, type QualityReason, type QualityVerdict} from './recordQuality';

type State =
    | { status: 'idle' }
    | { status: 'loading' }
    | { status: 'error'; message: string }
    | { status: 'ok'; relationships: FileRelationships; metadata: Map<RecordId, RecordMetadata> };

type SimilarState =
    | { status: 'idle' }
    | { status: 'loading' }
    | { status: 'unhashed' }
    | { status: 'error'; message: string }
    | {
    status: 'ok';
    records: SimilarRecord[];
    truncated: boolean;
    metadata: Map<RecordId, RecordMetadata>;
};

const DISTANCE_OPTIONS = [
    {value: 0, label: 'Exact (0)'},
    {value: 2, label: 'Very similar (2)'},
    {value: 4, label: 'Similar (4)'},
    {value: 8, label: 'Loose (8)'},
] as const;

const DEFAULT_DISTANCE = 4;

/** Pairs kept behind the Back button. */
const HISTORY_LIMIT = 32;

const TILE_THUMBNAIL = 256;

const LOOKAHEAD_PAIRS = 2;

/** Retain the current pair, lookahead pairs, and previous pair. */
const CACHE_LIMIT = (LOOKAHEAD_PAIRS + 2) * 2;

/** A pair that has been on screen, in A/B order. */
interface ComparedPair {
    a: RecordId;
    b: RecordId;
}

type CompareView = CompareLayout | 'diff';

const COMPARE_VIEWS = [
    {value: 'side-by-side', label: 'Side by side'},
    {value: 'flip', label: 'Flip'},
    {value: 'diff', label: 'Difference'},
] as const;

/** How a record already relates to the selected one, if at all. */
type Relation = 'superior' | 'inferior' | 'alternative' | 'unrelated';

/** Invert the stored relation when quality ordering swaps A and B. */
function flipRelation(relation: Relation | undefined): Relation | undefined {
    if (relation === 'superior') return 'inferior';
    if (relation === 'inferior') return 'superior';
    return relation;
}

interface Sides {
    a: RecordId;
    b: RecordId;
}

/** Use event.code so keypad bindings are independent of Num Lock. */
type Keypad = 'a' | 'b' | 'a-better' | 'b-better' | 'alternative' | 'unrelated' | 'diff' | 'layout';

const KEYPAD: Record<string, Keypad> = {
    Numpad1: 'diff',
    Numpad2: 'unrelated',
    Numpad3: 'layout',
    Numpad4: 'a',
    Numpad5: 'alternative',
    Numpad6: 'b',
    Numpad7: 'a-better',
    Numpad9: 'b-better',
};

const TYPED_INTO = new Set(['INPUT', 'TEXTAREA', 'SELECT']);

interface Bounds {
    width: number;
    height: number;
}

/** Panels never import the api layer, so match ApiError's status structurally. */
function isNotFound(error: unknown): boolean {
    return typeof error === 'object' && error !== null && (error as { status?: unknown }).status === 404;
}

function bitsLabel(distance: number): string {
    if (distance === 0) return 'exact';
    return `${distance} ${distance === 1 ? 'bit' : 'bits'}`;
}

function boundsOf(metadata?: RecordMetadata): Bounds | null {
    if (!metadata?.width || !metadata.height) return null;
    return {width: metadata.width, height: metadata.height};
}

/** Two files can only be compared pixel by pixel when they are the same size to begin with. */
function sameResolution(aBounds: Bounds | null, bBounds: Bounds | null): boolean {
    if (!aBounds || !bBounds) return false;
    return aBounds.width === bBounds.width && aBounds.height === bBounds.height;
}

function message(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

/** The server serves the file as an attachment, so the empty download keeps its stored name. */
function downloadRecord(host: PanelProps['host'], id: RecordId) {
    const anchor = document.createElement('a');
    anchor.href = host.records.fileUrl(id, {download: true});
    anchor.download = '';
    anchor.click();
}

interface FileCardProps {
    host: PanelProps['host'];
    id: RecordId;
    metadata?: RecordMetadata;
    selected?: boolean;
    active?: boolean;
    /** Replaces the dimensions in the meta row, e.g. `alternative (4 bits)`. */
    detail?: string;
    /** An existing relationship to the selected record, marked by the preview's background colour. */
    relation?: Relation;
    thumbnail?: boolean;

    onSelect(id: RecordId): void;

    onMenu?(event: ReactMouseEvent, id: RecordId): void;
}

function FileCard({
                      host,
                      id,
                      metadata,
                      selected = false,
                      active = false,
                      detail,
                      relation,
                      thumbnail = false,
                      onSelect,
                      onMenu,
                  }: FileCardProps) {
    const present = metadata?.size !== undefined;
    const dimensions = metadata?.width && metadata.height ? `${metadata.width} × ${metadata.height}` : null;

    return (
        <button
            type="button"
            className={`file-relationship-card${selected ? ' is-selected' : ''}${active ? ' is-active' : ''}${present ? '' : ' is-missing'}${relation ? ` is-relation-${relation}` : ''}`}
            onClick={() => onSelect(id)}
            onContextMenu={(event) => onMenu?.(event, id)}
            aria-pressed={selected || active}
            title={detail}
            aria-label={`${selected ? 'Record' : 'Compare record'} ${id}${present ? '' : ', file unavailable'}${detail ? `, ${detail}` : ''}`}
        >
      <span className="file-relationship-preview">
        {present ? (
            <img
                key={id}
                src={thumbnail ? host.records.thumbnailUrl(id, TILE_THUMBNAIL) : host.records.fileUrl(id)}
                alt=""
                draggable={false}
            />
        ) : (
            <span className="file-relationship-blur" aria-hidden="true"/>
        )}
          {!present && <span className="file-relationship-unavailable">File unavailable</span>}
      </span>
            <span className="file-relationship-meta">
        <strong>#{id}</strong>
                {detail === undefined ? (
                    dimensions && <span>{dimensions}</span>
                ) : (
                    <span className="file-relationship-detail">{detail}</span>
                )}
      </span>
        </button>
    );
}

interface CompareProps {
    host: PanelProps['host'];
    a: RecordId;
    b: RecordId;
    aBounds: Bounds | null;
    bBounds: Bounds | null;
}

/**
 * Percentage basis for both images: the larger bound on each axis. Each image then takes its own
 * share of the frame and letterboxes inside it, so both end up scaled by the same factor whatever
 * shape the frame is, and a lower-resolution copy visibly fills less of it.
 */
function frameFor(aBounds: Bounds | null, bBounds: Bounds | null): Bounds | null {
    if (!aBounds || !bBounds) return null;
    return {
        width: Math.max(aBounds.width, bBounds.width),
        height: Math.max(aBounds.height, bBounds.height),
    };
}

/** Both sides filling the frame, so the smaller copy is drawn at the larger one's size. */
const MATCHED_SHARE = {width: '100%', height: '100%'} as const;

function shareOf(frame: Bounds | null, bounds: Bounds | null): { width: string; height: string } | undefined {
    if (!frame || !bounds) return undefined;
    return {
        width: `${(bounds.width / frame.width) * 100}%`,
        height: `${(bounds.height / frame.height) * 100}%`,
    };
}

/** A field of a record worth weighing one side of a comparison against the other by. */
interface InfoRow {
    label: string;
    value: string;
    /** Bigger of the two, where bigger is a thing this field can be. */
    greater?: boolean;
}

const megapixels = (metadata: RecordMetadata): number | null =>
    metadata.width && metadata.height ? (metadata.width * metadata.height) / 1_000_000 : null;

/** Embedded blocks the file carries. Empty when it carries none, or was never parsed for them. */
function embeddedBlocks(metadata: RecordMetadata | undefined): string[] {
    if (!metadata) return [];
    return EMBEDDED_BLOCKS.filter(({key}) => metadata[key] === true).map(({label}) => label);
}

/**
 * What one side of the comparison is, as rows. `other` decides which side carries the larger value
 * of the fields where that is a reason to prefer one file: a bigger file at more pixels is usually
 * the copy worth keeping.
 */
function infoRows(metadata: RecordMetadata | undefined, other: RecordMetadata | undefined): InfoRow[] {
    if (!metadata) return [];

    const rows: InfoRow[] = [];

    if (metadata.size !== undefined) {
        rows.push({
            label: 'Size',
            value: formatBytes(metadata.size),
            greater: other?.size !== undefined && metadata.size > other.size,
        });
    }

    if (metadata.mime) rows.push({label: 'Type', value: metadata.mime});

    if (metadata.width && metadata.height) {
        const pixels = megapixels(metadata);
        const otherPixels = other ? megapixels(other) : null;
        rows.push({
            label: 'Dimensions',
            value: `${metadata.width} x ${metadata.height}${pixels ? ` (${pixels.toFixed(1)} MP)` : ''}`,
            greater: pixels !== null && otherPixels !== null && pixels > otherPixels,
        });
    }

    if (metadata.channels !== undefined) rows.push({label: 'Channels', value: String(metadata.channels)});

    if (metadata.duration !== undefined) rows.push({label: 'Duration', value: formatDuration(metadata.duration)});
    if (metadata.framerate !== undefined) rows.push({
        label: 'Framerate',
        value: `${metadata.framerate.toFixed(2)} fps`
    });
    if (metadata.frame_count !== undefined) rows.push({label: 'Frames', value: metadata.frame_count.toLocaleString()});

    return rows;
}

interface CompareInfoProps {
    which: 'a' | 'b';
    id: RecordId;
    metadata?: RecordMetadata;
    other?: RecordMetadata;
    /** How this side already relates to the other, when it does. */
    detail?: string;
    /** The two decode to the same pixels. Shown on both sides, since it is a fact about the pair. */
    pixelDuplicate: boolean;
    active: boolean;
    /** Reasons favoring this side; empty for the losing side or a tie. */
    advantages?: QualityReason[];
    best?: boolean;
    /** File size resolved a zero aggregate quality score. */
    tiebreak?: boolean;
}

interface AgeMarker {
    kind: 'older' | 'newer';
    timestamp: string;
}

/** Exact UTC text for the server's Unix-microsecond timestamp. */
function formatFileTime(timestamp: number): string {
    const milliseconds = Math.trunc(timestamp / 1000);
    const microseconds = Math.trunc(timestamp % 1_000_000);
    const date = new Date(milliseconds);

    return `${date.toISOString().slice(0, 19)}.${microseconds.toString().padStart(6, '0')} UTC`;
}

function ageMarker(metadata: RecordMetadata | undefined, other: RecordMetadata | undefined): AgeMarker | null {
    if (metadata?.file_mtime === undefined || other?.file_mtime === undefined) return null;
    if (metadata.file_mtime === other.file_mtime) return null;

    return {
        kind: metadata.file_mtime < other.file_mtime ? 'older' : 'newer',
        timestamp: formatFileTime(metadata.file_mtime),
    };
}

/** Show only criteria favoring each side. */
function advantagesOf(verdict: QualityVerdict, which: 'a' | 'b'): QualityReason[] {
    return verdict.reasons.filter((reason) => (which === 'a' ? reason.points > 0 : reason.points < 0));
}

/** The side-by-side facts a duplicate call is actually made on, embedded blocks first. */
function CompareInfo(
    {which, id, metadata, other, detail, pixelDuplicate, active, advantages = [], best = false, tiebreak = false}:
        CompareInfoProps,
) {
    const rows = infoRows(metadata, other);
    const embedded = embeddedBlocks(metadata);
    const encoding = encodingLabel(metadata);
    const comment = metadata?.comment?.trim() ?? '';
    // An absent comment field is unknown; only an explicit empty string establishes absence.
    const strippedComment = comment === '' && metadata?.comment === '' && (other?.comment?.trim() ?? '') !== '';
    const missingEmbedded = EMBEDDED_BLOCKS
        .filter(({key}) => metadata?.[key] === false && other?.[key] === true)
        .map(({label}) => label);
    const age = ageMarker(metadata, other);
    // Only an explicit false proves that the other side would lose the block; absent means unknown.
    const onlySide = EMBEDDED_BLOCKS.some(({key}) => metadata?.[key] === true && other?.[key] === false);

    return (
        <div className={`file-relationship-side-info is-${which}${active ? ' is-shown' : ''}`}>
            <h3>
                {which.toUpperCase()} · #{id}
                {best && (
                    <span
                        className={`file-relationship-side-best${tiebreak ? ' is-tiebreak' : ''}`}
                        title={tiebreak ? 'The same picture either way, and this is the smaller file' : undefined}
                    >
                        {tiebreak ? 'Smaller' : 'Best'}
                    </span>
                )}
            </h3>
            {encoding && (
                <p className={`file-relationship-side-encoding is-${metadata?.lossless ? 'lossless' : 'lossy'}`}>
                    {encoding}
                </p>
            )}
            {comment !== '' && (
                <p className="file-relationship-side-comment" title={comment}>
                    {comment}
                </p>
            )}
            {strippedComment && (
                <p className="file-relationship-side-embedded is-anti">Encoder comment stripped</p>
            )}
            {advantages.length > 0 && (
                <ul className="file-relationship-side-advantages">
                    {advantages.map((reason) => (
                        <li key={reason.label}>{reason.label}</li>
                    ))}
                </ul>
            )}
            {pixelDuplicate && <p className="file-relationship-side-match">Pixel-for-pixel duplicate</p>}
            {age && (
                <p className={`file-relationship-side-age is-${age.kind}`}>
                    <strong>{age.kind === 'older' ? 'Older' : 'Newer'}</strong>
                    <time>{age.timestamp}</time>
                </p>
            )}
            {embedded.length > 0 && (
                <p
                    className={`file-relationship-side-embedded is-norm${onlySide ? ' is-only' : ''}`}
                    title={onlySide ? 'The other copy does not carry all of these' : undefined}
                >
                    Contains {embedded.join(', ')}
                </p>
            )}
            {missingEmbedded.length > 0 && (
                <p className="file-relationship-side-embedded is-anti">
                    Does NOT contain {missingEmbedded.join(', ')}
                </p>
            )}
            {detail && <p className="file-relationship-side-detail">{detail}</p>}
            {rows.length === 0 ? (
                <p className="muted">No metadata</p>
            ) : (
                <dl>
                    {rows.map((row) => (
                        <div key={row.label} className={row.greater ? 'is-greater' : undefined}>
                            <dt>{row.label}</dt>
                            <dd title={row.greater ? `Larger than ${which === 'a' ? 'B' : 'A'}` : undefined}>{row.value}</dd>
                        </div>
                    ))}
                </dl>
            )}
        </div>
    );
}

/** One wheel notch, and the range the frame will hold. */
const ZOOM_STEP = 1.25;
const ZOOM_MAX = 256;

/** A shared view onto both files: pan in frame pixels from the centre, then scale. */
interface Zoom {
    scale: number;
    x: number;
    y: number;
}

const NO_ZOOM: Zoom = {scale: 1, x: 0, y: 0};

/** Keeps the magnified image covering the frame, so a pan cannot strand it off the edge. */
function clampPan(zoom: Zoom, width: number, height: number): Zoom {
    const limitX = (width * (zoom.scale - 1)) / 2;
    const limitY = (height * (zoom.scale - 1)) / 2;
    return {
        scale: zoom.scale,
        x: Math.min(limitX, Math.max(-limitX, zoom.x)),
        y: Math.min(limitY, Math.max(-limitY, zoom.y)),
    };
}

/**
 * Whole factors only, so a magnified pixel stays a square block. The step is multiplicative, and a
 * notch that would round back to where it started moves by one instead.
 */
function scaledBy(zoom: Zoom, factor: number): number {
    const stepped = Math.round(zoom.scale * factor);
    const moved = stepped === zoom.scale ? zoom.scale + (factor > 1 ? 1 : -1) : stepped;
    return Math.min(ZOOM_MAX, Math.max(1, moved));
}

/** Re-anchors the pan so whatever sits under (x, y) stays there as the scale changes. */
function zoomTo(current: Zoom, scale: number, x: number, y: number, width: number, height: number): Zoom {
    if (scale <= 1) return NO_ZOOM;

    const ratio = scale / current.scale;

    return clampPan({scale, x: x - (x - current.x) * ratio, y: y - (y - current.y) * ratio}, width, height);
}

type MarkState =
    | { status: 'off' }
    | { status: 'scanning' }
    | { status: 'ready'; survey: DiffSurvey }
    | { status: 'error'; message: string };

/** Divide by zoom to keep the outline width constant on screen. */
const MARK_BORDER = 1.5;

function marksOver(marks: MarkState, frame: Bounds | null, image: Bounds | null): MarkBox[] {
    if (marks.status !== 'ready' || !frame || !image) return [];
    return markBoxes(marks.survey.regions, frame, image);
}

/** Ctrl, cmd and alt all zoom; a bare wheel keeps flipping. */
const zooming = (event: WheelEvent): boolean => event.ctrlKey || event.metaKey || event.altKey;

interface CompareFrameProps {
    children: ReactNode;
    /** Shared transform synchronizes both frames. */
    zoom: Zoom;
    setZoom: Dispatch<SetStateAction<Zoom>>;
    label: string;
    active: boolean;
    /** Hide without unmounting while the replacement pair loads. */
    blank: boolean;
    marks: MarkState;
    /** Source image dimensions used for region coordinates. */
    markBounds: Bounds | null;

    onSide(side: 'a' | 'b'): void;

    onMenu(event: ReactMouseEvent): void;
}

function CompareFrame(
    {children, zoom, setZoom, label, active, blank, marks, markBounds, onSide, onMenu}: CompareFrameProps,
) {
    const frameRef = useRef<HTMLDivElement>(null);
    const panFrom = useRef<{ x: number; y: number } | null>(null);
    // Region placement requires the rendered frame dimensions.
    const [frameSize, setFrameSize] = useState<Bounds | null>(null);

    useEffect(() => {
        const node = frameRef.current;
        if (!node) return;

        const measure = () => setFrameSize({width: node.clientWidth, height: node.clientHeight});
        const observer = new ResizeObserver(measure);
        observer.observe(node);
        measure();

        return () => observer.disconnect();
    }, []);

    useEffect(() => {
        const node = frameRef.current;
        if (!node) return;

        // React attaches its own wheel listener passively, so preventDefault only works from a native one
        function onWheel(event: WheelEvent) {
            event.preventDefault();

            if (!zooming(event)) {
                onSide(event.deltaY > 0 ? 'b' : 'a');
                return;
            }

            const rect = node!.getBoundingClientRect();
            const pointerX = event.clientX - rect.left - rect.width / 2;
            const pointerY = event.clientY - rect.top - rect.height / 2;

            setZoom((current) =>
                zoomTo(
                    current,
                    scaledBy(current, event.deltaY > 0 ? 1 / ZOOM_STEP : ZOOM_STEP),
                    pointerX,
                    pointerY,
                    rect.width,
                    rect.height,
                ),
            );
        }

        node.addEventListener('wheel', onWheel, {passive: false});
        return () => node.removeEventListener('wheel', onWheel);
    }, [onSide, setZoom]);

    /** Zoom from the keyboard, anchored on the middle of the frame. */
    function zoomFromCentre(factor: number) {
        const rect = frameRef.current?.getBoundingClientRect();
        setZoom((current) => zoomTo(current, scaledBy(current, factor), 0, 0, rect?.width ?? 0, rect?.height ?? 0));
    }

    function startPan(event: ReactPointerEvent<HTMLDivElement>) {
        if (zoom.scale === 1 || event.button !== 0) return;
        panFrom.current = {x: event.clientX - zoom.x, y: event.clientY - zoom.y};
        event.currentTarget.setPointerCapture(event.pointerId);
    }

    function pan(event: ReactPointerEvent<HTMLDivElement>) {
        const from = panFrom.current;
        if (!from) return;

        const rect = event.currentTarget.getBoundingClientRect();
        const x = event.clientX - from.x;
        const y = event.clientY - from.y;
        setZoom((current) => clampPan({scale: current.scale, x, y}, rect.width, rect.height));
    }

    function endPan(event: ReactPointerEvent<HTMLDivElement>) {
        panFrom.current = null;
        if (event.currentTarget.hasPointerCapture(event.pointerId))
            event.currentTarget.releasePointerCapture(event.pointerId);
    }

    const boxes = blank ? [] : marksOver(marks, frameSize, markBounds);

    return (
        <div
            ref={frameRef}
            className={
                'file-relationship-flip-frame'
                + (zoom.scale > 1 ? ' is-zoomed' : '')
                + (active ? ' is-active' : '')
            }
            tabIndex={0}
            role="group"
            aria-label={label}
            onKeyDown={(event) => {
                if (event.key === 'ArrowLeft' || event.key === 'ArrowUp') onSide('a');
                if (event.key === 'ArrowRight' || event.key === 'ArrowDown') onSide('b');
                if (event.key === '+' || event.key === '=') zoomFromCentre(ZOOM_STEP);
                if (event.key === '-') zoomFromCentre(1 / ZOOM_STEP);
                if (event.key === '0') setZoom(NO_ZOOM);
            }}
            onPointerDown={startPan}
            onPointerMove={pan}
            onPointerUp={endPan}
            onPointerCancel={endPan}
            onDoubleClick={() => setZoom(NO_ZOOM)}
            onContextMenu={onMenu}
        >
            <div
                className="file-relationship-flip-zoom"
                style={{transform: `translate(${zoom.x}px, ${zoom.y}px) scale(${zoom.scale})`}}
            >
                {children}
                {boxes.map((box, index) => (
                    <span
                        key={index}
                        className="file-relationship-flip-mark"
                        style={{
                            left: `${box.left}px`,
                            top: `${box.top}px`,
                            width: `${box.width}px`,
                            height: `${box.height}px`,
                            borderWidth: `${MARK_BORDER / zoom.scale}px`,
                        }}
                    />
                ))}
            </div>
            {blank && <span className="file-relationship-flip-wait muted">Loading…</span>}
            {!blank && zoom.scale > 1 && (
                <span className="file-relationship-flip-level">{zoom.scale}x</span>
            )}
        </div>
    );
}

type CompareLayout = 'flip' | 'side-by-side';

interface PairCompareProps extends CompareProps {
    layout: CompareLayout;
    side: 'a' | 'b';
    diffKey: boolean;
    blank: boolean;
    marks: MarkState;
    aMetadata?: RecordMetadata;
    bMetadata?: RecordMetadata;
    /** How B already relates to A, when it does. */
    bDetail?: string;
    /** Verdict is relative to A, the preferred side. */
    verdict: QualityVerdict;
    pixelDuplicate: boolean;
    /** Blow the smaller copy up to the larger one's size instead of showing the size gap. */
    matchScale: boolean;

    onSide(side: 'a' | 'b'): void;

    onMatchScale(matched: boolean): void;

    onMenu(event: ReactMouseEvent, id: RecordId): void;
}

/**
 * Keep both images mounted to avoid decoding during swaps. Both layouts share zoom and scale
 * matching.
 */
function PairCompare(
    {
        host, a, b, aBounds, bBounds, layout, side, onSide, aMetadata, bMetadata, bDetail, verdict, pixelDuplicate,
        matchScale, diffKey, blank, marks, onMatchScale, onMenu,
    }: PairCompareProps,
) {
    const frame = frameFor(aBounds, bBounds);
    const split = layout === 'side-by-side';
    const [zoom, setZoom] = useState<Zoom>(NO_ZOOM);

    useEffect(() => setZoom(NO_ZOOM), [a, b]);

    const keyHelp =
        'Hold ctrl while scrolling to zoom both, plus and minus to zoom from the keyboard, '
        + '0 or a double click to reset. On the keypad, 4 and 6 pick a side, 7 and 9 call that '
        + 'side better, 5 pairs them as alternatives, 2 marks them unrelated, 3 switches layout '
        + 'and 1 shows the difference.';

    function image(which: 'a' | 'b') {
        const id = which === 'a' ? a : b;
        const bounds = which === 'a' ? aBounds : bBounds;
        const shown = !blank && (split || side === which);
        return (
            <img
                className={`file-relationship-flip-image${shown ? ' is-shown' : ''}`}
                style={matchScale ? MATCHED_SHARE : shareOf(frame, bounds)}
                src={host.records.fileUrl(id)}
                alt=""
                draggable={false}
            />
        );
    }

    return (
        <div className="file-relationship-flip">
            <div className="file-relationship-flip-stage">
                <CompareInfo
                    which="a"
                    id={a}
                    metadata={aMetadata}
                    other={bMetadata}
                    pixelDuplicate={pixelDuplicate}
                    active={side === 'a'}
                    advantages={advantagesOf(verdict, 'a')}
                    best={verdict.score > 0}
                    tiebreak={verdict.tiebreak}
                />
                <div className={`file-relationship-flip-frames${split ? ' is-split' : ''}`}>
                    {split ? (
                        (['a', 'b'] as const).map((which) => (
                            <CompareFrame
                                key={which}
                                zoom={zoom}
                                setZoom={setZoom}
                                label={
                                    `Record ${which === 'a' ? a : b}, side ${which.toUpperCase()}. ${keyHelp}`
                                }
                                active={side === which}
                                blank={blank}
                                marks={marks}
                                markBounds={which === 'a' ? aBounds : bBounds}
                                onSide={onSide}
                                onMenu={(event) => onMenu(event, which === 'a' ? a : b)}
                            >
                                {image(which)}
                            </CompareFrame>
                        ))
                    ) : (
                        <CompareFrame
                            zoom={zoom}
                            setZoom={setZoom}
                            label={`Flip between record ${a} and record ${b}. Scroll, or use the arrow keys. ${keyHelp}`}
                            active={false}
                            blank={blank}
                            marks={marks}
                            markBounds={aBounds}
                            onSide={onSide}
                            onMenu={(event) => onMenu(event, side === 'a' ? a : b)}
                        >
                            {image('a')}
                            {image('b')}
                        </CompareFrame>
                    )}
                </div>
                <CompareInfo
                    which="b"
                    id={b}
                    metadata={bMetadata}
                    other={aMetadata}
                    detail={bDetail}
                    pixelDuplicate={pixelDuplicate}
                    active={side === 'b'}
                    advantages={advantagesOf(verdict, 'b')}
                />
            </div>
            <div className="file-relationship-flip-bar">
                {(['a', 'b'] as const).map((which) => {
                    const id = which === 'a' ? a : b;
                    const bounds = which === 'a' ? aBounds : bBounds;
                    return (
                        <button
                            key={which}
                            type="button"
                            className={`file-relationship-flip-tab${side === which ? ' is-shown' : ''}`}
                            onClick={() => onSide(which)}
                            aria-pressed={side === which}
                        >
                            {which.toUpperCase()} · #{id}
                            {bounds && <span className="muted"> {bounds.width} × {bounds.height}</span>}
                        </button>
                    );
                })}
                <label className="file-relationship-flip-match">
                    <input
                        type="checkbox"
                        checked={matchScale}
                        onChange={(event) => onMatchScale(event.target.checked)}
                    />
                    <span>Match scale</span>
                </label>
                {marks.status === 'scanning' && (
                    <span className="file-relationship-flip-marking muted">Reading every pixel…</span>
                )}
                {marks.status === 'error' && (
                    <span className="file-relationship-flip-marking error">{marks.message}</span>
                )}
                {marks.status === 'ready' && (
                    <span className="file-relationship-flip-marking muted">
                        {marks.survey.regions.length === 0
                            ? 'Nothing visibly different'
                            : `${marks.survey.regions.length} boxed${marks.survey.dropped > 0 ? ` (+${marks.survey.dropped})` : ''}`}
                    </span>
                )}
            </div>
            <p className="file-relationship-flip-keys muted">
                Keypad: 4/6 side · 7/9 better · 5 alternatives · 2 unrelated · 3 layout
                {diffKey && ' · 1 difference'}
            </p>
        </div>
    );
}

function loadImage(source: string): Promise<HTMLImageElement> {
    return new Promise((resolve, reject) => {
        const image = new Image();
        image.onload = () => resolve(image);
        image.onerror = () => reject(new Error('The file could not be loaded as an image'));
        image.src = source;
    });
}

type PixelState =
    | { status: 'unknown' }
    | { status: 'checking' }
    | { status: 'identical' }
    | { status: 'different' }
    | { status: 'mismatched' }
    | { status: 'error'; message: string };

/**
 * Compares the two files pixel for pixel, but only once their stored dimensions agree: a pair of
 * different sizes cannot match, and saying so costs no decode at all.
 */
function usePixelComparison(
    host: PanelProps['host'],
    a: RecordId | null,
    b: RecordId | null,
    aBounds: Bounds | null,
    bBounds: Bounds | null,
): PixelState {
    const [state, setState] = useState<PixelState>({status: 'unknown'});
    const pinned = a !== null && b !== null;
    // boundsOf builds a fresh object every render, so the effect turns them into plain booleans first.
    const measured = aBounds !== null && bBounds !== null;
    const sameSize = sameResolution(aBounds, bBounds);

    useEffect(() => {
        if (!pinned || !sameSize) {
            setState(pinned && measured ? {status: 'mismatched'} : {status: 'unknown'});
            return;
        }

        let cancelled = false;
        setState({status: 'checking'});

        Promise.all([loadImage(host.records.fileUrl(a!)), loadImage(host.records.fileUrl(b!))])
            .then((images) => pixelsIdentical(images[0]!, images[1]!, () => cancelled))
            .then((identical) => {
                if (!cancelled) setState({status: identical ? 'identical' : 'different'});
            })
            .catch((error: unknown) => {
                if (!cancelled) setState({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
        };
    }, [host, a, b, pinned, measured, sameSize]);

    return state;
}

type DiffState =
    | { status: 'loading'; progress: number }
    | { status: 'ready'; survey: DiffSurvey }
    | { status: 'error'; message: string };

function percent(share: number): string {
    return `${(share * 100).toFixed(share > 0.9999 ? 4 : 2)}%`;
}

function verdictOf(survey: DiffSurvey): string {
    const verdict = classifyDifference(survey);
    switch (verdict.kind) {
        case 'identical':
            return 'Every pixel matches, alpha included.';
        case 'noise':
            return `${verdict.changed.toLocaleString()} pixels differ, none of them by more than `
                + `${NOTABLE_DELTA} of 255: the same image re-encoded.`;
        case 'near-identical':
            return `${percent(verdict.share)} of pixels match, with ${verdict.regions} `
                + `${verdict.regions === 1 ? 'region' : 'regions'} changed.`;
        case 'different':
            return `${percent(verdict.share)} of pixels match, spread across the frame rather than `
                + 'gathered into a few edits.';
    }
}

function regionLabel(survey: DiffSurvey, index: number): string {
    const region = survey.regions[index]!;
    return `${region.width} x ${region.height} at ${region.x}, ${region.y} · `
        + `${region.pixels.toLocaleString()} px`;
}

/** Display an overview and 1:1 crops ordered by notable-pixel count. */
function DiffCompare({host, a, b}: CompareProps) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const images = useRef<[HTMLImageElement, HTMLImageElement] | null>(null);
    const [state, setState] = useState<DiffState>({status: 'loading', progress: 0});
    /** Null selects the overview; an index selects a region at 1:1. */
    const [region, setRegion] = useState<number | null>(null);

    useEffect(() => {
        let cancelled = false;
        images.current = null;
        setRegion(null);
        setState({status: 'loading', progress: 0});

        Promise.all([loadImage(host.records.fileUrl(a)), loadImage(host.records.fileUrl(b))])
            .then(async ([imageA, imageB]) => {
                images.current = [imageA, imageB];
                const survey = await surveyDifference(
                    imageA,
                    imageB,
                    () => cancelled,
                    (done) => {
                        if (!cancelled) setState({status: 'loading', progress: done});
                    },
                );
                if (cancelled) return;
                setState({status: 'ready', survey});
                setRegion(survey.regions.length > 0 ? 0 : null);
            })
            .catch((error: unknown) => {
                if (!cancelled) setState({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
        };
    }, [host, a, b]);

    useEffect(() => {
        const canvas = canvasRef.current;
        const pair = images.current;
        if (state.status !== 'ready' || !canvas || !pair) return;

        const chosen = region === null ? undefined : state.survey.regions[region];

        try {
            if (chosen === undefined) paintOverview(canvas, state.survey, null);
            else paintRegion(canvas, pair[0], pair[1], regionWindow(chosen, state.survey.width, state.survey.height));
        } catch (error: unknown) {
            setState({status: 'error', message: message(error)});
        }
    }, [state, region]);

    const survey = state.status === 'ready' ? state.survey : null;
    const found = survey?.regions.length ?? 0;

    function pickRegion(event: ReactMouseEvent<HTMLCanvasElement>) {
        if (survey === null || region !== null || found === 0) return;
        const rect = event.currentTarget.getBoundingClientRect();
        setRegion(regionAt(
            survey,
            ((event.clientX - rect.left) / rect.width) * survey.width,
            ((event.clientY - rect.top) / rect.height) * survey.height,
        ));
    }

    function step(by: number) {
        if (found === 0) return;
        setRegion((current) => ((current ?? 0) + by + found) % found);
    }

    return (
        <div className="file-relationship-diff">
            <div className="file-relationship-diff-bar">
                <button
                    type="button"
                    className="file-relationship-action"
                    disabled={survey === null || region === null}
                    title="Show the whole image again"
                    onClick={() => setRegion(null)}
                >
                    Whole image
                </button>
                <button
                    type="button"
                    className="file-relationship-action"
                    disabled={found === 0}
                    title="Previous region, largest first"
                    onClick={() => step(-1)}
                >
                    Previous
                </button>
                <span className="file-relationship-diff-where">
                    {survey === null
                        ? 'Comparing…'
                        : found === 0
                            ? 'No region to zoom to'
                            : region === null
                                ? `${found} ${found === 1 ? 'region' : 'regions'}, largest first`
                                : `Region ${region + 1} of ${found}${survey.dropped > 0 ? ` (+${survey.dropped})` : ''} · ${regionLabel(survey, region)}`}
                </span>
                <button
                    type="button"
                    className="file-relationship-action"
                    disabled={found === 0}
                    title="Next region, largest first"
                    onClick={() => step(1)}
                >
                    Next
                </button>
            </div>
            <div className="file-relationship-diff-frame">
                <canvas
                    ref={canvasRef}
                    className={`${state.status === 'ready' ? 'is-ready' : ''}${region === null ? ' is-map' : ' is-zoomed'}`}
                    onClick={pickRegion}
                />
                {state.status === 'loading' && (
                    <p className="file-relationship-none muted">
                        Comparing every pixel… {Math.round(state.progress * 100)}%
                    </p>
                )}
                {state.status === 'error' && <p className="file-relationship-none error">{state.message}</p>}
            </div>
            <p className="file-relationship-diff-note muted">
                {survey === null ? (
                    'Read at full resolution: nothing is scaled down before it is compared.'
                ) : (
                    <>
                        {verdictOf(survey)} Compared at {survey.width} x {survey.height}
                        {region === null
                            ? '; the map takes the largest difference in each block, so one changed pixel still shows. Click it to zoom.'
                            : ', and this region is drawn one output pixel per real pixel.'}
                        {' '}Keypad 3, 4 or 6 goes back to the images.
                    </>
                )}
            </p>
        </div>
    );
}

function EmptyLane({children}: { children: string }) {
    return <p className="file-relationship-none muted">{children}</p>;
}

function FileRelationshipsPanel({host}: PanelProps) {
    const [focused, setFocused] = useState<RecordId | null>(() => lastRelationshipsFocus());
    const [state, setState] = useState<State>({status: 'idle'});
    const [distance, setDistance] = useState<number>(DEFAULT_DISTANCE);
    const [filterKnown, setFilterKnown] = useState(true);
    const [filterUnrelated, setFilterUnrelated] = useState(true);
    const [similar, setSimilar] = useState<SimilarState>({status: 'idle'});
    const [compared, setCompared] = useState<RecordId | null>(null);
    const [compareView, setCompareView] = useState<CompareView>('flip');
    const [flipSide, setFlipSide] = useState<'a' | 'b'>('a');
    const [matchScale, setMatchScale] = useState(true);
    const [pending, setPending] = useState(false);
    const [reloadToken, setReloadToken] = useState(0);
    const [history, setHistory] = useState<ComparedPair[]>([]);
    const [loadedPair, setLoadedPair] = useState<ComparedPair | null>(null);
    const [markDifferences, setMarkDifferences] = useState(false);
    const [marks, setMarks] = useState<MarkState>({status: 'off'});
    const rootRef = useRef<HTMLDivElement>(null);
    const decoded = useRef<Map<RecordId, HTMLImageElement>>(new Map());
    const facts = useRef<Map<RecordId, RecordMetadata>>(new Map());
    const [queue] = useState(() => createLookaheadQueue<ComparedPair>());
    const shown = useRef<ComparedPair | null>(null);

    useEffect(
        () =>
            host.bus.on(RELATIONSHIPS_FOCUS_TOPIC, (payload) => {
                if (typeof payload !== 'number') return;
                setFocused(payload);
                setCompared(null);
            }),
        [host],
    );

    useEffect(() => {
        if (focused === null) {
            setState({status: 'idle'});
            return;
        }

        const controller = new AbortController();
        let cancelled = false;
        setState({status: 'loading'});

        host.records
            .relationships(focused, controller.signal)
            .then(async (relationships) => {
                const ids = [
                    ...new Set([focused, ...relationships.inferior, ...relationships.superior, ...relationships.alternatives]),
                ];
                const response = await host.records.getMetadata(ids);
                return {
                    relationships,
                    metadata: new Map(response.records.map((record) => [record.record_id, record])),
                };
            })
            .then((result) => {
                if (!cancelled) setState({status: 'ok', ...result});
            })
            .catch((error: unknown) => {
                if (cancelled || (error instanceof DOMException && error.name === 'AbortError')) return;
                setState({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
            controller.abort();
        };
    }, [focused, host, reloadToken]);

    useEffect(() => {
        if (focused === null) {
            setSimilar({status: 'idle'});
            return;
        }

        const controller = new AbortController();
        let cancelled = false;
        setSimilar({status: 'loading'});

        host.records
            .similar(
                focused,
                {distance, includeRelated: !filterKnown, includeUnrelated: !filterUnrelated},
                controller.signal,
            )
            .then(async (response) => {
                const found = await host.records.getMetadata(response.results.map((record) => record.record_id));
                return {
                    records: response.results,
                    truncated: response.truncated,
                    metadata: new Map(found.records.map((record) => [record.record_id, record])),
                };
            })
            .then((result) => {
                if (!cancelled) setSimilar({status: 'ok', ...result});
            })
            .catch((error: unknown) => {
                if (cancelled || (error instanceof DOMException && error.name === 'AbortError')) return;
                if (isNotFound(error)) {
                    setSimilar({status: 'unhashed'});
                    return;
                }
                setSimilar({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
            controller.abort();
        };
    }, [focused, distance, filterKnown, filterUnrelated, host, reloadToken]);

    const {openRecordMenu, recordMenu} = useRecordMenu(host);

    /** Pinning a neighbour never moves the app-wide selection, so the thumbnail grid stays put. */
    function compareWith(id: RecordId) {
        setCompared((current) => (current === id ? null : id));
    }

    /** Move focus to the panel before disabling controls to preserve keypad handling. */
    function holdFocus() {
        rootRef.current?.focus();
    }

    /**
     * The action returns its outcome because clearing resolves the relationship at execution
     * time. Hide images and disable rulings until the next pair is ready.
     */
    function runAction(work: () => Promise<string>, advance: boolean = false) {
        holdFocus();
        setPending(true);
        work()
            .then((done) => {
                queue.invalidate();
                host.ui.toast(done, {kind: 'success'});
                setReloadToken((token) => token + 1);
                return advance ? advanceToNext() : undefined;
            })
            .catch((error: unknown) => {
                host.ui.toast(message(error), {kind: 'error'});
            })
            .finally(() => setPending(false));
    }

    function show(pair: ComparedPair) {
        if (focused !== null && compared !== null)
            setHistory((past) => [...past, {a: focused, b: compared}].slice(-HISTORY_LIMIT));
        setFocused(pair.a);
        setCompared(pair.b);
        setFlipSide('a');
    }

    /** Skip the displayed pair explicitly because failed rulings leave it in the queue. */
    function takeQueued(): ComparedPair | undefined {
        for (let next = queue.take(); next !== undefined; next = queue.take()) {
            if (!isShown(next)) return next;
        }
        return undefined;
    }

    /**
     * Compare IDs without ordering: queue pairs use ascending IDs, while similar results may be
     * reversed.
     */
    function isShown(pair: ComparedPair): boolean {
        const current = shown.current;
        if (current === null) return false;
        return (pair.a === current.a && pair.b === current.b) || (pair.a === current.b && pair.b === current.a);
    }

    function advanceToNext(): Promise<void> {
        const held = takeQueued();
        if (held !== undefined) {
            show(held);
            return Promise.resolve();
        }

        return lookAhead().then(() => {
            const next = takeQueued();
            if (next === undefined) {
                host.ui.toast(`Nothing left to rule on within ${bitsLabel(distance)}`, {kind: 'info'});
                return;
            }
            show(next);
        });
    }

    function ruleBetter(better: RecordId, worse: RecordId) {
        runAction(async () => {
            await host.records.setBetterDuplicate(worse, better);
            return `Record #${better} is now superior to #${worse}`;
        }, true);
    }

    function ruleAlternatives(a: RecordId, b: RecordId) {
        runAction(async () => {
            await host.records.addAlternatives([a, b]);
            return `Records #${a} and #${b} are now alternatives`;
        }, true);
    }

    function ruleUnrelated(a: RecordId, b: RecordId) {
        runAction(async () => {
            await host.records.setUnrelated(a, b);
            return `#${a} and #${b} are now marked unrelated`;
        }, true);
    }

    function loadNextUndecided() {
        holdFocus();
        setPending(true);
        advanceToNext()
            .catch((error: unknown) => {
                host.ui.toast(message(error), {kind: 'error'});
            })
            .finally(() => setPending(false));
    }

    function goBack() {
        const previous = history[history.length - 1];
        if (previous === undefined) return;
        holdFocus();
        setHistory((past) => past.slice(0, -1));
        setFocused(previous.a);
        setCompared(previous.b);
        setFlipSide('a');
    }

    function metadataFor(id: RecordId): RecordMetadata | undefined {
        const known = state.status === 'ok' ? state.metadata.get(id) : undefined;
        const lane = known ?? (similar.status === 'ok' ? similar.metadata.get(id) : undefined);
        return lane ?? facts.current.get(id);
    }

    /** Fetch missing queue metadata before computing quality order. */
    function readMetadata(ids: RecordId[]): Promise<void> {
        const missing = ids.filter((id) => !facts.current.has(id));
        if (missing.length === 0) return Promise.resolve();

        return host.records
            .getMetadata(missing)
            .then((response) => {
                for (const record of response.records) facts.current.set(record.record_id, record);
            })
            .catch(() => undefined);
    }

    const focusedMetadata = focused === null ? undefined : metadataFor(focused);
    const comparedMetadata = compared === null ? undefined : metadataFor(compared);

    /**
     * Order comparisons with the preferred copy as A. Keep similarity results anchored to the
     * selected record.
     */
    const swapped = compareQuality(focusedMetadata, comparedMetadata).score < 0;
    const sides: Sides | null = focused === null || compared === null
        ? null
        : swapped
            ? {a: compared, b: focused}
            : {a: focused, b: compared};
    const aMetadata = swapped ? comparedMetadata : focusedMetadata;
    const bMetadata = swapped ? focusedMetadata : comparedMetadata;
    const verdict = compareQuality(aMetadata, bMetadata);

    const aBounds = boundsOf(aMetadata);
    const bBounds = boundsOf(bMetadata);
    const pixels = usePixelComparison(host, sides?.a ?? null, sides?.b ?? null, aBounds, bBounds);
    const comparable = sameResolution(aBounds, bBounds);

    const pairShown = compared === null
        || (loadedPair !== null && loadedPair.a === focused && loadedPair.b === compared);
    /**
     * Use one busy flag to hide images and disable rulings until decoding completes. Keep
     * images mounted for recovery after failed actions.
     */
    const busy = pending || !pairShown;

    /**
     * Retain decoded HTMLImageElements in the cache. Resolve decode failures so they cannot
     * block queue advancement.
     */
    function decode(id: RecordId): Promise<HTMLImageElement | undefined> {
        const held = decoded.current.get(id);
        if (held !== undefined) {
            // Refresh insertion order to evict older pairs first.
            decoded.current.delete(id);
            decoded.current.set(id, held);
            return Promise.resolve(held);
        }

        return loadImage(host.records.fileUrl(id)).then(
            (image) => {
                decoded.current.set(id, image);
                for (const oldest of decoded.current.keys()) {
                    if (decoded.current.size <= CACHE_LIMIT) break;
                    decoded.current.delete(oldest);
                }
                return image;
            },
            () => undefined,
        );
    }

    /** Run the full-resolution region scan only when overlays are enabled. */
    useEffect(() => {
        if (!markDifferences || !comparable || focused === null || compared === null || !pairShown) {
            setMarks({status: 'off'});
            return;
        }

        let cancelled = false;
        setMarks({status: 'scanning'});

        Promise.all([decode(focused), decode(compared)])
            .then(async ([imageA, imageB]) => {
                if (imageA === undefined || imageB === undefined)
                    throw new Error('One of the two could not be read as an image');
                const survey = await surveyDifference(imageA, imageB, () => cancelled);
                if (!cancelled) setMarks({status: 'ready', survey});
            })
            .catch((error: unknown) => {
                if (!cancelled) setMarks({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [markDifferences, comparable, focused, compared, pairShown, host]);

    /**
     * Wait for both metadata and decoded images before revealing the pair, so labels and
     * quality order match the displayed files.
     */
    useEffect(() => {
        if (focused === null || compared === null) return;

        const pair = {a: focused, b: compared};
        let cancelled = false;

        void Promise.all([decode(pair.a), decode(pair.b), readMetadata([pair.a, pair.b])]).then(() => {
            if (!cancelled) setLoadedPair(pair);
        });

        return () => {
            cancelled = true;
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [focused, compared, host]);

    /**
     * Prefetch metadata and decode lookahead pairs. Share requests within a queue generation
     * across navigation; successful rulings invalidate them because inherited rulings may
     * remove other candidates.
     */
    function lookAhead(): Promise<void> {
        return queue.fill(async () => {
            const response = await host.records.nextUndecidedDuplicate({distance, limit: LOOKAHEAD_PAIRS + 1});
            const ahead = (response.pairs ?? [])
                .map((pair) => ({a: pair.record_id_a, b: pair.record_id_b}))
                .filter((pair) => !isShown(pair))
                .slice(0, LOOKAHEAD_PAIRS);
            await readMetadata(ahead.flatMap((pair) => [pair.a, pair.b]));

            for (const pair of ahead) {
                await decode(pair.a);
                await decode(pair.b);
            }
            return ahead;
        });
    }

    /** A changed query or host must not reuse candidates from an earlier request. */
    useEffect(() => {
        queue.invalidate();
        return () => queue.invalidate();
    }, [distance, host, queue]);

    /** Read the current pair when lookahead resolves to exclude it after intervening navigation. */
    useEffect(() => {
        shown.current = focused === null || compared === null ? null : {a: focused, b: compared};
    }, [focused, compared]);

    /** Refill after each ruling because inherited relationships may invalidate queued pairs. */
    useEffect(() => {
        if (focused === null || compared === null || !pairShown) return;
        void lookAhead().catch(() => undefined);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [focused, compared, pairShown, distance, reloadToken, host]);

    /**
     * Scope keypad handling to the focused panel. Keys 3, 4, and 6 return from the difference
     * view to comparison.
     */
    useEffect(() => {
        if (sides === null || busy) return;

        const a: RecordId = sides.a;
        const b: RecordId = sides.b;

        function onKey(event: KeyboardEvent) {
            if (event.ctrlKey || event.altKey || event.metaKey) return;

            const root = rootRef.current;
            const target = event.target;
            if (!root || !(target instanceof HTMLElement) || !root.contains(target)) return;
            if (TYPED_INTO.has(target.tagName) || target.isContentEditable) return;

            const action = KEYPAD[event.code];
            if (action === undefined) return;
            if (action === 'diff' && !comparable) return;

            // Capture before the frame handles Num Lock-off arrow keys.
            event.preventDefault();
            event.stopPropagation();

            switch (action) {
                case 'a':
                case 'b':
                    setFlipSide(action);
                    if (compareView === 'diff') {
                        setCompareView('flip');
                        // Restore panel focus before unmounting the difference canvas.
                        root.focus();
                    }
                    return;
                case 'a-better':
                    ruleBetter(a, b);
                    return;
                case 'b-better':
                    ruleBetter(b, a);
                    return;
                case 'alternative':
                    ruleAlternatives(a, b);
                    return;
                case 'unrelated':
                    ruleUnrelated(a, b);
                    return;
                case 'diff':
                    setCompareView('diff');
                    root.focus();
                    return;
                case 'layout':
                    setCompareView(compareView === 'flip' ? 'side-by-side' : 'flip');
                    // Restore panel focus before unmounting the difference canvas.
                    root.focus();
                    return;
            }
        }

        window.addEventListener('keydown', onKey, true);
        return () => window.removeEventListener('keydown', onKey, true);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [compareView, sides?.a, sides?.b, busy, comparable, host, distance]);

    if (focused === null) {
        return (
            <div className="panel-body file-relationships-empty">
                <p className="muted">
                    Right click a record in the thumbnail grid and choose "Send to Relationships View" to find
                    files that look like it.
                </p>
                <button
                    type="button"
                    className="file-relationship-action"
                    disabled={busy}
                    title={`Jump to the closest pair within ${bitsLabel(distance)} that nobody has ruled on`}
                    onClick={loadNextUndecided}
                >
                    Next Unresolved
                </button>
            </div>
        );
    }

    const relationships = state.status === 'ok' ? state.relationships : null;
    const superiorIds = new Set(relationships?.superior ?? []);
    const inferiorIds = new Set(relationships?.inferior ?? []);
    const alternativeIds = new Set(relationships?.alternatives ?? []);
    const unrelatedIds = new Set(
        similar.status === 'ok' ? similar.records.filter((record) => record.unrelated).map((r) => r.record_id) : [],
    );

    function relationTo(id: RecordId): Relation | undefined {
        if (superiorIds.has(id)) return 'superior';
        if (inferiorIds.has(id)) return 'inferior';
        if (alternativeIds.has(id)) return 'alternative';
        if (unrelatedIds.has(id)) return 'unrelated';
        return undefined;
    }

    /** `alternative (4 bits)` when both halves are known, either half alone otherwise. */
    function describe(id: RecordId, bits?: number): string | undefined {
        const relation = relationTo(id);
        const distanceText = bits === undefined ? undefined : bitsLabel(bits);
        if (relation && distanceText) return `${relation} (${distanceText})`;
        return relation ?? distanceText;
    }

    const onCardMenu = (event: ReactMouseEvent, id: RecordId) => openRecordMenu(event, [id]);
    const comparing = sides !== null;
    const comparedRelation = compared === null ? undefined : relationTo(compared);
    const sideBRelation = swapped ? flipRelation(comparedRelation) : comparedRelation;
    const previousPair = history[history.length - 1];

    return (
        <div
            ref={rootRef}
            tabIndex={-1}
            className={`file-relationships-panel${comparing ? ' is-comparing' : ''}`}
        >
            <section className="file-relationship-primary" aria-labelledby={`selected-${host.instanceId}`}>
                <div className="file-relationship-group-head">
                    <h2 id={`selected-${host.instanceId}`}>{comparing ? 'Comparison' : 'Primary'}</h2>
                    <div className="file-relationship-group-controls">
                        {comparing && (
                            <label className="file-relationship-distance-picker">
                                <span>View</span>
                                <select
                                    value={compareView}
                                    onChange={(event) => setCompareView(event.target.value as CompareView)}
                                    aria-label="Comparison view"
                                >
                                    {COMPARE_VIEWS.map((option) => (
                                        <option
                                            key={option.value}
                                            value={option.value}
                                            disabled={option.value === 'diff' && !comparable}
                                        >
                                            {option.label}
                                            {option.value === 'diff' && !comparable ? ' (same resolution only)' : ''}
                                        </option>
                                    ))}
                                </select>
                            </label>
                        )}
                        {comparing && compareView !== 'diff' && (
                            <label
                                className="file-relationship-toggle"
                                title={
                                    comparable
                                        ? 'Box every area where the two differ, read at full resolution'
                                        : 'The two are different resolutions, so their pixels do not line up'
                                }
                            >
                                <input
                                    type="checkbox"
                                    checked={markDifferences}
                                    disabled={!comparable}
                                    onChange={(event) => setMarkDifferences(event.target.checked)}
                                />
                                <span>Mark differences</span>
                            </label>
                        )}
                        <button
                            type="button"
                            className="file-relationship-action"
                            disabled={busy || previousPair === undefined}
                            title={
                                previousPair === undefined
                                    ? 'No earlier pair to go back to'
                                    : `Go back to records ${previousPair.a} and ${previousPair.b}`
                            }
                            onClick={goBack}
                        >
                            Back
                        </button>
                        <button
                            type="button"
                            className="file-relationship-action"
                            disabled={busy}
                            title={`Jump to the closest pair within ${bitsLabel(distance)} that nobody has ruled on`}
                            onClick={loadNextUndecided}
                        >
                            Next Unresolved
                        </button>
                        {sides !== null && (
                            <>
                                <button
                                    type="button"
                                    className="file-relationship-action"
                                    disabled={busy}
                                    title={`Reparse records ${sides.a} and ${sides.b} and reload both sides`}
                                    onClick={() =>
                                        runAction(async () => {
                                            await host.records.regenerateMetadata([sides.a, sides.b]);
                                            facts.current.delete(sides.a);
                                            facts.current.delete(sides.b);
                                            return `Regenerated metadata for #${sides.a} and #${sides.b}`;
                                        })
                                    }
                                >
                                    Regenerate Metadata
                                </button>
                                <button
                                    type="button"
                                    className="file-relationship-action"
                                    disabled={aMetadata?.size === undefined || bMetadata?.size === undefined}
                                    title={`Download both record ${sides.a} and record ${sides.b}`}
                                    onClick={() => {
                                        downloadRecord(host, sides.a);
                                        downloadRecord(host, sides.b);
                                    }}
                                >
                                    Download Both
                                </button>
                                <button type="button" className="file-relationship-close"
                                        onClick={() => setCompared(null)}>
                                    Close
                                </button>
                            </>
                        )}
                    </div>
                </div>
                {sides === null ? (
                    <FileCard
                        host={host}
                        id={focused}
                        metadata={metadataFor(focused)}
                        selected
                        onSelect={() => {
                        }}
                        onMenu={onCardMenu}
                    />
                ) : (
                    <>
                        {compareView !== 'diff' && (
                            <PairCompare
                                host={host}
                                layout={compareView}
                                a={sides.a}
                                b={sides.b}
                                aBounds={aBounds}
                                bBounds={bBounds}
                                side={flipSide}
                                onSide={setFlipSide}
                                matchScale={matchScale}
                                onMatchScale={setMatchScale}
                                aMetadata={aMetadata}
                                bMetadata={bMetadata}
                                bDetail={sideBRelation}
                                verdict={verdict}
                                pixelDuplicate={pixels.status === 'identical'}
                                diffKey={comparable}
                                blank={busy}
                                marks={marks}
                                onMenu={onCardMenu}
                            />
                        )}
                        {compareView === 'diff'
                            && (comparable ? (
                                <DiffCompare host={host} a={sides.a} b={sides.b} aBounds={aBounds} bBounds={bBounds}/>
                            ) : (
                                <p className="file-relationship-none muted">
                                    These two are different resolutions, so there is no pixel difference to show.
                                    Compare
                                    them side by side or by flipping instead.
                                </p>
                            ))}
                        <div className="file-relationship-actions">
                            <button
                                type="button"
                                className="file-relationship-action is-better"
                                disabled={busy || sideBRelation === 'inferior'}
                                title={`Mark record ${sides.a} as the superior copy of record ${sides.b} (keypad 7)`}
                                onClick={() => ruleBetter(sides.a, sides.b)}
                            >
                                A #{sides.a} Better
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-better"
                                disabled={busy || sideBRelation === 'superior'}
                                title={`Mark record ${sides.b} as the superior copy of record ${sides.a} (keypad 9)`}
                                onClick={() => ruleBetter(sides.b, sides.a)}
                            >
                                B #{sides.b} Better
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-primary"
                                disabled={busy || sideBRelation === 'alternative'}
                                title={`Pair records ${sides.a} and ${sides.b} as alternatives (keypad 5)`}
                                onClick={() => ruleAlternatives(sides.a, sides.b)}
                            >
                                Set Alternatives
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-quarter"
                                disabled={busy || sideBRelation === 'unrelated'}
                                title={`Mark records ${sides.a} and ${sides.b} as coincidental lookalikes (keypad 2)`}
                                onClick={() => ruleUnrelated(sides.a, sides.b)}
                            >
                                Set Unrelated
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-danger is-quarter"
                                disabled={busy}
                                title={`Drop the relationship between records ${sides.a} and ${sides.b}`}
                                onClick={() =>
                                    runAction(async () => {
                                        const cleared = await host.records.clearRelationship(sides.a, sides.b);
                                        if (cleared.duplicate_removed || cleared.alternative_removed)
                                            return `Cleared the relationship between #${sides.a} and #${sides.b}`;
                                        return `#${sides.a} and #${sides.b} have no direct relationship to clear`;
                                    })
                                }
                            >
                                Clear Relationship
                            </button>
                        </div>
                    </>
                )}
            </section>

            <aside className="file-relationship-groups">
                <section className="file-relationship-group" aria-labelledby={`similar-${host.instanceId}`}>
                    <div className="file-relationship-group-head">
                        <h2 id={`similar-${host.instanceId}`}>
                            Similar
                            {similar.status === 'ok' && similar.truncated && ' · truncated'}
                        </h2>
                        <div className="file-relationship-group-controls">
                            <label className="file-relationship-toggle">
                                <input
                                    type="checkbox"
                                    checked={filterKnown}
                                    onChange={(event) => setFilterKnown(event.target.checked)}
                                />
                                <span>Filter known</span>
                            </label>
                            <label className="file-relationship-toggle">
                                <input
                                    type="checkbox"
                                    checked={filterUnrelated}
                                    onChange={(event) => setFilterUnrelated(event.target.checked)}
                                />
                                <span>Filter unrelated</span>
                            </label>
                            <label className="file-relationship-distance-picker">
                                <span>Distance</span>
                                <select
                                    value={distance}
                                    onChange={(event) => setDistance(Number(event.target.value))}
                                    aria-label="Maximum perceptual hash distance"
                                >
                                    {DISTANCE_OPTIONS.map((option) => (
                                        <option key={option.value} value={option.value}>
                                            {option.label}
                                        </option>
                                    ))}
                                </select>
                            </label>
                        </div>
                    </div>
                    <div className="file-relationship-group-list">
                        {similar.status === 'loading' && <EmptyLane>Searching…</EmptyLane>}
                        {similar.status === 'unhashed' && <EmptyLane>This record has no perceptual hash</EmptyLane>}
                        {similar.status === 'error' &&
                            <p className="file-relationship-none error">{similar.message}</p>}
                        {similar.status === 'ok' &&
                            (similar.records.length === 0 ? (
                                <EmptyLane>Nothing within this distance</EmptyLane>
                            ) : (
                                similar.records.map((record) => (
                                    <FileCard
                                        key={record.record_id}
                                        host={host}
                                        id={record.record_id}
                                        metadata={similar.metadata.get(record.record_id)}
                                        detail={describe(record.record_id, record.distance)}
                                        relation={relationTo(record.record_id)}
                                        active={record.record_id === compared}
                                        thumbnail
                                        onSelect={compareWith}
                                        onMenu={onCardMenu}
                                    />
                                ))
                            ))}
                    </div>
                </section>
            </aside>
            {recordMenu}
        </div>
    );
}

export const fileRelationshipsPanel = {
    type: 'file-relationships',
    title: 'File Relationships',
    description:
        'Find perceptually similar files to the selected record, compare them side by side, and mark them as duplicates or alternatives.',
    component: FileRelationshipsPanel,
    configVersion: 1,
} as const;
