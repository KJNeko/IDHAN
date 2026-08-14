/**
 * Registers the built-in panel catalog. Import this once at startup for its side effects.
 * Registration is the same path third-party plugins use.
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
import {embeddingsPanel} from './EmbeddingsPanel';
import {embeddingSearchPanel} from './EmbeddingSearchPanel';
import {embeddingComparePanel} from './EmbeddingComparePanel';
import {debugPanel} from './DebugPanel';

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
    registerPanel(embeddingsPanel);
  registerPanel(embeddingSearchPanel);
  registerPanel(embeddingComparePanel);
  registerPanel(serverStatusPanel);
  registerPanel(selectionInspectorPanel);
    registerPanel(debugPanel);
}
