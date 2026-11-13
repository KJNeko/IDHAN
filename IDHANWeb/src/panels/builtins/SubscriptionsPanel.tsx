import {useEffect, useState} from 'react';
import {api} from '../../api/client';
import {watchSubscriptions} from '../../api/subscriptionsStream';
import {formatBytes} from './RecordInfoView';
import type {
	Subscription,
	SubscriptionGroup,
	SubscriptionGroupInput,
	DownloaderParser,
	GeneratorParameter
} from '../../api/types';

const units = {days: 86400, weeks: 604800, months: 2592000} as const;
type Unit = keyof typeof units;
const defaults: SubscriptionGroupInput = {
	name: '', generator: '', enabled: true, interval_seconds: 86400,
};

interface GeneratorChoice {
	id: string;
	parser: string;
	name: string;
	parameters: Record<string, GeneratorParameter>;
}

export function generatorChoices(parsers: DownloaderParser[]): GeneratorChoice[] {
	const active = new Map<string, DownloaderParser>();
	for (const parser of parsers) {
		if (!active.has(parser.id) || active.get(parser.id)!.version < parser.version) active.set(parser.id, parser);
	}
	return [...active.values()].flatMap(parser => parser.handlers.map(handler => ({
		id: `${parser.id}:${handler.id}`, parser: parser.id, name: handler.id, parameters: handler.parameters,
	})));
}

function GeneratorSelect({choices, value, change}: {
	choices: GeneratorChoice[];
	value: string;
	change: (value: string) => void
}) {
	return <select aria-label="Generator" value={value} required onChange={event => change(event.target.value)}>
		{!choices.some(choice => choice.id === value) && <option value={value}>{value || 'Generator'}</option>}
		{[...new Set(choices.map(choice => choice.parser))].map(parser => <optgroup key={parser} label={parser}>
			{choices.filter(choice => choice.parser === parser).map(choice =>
				<option key={choice.id} value={choice.id}>{choice.name}</option>)}
		</optgroup>)}
	</select>;
}

function itemLabel(item: Subscription): string {
	return item.tags || Object.values(item.parameters).join(' · ');
}

function AddSubscription({group, choice, busy, act}: {
	group: SubscriptionGroup;
	choice?: GeneratorChoice;
	busy: boolean;
	act: (action: () => Promise<unknown>) => Promise<void>;
}) {
	const [values, setValues] = useState<Record<string, string>>({});
	const fields = Object.entries(choice?.parameters ?? {});

	function parametersFor(overrides: Record<string, string> = {}) {
		const current = {...values, ...overrides};
		return Object.fromEntries(fields.filter(([key, field]) => key in current || field.default !== undefined || !field.optional)
		.map(([key, field]) => [key, current[key] ?? field.default ?? '']));
	}

	return <form onKeyDown={event => {
		if (event.key !== 'Enter' || event.nativeEvent.isComposing) return;
		event.preventDefault();
		event.currentTarget.requestSubmit();
	}} onSubmit={event => {
		event.preventDefault();
		if (busy || !choice) return;
		const parameters = parametersFor();
		const body = fields.length === 1 && fields[0]?.[0] === 'tags'
			? {tags: parameters.tags ?? '', enabled: true} : {parameters, enabled: true};
		void act(async () => {
			await api.subscriptions.create(group.id, body);
			setValues({});
		});
	}}>
		{fields.map(([key, field]) => <input key={key} className="search-input" aria-label={field.name}
											 placeholder={field.name} value={values[key] ?? field.default ?? ''}
											 required={!field.optional && field.default === undefined} readOnly={busy}
											 onPaste={event => {
												 const text = event.clipboardData.getData('text');
												 if (!text.includes('\n') && !text.includes('\r')) return;
												 event.preventDefault();
												 if (busy || !choice) return;
												 // Split clipboard rows, accepting Windows, Unix, and classic Mac line endings.
												 const lines = text.split(/\r\n|\n|\r/).filter(line => line.trim().length > 0);
												 const form = event.currentTarget.form;
												 const otherRequired = fields.find(([other, spec]) => other !== key && !spec.optional &&
													 (values[other] ?? spec.default ?? '') === '');
												 if (otherRequired) {
													 form?.reportValidity();
													 return;
												 }
												 void act(async () => {
													 for (let start = 0; start < lines.length; start += 5000) {
														 await api.subscriptions.batch(group.id, lines.slice(start, start + 5000).map(line => ({
															 parameters: parametersFor({[key]: line}), enabled: true,
														 })));
													 }
													 setValues({});
												 });
											 }}
											 onChange={event => setValues({...values, [key]: event.target.value})}/>)}
		{fields.length === 0 && choice && <button className="toolbar-button" disabled={busy}>Add</button>}
	</form>;
}

function intervalLabel(seconds: number): string {
	for (const unit of ['months', 'weeks', 'days'] as const) {
		if (seconds % units[unit] === 0) return `${seconds / units[unit]} ${unit}`;
	}
	return `${seconds / 3600} hours`;
}

