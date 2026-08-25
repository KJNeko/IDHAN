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
import type {MouseEvent as ReactMouseEvent, PointerEvent as ReactPointerEvent} from 'react';
import type {FileRelationships, RecordMetadata, SimilarRecord} from '../../api/types';
import type {PanelProps, RecordId} from '../../host/types';
import {EMBEDDED_BLOCKS, formatBytes, formatDuration} from './RecordInfoView';
import {useRecordMenu} from './recordActions';
import {lastRelationshipsFocus, RELATIONSHIPS_FOCUS_TOPIC} from './relationshipsFocus';

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

type CompareView = 'side-by-side' | 'flip' | 'diff';

const COMPARE_VIEWS = [
    {value: 'side-by-side', label: 'Side by side'},
    {value: 'flip', label: 'Flip'},
    {value: 'diff', label: 'Difference'},
] as const;

/** Longest edge the difference view resamples to before comparing pixels. */
const DIFF_MAX_EDGE = 1024;
/** Difference views are mostly near-black without it; 4 makes a re-encode visible. */
const DIFF_GAIN = 4;

/**
 * Rows of pixels read at a time when checking two files for an exact match. Both images are compared
 * a strip at a time so a pair of 8000px photographs costs a few MB of canvas rather than 500.
 */
const PIXEL_STRIP_ROWS = 128;

/** How a record already relates to the selected one, if at all. */
type Relation = 'superior' | 'inferior' | 'alternative' | 'unrelated';

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
            <img src={host.records.fileUrl(id)} alt="" draggable={false}/>
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
}

