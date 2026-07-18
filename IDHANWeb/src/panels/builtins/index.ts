/**
 * Registers the built-in panel catalog. Import this once at startup for its side effects.
 *
 * These are the M3 seed panels that prove the layout + host loop end to end; the P0 workhorses
 * (Search, Grid, Viewer, Record Info, Tag Editor) arrive in M4 and register the same way.
 */

import { registerPanel } from '../registry';
import { serverStatusPanel } from './ServerStatusPanel';
import { selectionInspectorPanel } from './SelectionInspectorPanel';
import { searchPanel } from './SearchPanel';

let registered = false;

export function registerBuiltinPanels(): void {
  if (registered) return;
  registered = true;
  registerPanel(searchPanel);
  registerPanel(serverStatusPanel);
  registerPanel(selectionInspectorPanel);
}