function GroupSettings({group, choices, busy, save, cancel}: {
	group: SubscriptionGroup; choices: GeneratorChoice[]; busy: boolean;
	save: (body: SubscriptionGroupInput) => Promise<void>; cancel: () => void;
}) {
	const [draft, setDraft] = useState<SubscriptionGroupInput>(group);
	const [unit, setUnit] = useState<Unit>(group.interval_seconds % units.months === 0 ? 'months'
		: group.interval_seconds % units.weeks === 0 ? 'weeks' : 'days');
	return <form className="subscription-settings" onSubmit={event => {
		event.preventDefault();
		void save(draft);
	}}>
		<fieldset disabled={busy}>
			<label>Name <input className="search-input" required maxLength={200} value={draft.name}
							   onChange={event => setDraft({...draft, name: event.target.value})}/></label>
			<label>Generator <GeneratorSelect choices={choices} value={draft.generator}
											  change={generator => setDraft({...draft, generator})}/></label>
			<label>Origin <input className="search-input" placeholder="Origin" value={draft.origin ?? ''}
								 onChange={event => setDraft({...draft, origin: event.target.value})}/></label>
			<label>Every <input aria-label="Check interval" type="number" required step="any"
								min={60 / units[unit]} max={31536000 / units[unit]}
								value={draft.interval_seconds / units[unit]}
								onChange={event => setDraft({
									...draft,
									interval_seconds: Math.round(Number(event.target.value) * units[unit])
								})}/>
				<select aria-label="Interval unit" value={unit} title="A month is 30 days" onChange={event => {
					const next = event.target.value as Unit;
					setDraft({
						...draft, interval_seconds: Math.min(31536000,
							Math.round(draft.interval_seconds / units[unit] * units[next]))
					});
					setUnit(next);
				}}>
					<option value="days">days</option>
					<option value="weeks">weeks</option>
					<option value="months">months</option>
				</select>
			</label>
			<button className="toolbar-button" type="submit">Save</button>
			<button className="toolbar-button" type="button" onClick={cancel}>Cancel</button>
		</fieldset>
	</form>;
}

function NextRun({at, paused, running}: {
	at: number | null;
	paused: boolean;
	running: boolean
}) {
	if (running) return <>Running</>;
	if (paused) return <>Paused</>;
	if (at === null) return <>—</>;
	const date = new Date(at * 1000);
	return <time dateTime={date.toISOString()} title={date.toLocaleString()}>
		{date.toLocaleString(undefined, {dateStyle: 'medium', timeStyle: 'short'})}
	</time>;
}

function SubscriptionRunStatus({item, groupEnabled}: { item: Subscription; groupEnabled: boolean }) {
	if (item.active_session_id !== null) return <>Running</>;

	if (!groupEnabled || !item.enabled) return <>Paused</>;

	if (item.dead) return <>Dead</>;

	return <>Next: <NextRun at={item.next_run_at} paused={false} running={false}/></>;
}

