import { describe, expect, it } from 'vitest';
import {
  DEFAULT_SCAN_PARAMS,
  fastScanPreset,
  sanitizeScanParams,
  buildScanQuery,
  type ScanParams,
} from './ClusterManagerPanel';

describe('DEFAULT_SCAN_PARAMS', () => {
  it('enables mime + metadata scanning and nothing else', () => {
    expect(DEFAULT_SCAN_PARAMS).toEqual({
      scan_mime: true,
      rescan_mime: false,
      scan_metadata: true,
      rescan_metadata: false,
      verify_hash: false,
      adopt_orphans: false,
      fix_extensions: false,
      stop_on_fail: false,
      remove_missing_files: false,
      readonly: false,
    });
  });
});

describe('fastScanPreset', () => {
  it('disables mime, metadata, and adopt_orphans (hash-only)', () => {
    const out = fastScanPreset({ ...DEFAULT_SCAN_PARAMS, adopt_orphans: true });
    expect(out.scan_mime).toBe(false);
    expect(out.scan_metadata).toBe(false);
    expect(out.adopt_orphans).toBe(false);
  });

  it('preserves unrelated params and does not mutate the input', () => {
    const input: ScanParams = { ...DEFAULT_SCAN_PARAMS, verify_hash: true, stop_on_fail: true };
    const out = fastScanPreset(input);
    expect(out.verify_hash).toBe(true);
    expect(out.stop_on_fail).toBe(true);
    expect(input.scan_mime).toBe(true); // input untouched
  });
});

describe('sanitizeScanParams', () => {
  it('clears fix_extensions when the cluster is read-only', () => {
    const out = sanitizeScanParams({ ...DEFAULT_SCAN_PARAMS, fix_extensions: true }, true);
    expect(out.fix_extensions).toBe(false);
  });

  it('clears fix_extensions when the user forces read-only', () => {
    const out = sanitizeScanParams({ ...DEFAULT_SCAN_PARAMS, fix_extensions: true, readonly: true }, false);
    expect(out.fix_extensions).toBe(false);
  });

  it('leaves params untouched on a writable cluster', () => {
    const input: ScanParams = { ...DEFAULT_SCAN_PARAMS, fix_extensions: true };
    const out = sanitizeScanParams(input, false);
      expect(out).toBe(input); // same reference, no copy when nothing to void
    expect(out.fix_extensions).toBe(true);
  });
});

describe('buildScanQuery', () => {
  it('encodes every param as a true/false string', () => {
    const qs = buildScanQuery(DEFAULT_SCAN_PARAMS);
    const parsed = new URLSearchParams(qs);
    expect(parsed.get('scan_mime')).toBe('true');
    expect(parsed.get('scan_metadata')).toBe('true');
    expect(parsed.get('verify_hash')).toBe('false');
    expect(parsed.get('readonly')).toBe('false');
  });

  it('reflects a fast-scan preset in the query string', () => {
    const qs = buildScanQuery(fastScanPreset(DEFAULT_SCAN_PARAMS));
    const parsed = new URLSearchParams(qs);
    expect(parsed.get('scan_mime')).toBe('false');
    expect(parsed.get('scan_metadata')).toBe('false');
  });
});
