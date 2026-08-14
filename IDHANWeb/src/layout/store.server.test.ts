/**
 * Server push/pull actions on the layout store. The REST client is mocked, so these cover the store's
 * orchestration (push then refresh, pull-and-migrate, busy-flag lifecycle), not the wire.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { LayoutDocument } from './document';
import type { ServerLayoutMeta } from '../api/types';

// vi.hoisted so the mock factory can reference these before the const initializers run.
const mocks = vi.hoisted(() => ({
  list: vi.fn(),
  get: vi.fn(),
  push: vi.fn(),
  remove: vi.fn(),
}));

vi.mock('../api/client', () => ({
  layouts: { list: mocks.list, get: mocks.get, push: mocks.push, remove: mocks.remove },
  // Present so the module shape is complete for anything else importing from the client.
  ApiError: class ApiError extends Error {},
}));

import { useLayoutStore } from './store';
import { createEmptyLayout } from './document';

const META: ServerLayoutMeta[] = [
  { id: 'a', name: 'Alpha', schema: 1, created_at: 1, updated_at: 2 },
];

beforeEach(() => {
  localStorage.clear();
  vi.clearAllMocks();
  mocks.list.mockResolvedValue(META);
  useLayoutStore.setState({
    doc: createEmptyLayout('Default'),
    generation: 0,
    serverLayouts: [],
    serverBusy: false,
  });
});

describe('layout store: server sync', () => {
  it('refreshServerLayouts populates serverLayouts', async () => {
    await useLayoutStore.getState().refreshServerLayouts();
    expect(mocks.list).toHaveBeenCalledOnce();
    expect(useLayoutStore.getState().serverLayouts).toEqual(META);
    expect(useLayoutStore.getState().serverBusy).toBe(false);
  });

  it('pushLayoutToServer pushes the working doc, then refreshes the list', async () => {
    mocks.push.mockResolvedValue({ id: 'x' });
    const doc = useLayoutStore.getState().doc;

    await useLayoutStore.getState().pushLayoutToServer();

    expect(mocks.push).toHaveBeenCalledWith(doc);
    expect(mocks.list).toHaveBeenCalledOnce();
    expect(useLayoutStore.getState().serverLayouts).toEqual(META);
  });

  it('pullLayoutFromServer replaces the working doc with the migrated server copy', async () => {
    const remote = createEmptyLayout('Remote');
    mocks.get.mockResolvedValue(remote as LayoutDocument);

    await useLayoutStore.getState().pullLayoutFromServer(remote.id);

    expect(mocks.get).toHaveBeenCalledWith(remote.id);
    expect(useLayoutStore.getState().doc.id).toBe(remote.id);
    expect(useLayoutStore.getState().doc.name).toBe('Remote');
    expect(useLayoutStore.getState().generation).toBe(1);
  });

  it('pullLayoutFromServer rejects an unreadable document and clears the busy flag', async () => {
    mocks.get.mockResolvedValue({ not: 'a layout' } as unknown as LayoutDocument);
    const before = useLayoutStore.getState().doc;

    await expect(useLayoutStore.getState().pullLayoutFromServer('a')).rejects.toThrow();

    // The working document is untouched and the store is no longer busy.
    expect(useLayoutStore.getState().doc).toBe(before);
    expect(useLayoutStore.getState().serverBusy).toBe(false);
  });

  it('deleteServerLayout removes then refreshes', async () => {
    mocks.remove.mockResolvedValue({ deleted: true });

    await useLayoutStore.getState().deleteServerLayout('a');

    expect(mocks.remove).toHaveBeenCalledWith('a');
    expect(mocks.list).toHaveBeenCalledOnce();
  });
});