function SubscriptionGroupView({group, items, choices, busy, act}: {
	group: SubscriptionGroup; items: Subscription[]; choices: GeneratorChoice[]; busy: boolean;
	act: (action: () => Promise<unknown>) => Promise<void>;
}) {
	const [settings, setSettings] = useState(false);
	const scheduled = items.filter(item => item.enabled && item.active_session_id === null);
	const nextRun = scheduled.length > 0 ? Math.min(...scheduled.map(item => item.next_run_at)) : null;
	const running = items.some(item => item.active_session_id !== null);
	return <section className="subscription-group">
		<header className="subscription-group-header">
			<div><h3>{group.name}</h3>
				<div
					className="muted">{group.subscription_count} item{group.subscription_count === 1 ? '' : 's'} · {group.record_count} file{group.record_count === 1 ? '' : 's'} · <span
					title={`${group.total_size_bytes.toLocaleString()} bytes`}>{formatBytes(group.total_size_bytes)}</span>
					{' · '}{intervalLabel(group.interval_seconds)}
					{group.running_count > 0 && ` · ${group.running_count} running`}
					{group.dead_count > 0 && ` · ${group.dead_count} dead`}
					{group.error_count > 0 && ` · ${group.error_count} failed`}
					{!group.enabled && ' · Paused'}
					<span className="subscription-next-run"
						  aria-label={`Next run for ${group.name}`}> · Next run: <NextRun
						at={nextRun} running={scheduled.length === 0 && running}
						paused={!group.enabled || (items.length > 0 && scheduled.length === 0 && !running)}/></span>
				</div>
			</div>
			<div className="subscription-actions">
				<button className="toolbar-button" disabled={busy || !group.enabled}
						onClick={() => void act(() => api.subscriptionGroups.run(group.id))}>Run
				</button>
				<button className="toolbar-button" disabled={busy}
						onClick={() => void act(() => api.subscriptionGroups.update(group.id, {
							...group,
							enabled: !group.enabled
						}))}>
					{group.enabled ? 'Pause' : 'Resume'}</button>
				<button className="toolbar-button" disabled={busy} onClick={() => setSettings(!settings)}>Settings
				</button>
				<button className="toolbar-button" disabled={busy || group.running_count > 0}
						onClick={() => void act(() => api.subscriptionGroups.remove(group.id))}>Delete
				</button>
			</div>
		</header>
		{settings && <GroupSettings group={group} choices={choices} busy={busy} cancel={() => setSettings(false)}
                                    save={async body => {
										await act(async () => {
											await api.subscriptionGroups.update(group.id, body);
											setSettings(false);
										});
									}}/>}
		<ul className="subscription-items">
			{items.map(item => <li key={item.id}>
				<div className="subscription-item-main"><span>{itemLabel(item)}</span>
					<span className="subscription-next-run muted" aria-label={`Next run for ${itemLabel(item)}`}>
						<span
							title={`${item.total_size_bytes.toLocaleString()} bytes (${formatBytes(item.total_size_bytes)})`}>{item.record_count} files</span>
						{' '}({item.imported_count ?? 0} added){' '}<SubscriptionRunStatus item={item}
						                                                                   groupEnabled={group.enabled}/>
					</span>
					<span className="muted" aria-label={`Total size for ${itemLabel(item)}`}
						  title={`${item.total_size_bytes.toLocaleString()} bytes`}> · {formatBytes(item.total_size_bytes)}</span>
					<div className="subscription-actions">
						<button className="toolbar-button"
								disabled={busy || !group.enabled || !item.enabled || item.active_session_id !== null}
								aria-label={`Run ${itemLabel(item)}`}
								onClick={() => void act(() => api.subscriptions.run(item.id))}>Run
						</button>
						<button className="toolbar-button" disabled={busy}
								aria-label={`${item.enabled ? 'Pause' : 'Resume'} ${itemLabel(item)}`}
								onClick={() => void act(() => api.subscriptions.update(item.id, {
									parameters: item.parameters,
									enabled: !item.enabled
								}))}>
							{item.enabled ? 'Pause' : 'Resume'}</button>
						<button className="toolbar-button" disabled={busy || item.active_session_id !== null}
								aria-label={`Remove ${itemLabel(item)}`}
								onClick={() => void act(() => api.subscriptions.remove(item.id))}>×
						</button>
					</div>
				</div>
				{item.last_error && <small className="error">{item.last_error}</small>}
			</li>)}
		</ul>
		<AddSubscription key={group.generator} group={group}
						 choice={choices.find(choice => choice.id === group.generator)} busy={busy} act={act}/>
	</section>;
}

function SubscriptionsPanel() {
	const [groups, setGroups] = useState<SubscriptionGroup[]>([]);
	const [items, setItems] = useState<Subscription[]>([]);
	const [name, setName] = useState('');
	const [choices, setChoices] = useState<GeneratorChoice[]>([]);
	const [generator, setGenerator] = useState('');
	const [busy, setBusy] = useState(false);
	const [error, setError] = useState<string | null>(null);
	useEffect(() => watchSubscriptions({
		onChange: snapshot => {
			setGroups(snapshot.groups);
			setItems(snapshot.items);
			setError(null);
		},
		onError: setError,
	}), []);
	useEffect(() => {
		const controller = new AbortController();
		void api.downloadSessions.parsers(controller.signal).then(installed => {
			const nextChoices = generatorChoices(installed.parsers);
			setChoices(nextChoices);
			setGenerator(current => current || nextChoices[0]?.id || '');
		}).catch((caught: unknown) => {
			if (!controller.signal.aborted) setError(String(caught));
		});
		return () => controller.abort();
	}, []);

	async function act(action: () => Promise<unknown>) {
		setBusy(true);
		setError(null);
		try {
			await action();
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
		} finally {
			setBusy(false);
		}
	}

	return <div className="subscriptions-panel">
		<header className="subscription-group-header">
			<form onSubmit={event => {
				event.preventDefault();
				if (!name.trim() || !generator || busy) return;
				void act(async () => {
					await api.subscriptionGroups.create({...defaults, name: name.trim(), generator});
					setName('');
				});
			}}><GeneratorSelect choices={choices} value={generator} change={setGenerator}/><input
				className="search-input" aria-label="New group" placeholder="Group name" required
				maxLength={200} value={name} readOnly={busy} onChange={event => setName(event.target.value)}/></form>
		</header>
		{error && <p role="alert" className="error">{error}</p>}
		{groups.map(group => <SubscriptionGroupView key={group.id} group={group}
													items={items.filter(item => item.group_id === group.id)}
													choices={choices} busy={busy} act={act}/>)}
	</div>;
}

export const subscriptionsPanel = {
	type: 'subscriptions', title: 'Subscriptions', description: 'Recurring searches.',
	component: SubscriptionsPanel, configVersion: 1, singleton: true,
} as const;
