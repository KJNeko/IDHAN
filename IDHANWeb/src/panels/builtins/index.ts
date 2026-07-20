/**
 * Registers the built-in panel catalog. Import this once at startup for its side effects.
 *
 * Registration is the same path third-party plugins use. The catalog now spans P0 (Search, Grid,
 * Viewer, Record Info, Tag Editor), P1 (Import, Job Monitor, Server Status, Notes, URLs), and P2
 * (Log Viewer, Cluster Manager, Database Stats, Tag Domain Manager, Tag Relationships).
 */

import { registerPanel } from '../registry';
import { serverStatusPanel } from './ServerStatusPanel';
import { selectionInspectorPanel } from './SelectionInspectorPanel';
import { searchPanel } from './SearchPanel';
import { gridPanel } from './GridPanel';
import { mediaViewerPanel } from './MediaViewerPanel';
import { recordInfoPanel } from './RecordInfoPanel';
import { tagEditorPanel } from './TagEditorPanel';
import { urlListPanel } from './UrlListPanel';
import { notesPanel } from './NotesPanel';
import { importPanel } from './ImportPanel';
import { jobMonitorPanel } from './JobMonitorPanel';
import { logViewerPanel } from './LogViewerPanel';
import { clusterManagerPanel } from './ClusterManagerPanel';
import { databaseStatsPanel } from './DatabaseStatsPanel';
import { tagDomainManagerPanel } from './TagDomainManagerPanel';
import { tagRelationshipsPanel } from './TagRelationshipsPanel';

let registered = false;

export function registerBuiltinPanels(): void {
  if (registered) return;
  registered = true;
  registerPanel(searchPanel);
  registerPanel(gridPanel);
  registerPanel(mediaViewerPanel);
  registerPanel(recordInfoPanel);
  registerPanel(tagEditorPanel);
  registerPanel(urlListPanel);
  registerPanel(notesPanel);
  registerPanel(importPanel);
  registerPanel(jobMonitorPanel);
  registerPanel(logViewerPanel);
  registerPanel(clusterManagerPanel);
  registerPanel(databaseStatsPanel);
  registerPanel(tagDomainManagerPanel);
  registerPanel(tagRelationshipsPanel);
  registerPanel(serverStatusPanel);
  registerPanel(selectionInspectorPanel);
}