/** The side-by-side facts a duplicate call is actually made on, embedded blocks first. */
function CompareInfo({which, id, metadata, other, detail, pixelDuplicate, active}: CompareInfoProps) {
    const rows = infoRows(metadata, other);
    const embedded = embeddedBlocks(metadata);
    // Losing the only copy of an EXIF or GPS block is the one mistake this view exists to prevent.
    const onlySide = embedded.some((block) => !embeddedBlocks(other).includes(block));

    return (
        <div className={`file-relationship-side-info is-${which}${active ? ' is-shown' : ''}`}>
            <h3>
                {which.toUpperCase()} · #{id}
            </h3>
            {pixelDuplicate && <p className="file-relationship-side-match">Pixel-for-pixel duplicate</p>}
            {embedded.length > 0 && (
                <p
                    className={`file-relationship-side-embedded${onlySide ? ' is-only' : ''}`}
                    title={onlySide ? 'The other copy does not carry all of these' : undefined}
                >
                    Contains {embedded.join(', ')}
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
const ZOOM_MAX = 16;
/** Past this, interpolation smooths away the encoding detail the zoom is there to show. */
const ZOOM_PIXELATED = 2;

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

const scaledBy = (zoom: Zoom, factor: number): number =>
    Math.min(ZOOM_MAX, Math.max(1, zoom.scale * factor));

/** Re-anchors the pan so whatever sits under (x, y) stays there as the scale changes. */
function zoomTo(current: Zoom, scale: number, x: number, y: number, width: number, height: number): Zoom {
    if (scale <= 1) return NO_ZOOM;

    const ratio = scale / current.scale;

    return clampPan({scale, x: x - (x - current.x) * ratio, y: y - (y - current.y) * ratio}, width, height);
}

/** Ctrl, cmd and alt all zoom; a bare wheel keeps flipping. */
const zooming = (event: WheelEvent): boolean => event.ctrlKey || event.metaKey || event.altKey;

interface FlipCompareProps extends CompareProps {
    side: 'a' | 'b';
    aMetadata?: RecordMetadata;
    bMetadata?: RecordMetadata;
    /** How B already relates to A, when it does. */
    bDetail?: string;
    pixelDuplicate: boolean;

    onSide(side: 'a' | 'b'): void;

    onMenu(event: ReactMouseEvent, id: RecordId): void;
}

/**
 * Scrolling swaps which of the two is on top. Both stay mounted so the swap never waits on a decode,
 * which is the whole point of flipping rather than looking side by side.
 *
 * Scrolling with a modifier held zooms instead, and the zoom is shared: flipping at 8x stays on the
 * same region of the same magnification, which is how a re-encode gives itself away.
 */
function FlipCompare(
    {host, a, b, aBounds, bBounds, side, onSide, aMetadata, bMetadata, bDetail, pixelDuplicate, onMenu}:
    FlipCompareProps,
) {
    const frameRef = useRef<HTMLDivElement>(null);
    const frame = frameFor(aBounds, bBounds);
    // One transform drives both images, so flipping stays on the same region of the same magnification.
    const [zoom, setZoom] = useState<Zoom>(NO_ZOOM);
    const panFrom = useRef<{ x: number; y: number } | null>(null);

    useEffect(() => setZoom(NO_ZOOM), [a, b]);

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
    }, [onSide]);

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
                />
                <div
                    ref={frameRef}
                    className={`file-relationship-flip-frame${zoom.scale > 1 ? ' is-zoomed' : ''}`}
                    tabIndex={0}
                    role="group"
                    aria-label={
                        `Flip between record ${a} and record ${b}. Scroll, or use the arrow keys. `
                        + 'Hold ctrl while scrolling to zoom both, plus and minus to zoom from the keyboard, '
                        + '0 or a double click to reset.'
                    }
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
                    onContextMenu={(event) => onMenu(event, side === 'a' ? a : b)}
                >
                    <div
                        className={`file-relationship-flip-zoom${zoom.scale >= ZOOM_PIXELATED ? ' is-magnified' : ''}`}
                        style={{transform: `translate(${zoom.x}px, ${zoom.y}px) scale(${zoom.scale})`}}
                    >
                        <img
                            className={`file-relationship-flip-image${side === 'a' ? ' is-shown' : ''}`}
                            style={shareOf(frame, aBounds)}
                            src={host.records.fileUrl(a)}
                            alt=""
                            draggable={false}
                        />
                        <img
                            className={`file-relationship-flip-image${side === 'b' ? ' is-shown' : ''}`}
                            style={shareOf(frame, bBounds)}
                            src={host.records.fileUrl(b)}
                            alt=""
                            draggable={false}
                        />
                    </div>
                    {zoom.scale > 1 && (
                        <span className="file-relationship-flip-level">{zoom.scale.toFixed(1)}x</span>
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
            </div>
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

function drawDifference(canvas: HTMLCanvasElement, a: HTMLImageElement, b: HTMLImageElement): void {
    const wide = Math.max(a.naturalWidth, b.naturalWidth);
    const tall = Math.max(a.naturalHeight, b.naturalHeight);
    const scale = Math.min(1, DIFF_MAX_EDGE / Math.max(wide, tall));
    const width = Math.max(1, Math.round(wide * scale));
    const height = Math.max(1, Math.round(tall * scale));

    // both are stretched to fill the same grid: a resolution difference would otherwise offset every
    // pixel and swamp the content difference this view exists to show
    function sample(image: HTMLImageElement): ImageData {
        const offscreen = document.createElement('canvas');
        offscreen.width = width;
        offscreen.height = height;
        const context = offscreen.getContext('2d', {willReadFrequently: true});
        if (!context) throw new Error('This browser did not provide a 2D canvas');
        context.drawImage(image, 0, 0, width, height);
        return context.getImageData(0, 0, width, height);
    }

    const left = sample(a);
    const right = sample(b);
    const output = new ImageData(width, height);

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

    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext('2d');
    if (!context) throw new Error('This browser did not provide a 2D canvas');
    context.putImageData(output, 0, 0);
}

/** A canvas one strip tall, reused for every strip of one image. */
function stripContext(width: number): CanvasRenderingContext2D {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = PIXEL_STRIP_ROWS;
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

/**
 * Whether the two decode to the same pixels, alpha included: a re-encode that changed nothing but
 * the container. Runs a strip at a time, yielding between strips so a large pair does not freeze the
 * panel, and stops at the first byte that differs.
 */
async function pixelsIdentical(
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

    for (let top = 0; top < height; top += PIXEL_STRIP_ROWS) {
        if (cancelled()) return false;

        const rows = Math.min(PIXEL_STRIP_ROWS, height - top);
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

type DiffState = { status: 'loading' } | { status: 'ready' } | { status: 'error'; message: string };

/** Experimental: black where the two agree, bright where they do not. */
function DiffCompare({host, a, b}: CompareProps) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const [state, setState] = useState<DiffState>({status: 'loading'});

    useEffect(() => {
        let cancelled = false;
        setState({status: 'loading'});

        Promise.all([loadImage(host.records.fileUrl(a)), loadImage(host.records.fileUrl(b))])
            .then(([imageA, imageB]) => {
                if (cancelled) return;
                const canvas = canvasRef.current;
                if (!canvas) return;
                drawDifference(canvas, imageA, imageB);
                setState({status: 'ready'});
            })
            .catch((error: unknown) => {
                if (!cancelled) setState({status: 'error', message: message(error)});
            });

        return () => {
            cancelled = true;
        };
    }, [host, a, b]);

    return (
        <div className="file-relationship-diff">
            <div className="file-relationship-diff-frame">
                <canvas ref={canvasRef} className={state.status === 'ready' ? 'is-ready' : ''}/>
                {state.status === 'loading' && <p className="file-relationship-none muted">Comparing…</p>}
                {state.status === 'error' && <p className="file-relationship-none error">{state.message}</p>}
            </div>
            <p className="file-relationship-diff-note muted">
                Both resampled to {DIFF_MAX_EDGE}px at most, so this shows where the two differ rather than
                how sharp either one is.
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
    const [compareView, setCompareView] = useState<CompareView>('side-by-side');
    const [flipSide, setFlipSide] = useState<'a' | 'b'>('a');
    const [pending, setPending] = useState(false);
    const [reloadToken, setReloadToken] = useState(0);

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

    /** The work reports its own outcome, since clearing cannot know what it removed until it runs. */
    function runAction(work: () => Promise<string>) {
        setPending(true);
        work()
            .then((done) => {
                host.ui.toast(done, {kind: 'success'});
                setReloadToken((token) => token + 1);
            })
            .catch((error: unknown) => {
                host.ui.toast(message(error), {kind: 'error'});
            })
            .finally(() => setPending(false));
    }

    /**
     * Loads the closest pair nobody has ruled on and pins it as A against B. Every action in this
     * panel takes the pair out of that queue, so the button walks forward on its own.
     */
    function loadNextUndecided() {
        setPending(true);
        host.records
            .nextUndecidedDuplicate({distance})
            .then((next) => {
                if (next.pair === null) {
                    host.ui.toast(`Nothing left to rule on within ${bitsLabel(distance)}`, {kind: 'info'});
                    return;
                }
                setFocused(next.pair.record_id_a);
                setCompared(next.pair.record_id_b);
                setFlipSide('a');
            })
            .catch((error: unknown) => {
                host.ui.toast(message(error), {kind: 'error'});
            })
            .finally(() => setPending(false));
    }

    function metadataFor(id: RecordId): RecordMetadata | undefined {
        const known = state.status === 'ok' ? state.metadata.get(id) : undefined;
        return known ?? (similar.status === 'ok' ? similar.metadata.get(id) : undefined);
    }

    const aMetadata = focused === null ? undefined : metadataFor(focused);
    const bMetadata = compared === null ? undefined : metadataFor(compared);
    const aBounds = boundsOf(aMetadata);
    const bBounds = boundsOf(bMetadata);
    const pixels = usePixelComparison(host, focused, compared, aBounds, bBounds);

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
                    disabled={pending}
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

    function similarDistance(id: RecordId): number | undefined {
        if (similar.status !== 'ok') return undefined;
        return similar.records.find((record) => record.record_id === id)?.distance;
    }

    const onCardMenu = (event: ReactMouseEvent, id: RecordId) => openRecordMenu(event, [id]);
    const comparing = compared !== null;
    const comparedRelation = compared === null ? undefined : relationTo(compared);
    const comparable = sameResolution(aBounds, bBounds);

    return (
        <div className={`file-relationships-panel${comparing ? ' is-comparing' : ''}`}>
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
                        <button
                            type="button"
                            className="file-relationship-action"
                            disabled={pending}
                            title={`Jump to the closest pair within ${bitsLabel(distance)} that nobody has ruled on`}
                            onClick={loadNextUndecided}
                        >
                            Next Unresolved
                        </button>
                        {comparing && (
                            <>
                                <button
                                    type="button"
                                    className="file-relationship-action"
                                    disabled={aMetadata?.size === undefined || bMetadata?.size === undefined}
                                    title={`Download both record ${focused} and record ${compared}`}
                                    onClick={() => {
                                        downloadRecord(host, focused);
                                        downloadRecord(host, compared);
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
                {compared === null ? (
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
                        {compareView === 'side-by-side' && (
                            <div className="file-relationship-compare">
                                <div className="file-relationship-compare-side is-a">
                                    <FileCard
                                        host={host}
                                        id={focused}
                                        metadata={aMetadata}
                                        selected
                                        onSelect={() => {
                                        }}
                                        onMenu={onCardMenu}
                                    />
                                    <CompareInfo
                                        which="a"
                                        id={focused}
                                        metadata={aMetadata}
                                        other={bMetadata}
                                        detail="primary"
                                        pixelDuplicate={pixels.status === 'identical'}
                                        active={false}
                                    />
                                </div>
                                <div className="file-relationship-compare-side is-b">
                                    <FileCard
                                        host={host}
                                        id={compared}
                                        metadata={bMetadata}
                                        selected
                                        relation={comparedRelation}
                                        onSelect={() => setCompared(null)}
                                        onMenu={onCardMenu}
                                    />
                                    <CompareInfo
                                        which="b"
                                        id={compared}
                                        metadata={bMetadata}
                                        other={aMetadata}
                                        detail={describe(compared, similarDistance(compared)) ?? 'no known relationship'}
                                        pixelDuplicate={pixels.status === 'identical'}
                                        active={false}
                                    />
                                </div>
                            </div>
                        )}
                        {compareView === 'flip' && (
                            <FlipCompare
                                host={host}
                                a={focused}
                                b={compared}
                                aBounds={aBounds}
                                bBounds={bBounds}
                                side={flipSide}
                                onSide={setFlipSide}
                                aMetadata={aMetadata}
                                bMetadata={bMetadata}
                                bDetail={comparedRelation}
                                pixelDuplicate={pixels.status === 'identical'}
                                onMenu={onCardMenu}
                            />
                        )}
                        {compareView === 'diff'
                            && (comparable ? (
                                <DiffCompare host={host} a={focused} b={compared} aBounds={aBounds} bBounds={bBounds}/>
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
                                className="file-relationship-action"
                                disabled={pending || comparedRelation === 'inferior'}
                                title={`Mark record ${focused} as the superior copy of record ${compared}`}
                                onClick={() =>
                                    runAction(async () => {
                                        await host.records.setBetterDuplicate(compared, focused);
                                        return `Record #${focused} is now superior to #${compared}`;
                                    })
                                }
                            >
                                A #{focused} Better
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action"
                                disabled={pending || comparedRelation === 'superior'}
                                title={`Mark record ${compared} as the superior copy of record ${focused}`}
                                onClick={() =>
                                    runAction(async () => {
                                        await host.records.setBetterDuplicate(focused, compared);
                                        return `Record #${compared} is now superior to #${focused}`;
                                    })
                                }
                            >
                                B #{compared} Better
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-primary"
                                disabled={pending || comparedRelation === 'alternative'}
                                title={`Pair records ${focused} and ${compared} as alternatives`}
                                onClick={() =>
                                    runAction(async () => {
                                        await host.records.addAlternatives([focused, compared]);
                                        return `Records #${focused} and #${compared} are now alternatives`;
                                    })
                                }
                            >
                                Set Alternatives
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action"
                                disabled={pending || comparedRelation === 'unrelated'}
                                title={`Mark records ${focused} and ${compared} as coincidental lookalikes`}
                                onClick={() =>
                                    runAction(async () => {
                                        await host.records.setUnrelated(focused, compared);
                                        return `#${focused} and #${compared} are now marked unrelated`;
                                    })
                                }
                            >
                                Set Unrelated
                            </button>
                            <button
                                type="button"
                                className="file-relationship-action is-danger"
                                disabled={pending}
                                title={`Drop the relationship between records ${focused} and ${compared}`}
                                onClick={() =>
                                    runAction(async () => {
                                        const cleared = await host.records.clearRelationship(focused, compared);
                                        if (cleared.duplicate_removed || cleared.alternative_removed)
                                            return `Cleared the relationship between #${focused} and #${compared}`;
                                        return `#${focused} and #${compared} have no direct relationship to clear`;
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
