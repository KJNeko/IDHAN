import { describe, expect, it } from 'vitest';
import { LAYOUT_SCHEMA_VERSION, createEmptyLayout, migrateLayout } from './document';

describe('migrateLayout', () => {
  it('round-trips a valid current-schema document', () => {
    const doc = createEmptyLayout('My Layout');
    const migrated = migrateLayout(JSON.parse(JSON.stringify(doc)));
    expect(migrated).not.toBeNull();
    expect(migrated?.id).toBe(doc.id);
    expect(migrated?.name).toBe('My Layout');
    expect(migrated?.schema).toBe(LAYOUT_SCHEMA_VERSION);
  });

  it('rejects non-objects and garbage', () => {
    expect(migrateLayout(null)).toBeNull();
    expect(migrateLayout(42)).toBeNull();
    expect(migrateLayout('nope')).toBeNull();
    expect(migrateLayout([])).toBeNull();
  });

  it('rejects a document missing required fields', () => {
    expect(migrateLayout({ id: 'x', name: 'y' })).toBeNull(); // no engine/panels
    expect(migrateLayout({ id: 'x', name: 'y', engine: { kind: 'other' }, panels: {} })).toBeNull();
    expect(migrateLayout({ name: 'y', engine: { kind: 'dockview' }, panels: {} })).toBeNull(); // no id
  });

  it('rejects an unknown-schema document rather than guessing', () => {
    const doc = { ...createEmptyLayout('x'), schema: 0 };
    expect(migrateLayout(doc)).toBeNull();
  });
});
