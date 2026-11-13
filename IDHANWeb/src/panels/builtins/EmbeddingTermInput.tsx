import {useState} from 'react';

export type EmbeddingInput = { kind: 'text'; text: string } | { kind: 'record'; recordId: number };

export function EmbeddingTermInput({textEnabled, onAdd}: {
	textEnabled: boolean;
	onAdd: (term: EmbeddingInput) => void;
}) {
	const [kind, setKind] = useState<'text' | 'record'>('text');
	const [draft, setDraft] = useState('');
	const activeKind = textEnabled ? kind : 'record';
	const text = draft.trim();
	const recordId = Number(text);
	const valid = activeKind === 'text' ? text.length > 0
		: text.length > 0 && Number.isInteger(recordId) && recordId > 0 && recordId <= 2147483647;

	function add() {
		if (!valid) return;
		onAdd(activeKind === 'text' ? {kind: 'text', text} : {kind: 'record', recordId});
		setDraft('');
	}

	return <div className="embed-add-row">
		<label className="embed-input-kind">Term type
			<select aria-label="Term type" value={activeKind} onChange={event => {
				setKind(event.target.value as 'text' | 'record');
				setDraft('');
			}}>
				<option value="text" disabled={!textEnabled}>{textEnabled ? 'Text' : 'Text unavailable'}</option>
				<option value="record">Reference image</option>
			</select>
		</label>
		<label className="embed-input-value">{activeKind === 'text' ? 'Text' : 'Record ID'}
			<input className="search-input" type={activeKind === 'text' ? 'text' : 'number'}
				   aria-label={activeKind === 'text' ? 'Text' : 'Record ID'}
				   min={activeKind === 'record' ? 1 : undefined} max={activeKind === 'record' ? 2147483647 : undefined}
				   step={activeKind === 'record' ? 1 : undefined} value={draft}
				   onChange={event => setDraft(event.target.value)}
				   onKeyDown={event => {
					   if (event.key === 'Enter') {
						   event.preventDefault();
						   add();
					   }
				   }}/>
		</label>
		<button type="button" className="toolbar-button" disabled={!valid} onClick={add}>Add term</button>
	</div>;
}
