/**
 * Maps panel *types* to the React components dockview renders. Each known type gets a wrapper that
 * builds a per-instance host and renders the registered panel. Any type present in the document but
 * absent from the registry (a plugin not installed) renders a tombstone that preserves the config.
 */

import { useMemo, type FC } from 'react';
import type { IDockviewPanelProps } from 'dockview-react';
import type { PanelDefinition } from '../host/types';
import { createHost } from '../host/createHost';
import { listPanels } from '../panels/registry';
import { makeSettingsBinding } from './settingsBinding';

type DockPanelProps = IDockviewPanelProps;

function instanceIdOf(props: DockPanelProps): string {
  return String((props.params as { instanceId?: unknown }).instanceId ?? props.api.id);
}

function InstanceHostPanel({ def, instanceId }: { def: PanelDefinition; instanceId: string }) {
  const host = useMemo(() => createHost(instanceId, makeSettingsBinding(instanceId)), [instanceId]);
  const Component = def.component;
  return <Component host={host} />;
}

function makeKnownComponent(def: PanelDefinition): FC<DockPanelProps> {
  return function KnownPanel(props: DockPanelProps) {
    return <InstanceHostPanel def={def} instanceId={instanceIdOf(props)} />;
  };
}

function makeTombstone(type: string): FC<DockPanelProps> {
  return function TombstonePanel() {
    return (
      <div className="panel-body tombstone">
        <p className="error">Unknown panel type “{type}”.</p>
        <p className="muted">
          Its saved settings are preserved. Install the plugin that provides this panel to restore it.
        </p>
      </div>
    );
  };
}

/** Build the dockview `components` map: every registered panel, plus tombstones for the given unknowns. */
export function buildComponents(unknownTypes: readonly string[]): Record<string, FC<DockPanelProps>> {
  const components: Record<string, FC<DockPanelProps>> = {};
  for (const def of listPanels()) components[def.type] = makeKnownComponent(def);
  for (const type of unknownTypes) {
    if (!components[type]) components[type] = makeTombstone(type);
  }
  return components;
}
