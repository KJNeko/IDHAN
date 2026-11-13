import {useEffect, useState} from 'react';
import {loadMimes, peekMimes, type MimeCatalogue} from '../../host/mimeCatalogue';
import {SYSTEM_PREDICATES} from './systemPredicates';

const OPERATORS = [{value: '=', label: 'Equals'}, {value: '>', label: 'Greater than'},
	{value: '<', label: 'Less than'}, {value: '>=', label: 'At least'}, {value: '<=', label: 'At most'}];
const UNITS = [{value: 1, label: 'B'}, {value: 1024, label: 'KiB'},
	{value: 1048576, label: 'MiB'}, {value: 1073741824, label: 'GiB'}];

export function SearchFilterInput({onAdd}: { onAdd: (term: string) => void }) {
	const [field, setField] = useState('system:width');
	const [operator, setOperator] = useState('=');
	const [value, setValue] = useState('');
	const [unit, setUnit] = useState(1);
	const [distance, setDistance] = useState('8');
	const [mimes, setMimes] = useState<MimeCatalogue>(peekMimes);
	const mime = field === 'system:mime' || field === 'system:mime_id';
	useEffect(() => {
		if (!mime || mimes.entries.length > 0) return;
		let active = true;
		void loadMimes().then(next => {
			if (active) setMimes(next);
		});
		return () => {
			active = false;
		};
	}, [mime, mimes]);
	const predicate = SYSTEM_PREDICATES.find(item => item.text === field)!;
	const flag = predicate.kind === 'flag';
	const hash = field === 'system:sha256';
	const nearby = field === 'system:nearby';
	const bytes = field === 'system:filesize';
	const number = Number(value);
	const numericValid = value.trim() !== '' && Number.isFinite(number) && number >= 0
		&& (bytes || Number.isInteger(number));
	const distanceValid = distance !== '' && Number.isInteger(Number(distance)) && Number(distance) >= 0 && Number(distance) <= 64;
	const valid = flag || (hash ? /^[0-9a-f]{64}$/i.test(value.trim())
		: mime ? value !== '' : numericValid && (!nearby || (number > 0 && distanceValid)));

	function add() {
		if (!valid) return;
		const argument = bytes ? String(Math.round(number * unit)) : value.trim();
		const term = flag ? field : nearby ? `${field} ${number} distance ${Number(distance)}`
			: `${field} ${hash || mime ? '=' : operator} ${argument}`;
		onAdd(term);
		setValue('');
	}

	return <details className="search-filter-builder">
		<summary>Add filter</summary>
		<div className="panel-row">
			<label>Filter
				<select aria-label="Filter" value={field} onChange={event => {
					setField(event.target.value);
					setValue('');
					setOperator('=');
				}}>
					{SYSTEM_PREDICATES.map(item => <option key={item.text} value={item.text}>
						{item.text.slice(7).replaceAll('_', ' ')}
					</option>)}
				</select>
			</label>
			{!flag && !hash && !mime && !nearby && <label>Comparison
                <select aria-label="Comparison" value={operator} onChange={event => setOperator(event.target.value)}>
					{OPERATORS.map(item => <option key={item.value} value={item.value}>{item.label}</option>)}
                </select>
            </label>}
			{!flag && <label>{nearby ? 'Record ID' : hash ? 'SHA-256' : mime ? 'File type' : 'Value'}
				{mime ? <select aria-label="File type" value={value} onChange={event => setValue(event.target.value)}>
					<option value="">Choose file type</option>
					{(field === 'system:mime' ? mimes.names : mimes.entries).map(item => <option key={item.id}
																								 value={field === 'system:mime' ? item.name : item.id}>{item.name}{field === 'system:mime_id' ? ` (#${item.id})` : ''}</option>)}
				</select> : <input aria-label={nearby ? 'Record ID' : hash ? 'SHA-256' : 'Filter value'}
								   type={hash ? 'text' : 'number'} min={nearby ? 1 : 0} step={bytes ? 'any' : 1}
								   maxLength={hash ? 64 : undefined} value={value}
				                   onChange={event => setValue(event.target.value)}/>}
            </label>}
			{bytes && <label>Units<select aria-label="Units" value={unit}
                                          onChange={event => setUnit(Number(event.target.value))}>
				{UNITS.map(item => <option key={item.value} value={item.value}>{item.label}</option>)}
            </select></label>}
			{nearby && <label>Hash distance<input aria-label="Hash distance" type="number" min={0} max={64} step={1}
                                                  value={distance} onChange={event => setDistance(event.target.value)}/></label>}
			<button type="button" className="toolbar-button" disabled={!valid} onClick={add}>Add filter</button>
		</div>
	</details>;
}
