import type { PanelDefinition } from '../host/types';

const registry = new Map<string, PanelDefinition>();

export function registerPanel(definition: PanelDefinition): void {
  if (registry.has(definition.type)) {
    throw new Error(`Panel type already registered: ${definition.type}`);
  }
  registry.set(definition.type, definition);
}

export function getPanel(type: string): PanelDefinition | undefined {
  return registry.get(type);
}

export function listPanels(): PanelDefinition[] {
  return [...registry.values()];
}

export function hasPanel(type: string): boolean {
  return registry.has(type);
}
