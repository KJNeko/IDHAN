import {useState} from 'react';
import {api} from '../../api/client';
import {ParserHosts} from './ParserHosts';
import type {
    DownloaderOptionDefinition,
    DownloaderOptions,
    DownloadSettings,
    DownloaderOptionValue
} from '../../api/types';

function OptionInput({option, onChange}: {
    option: DownloaderOptionDefinition;
    onChange: (value: DownloaderOptionValue) => void;
}) {
	if (option.type === 'boolean') return <label className="download-option download-option-toggle">
		<span>{option.name}</span>
        <input type="checkbox" defaultChecked={option.default} onChange={event => onChange(event.target.checked)}/>
    </label>;

    return <label className="download-option">
		<span>{option.name}</span>
        {option.type === 'string[]' ? <textarea defaultValue={option.default.join('\n')}
                                                aria-label={option.name} placeholder="One value per line"
                                                onChange={event => onChange(event.target.value.split('\n').filter(value => value.length > 0))}/>
            : option.type === 'number' ? <input type="number" defaultValue={option.default} required
                                                min={option.minimum} step={option.integer ? 1 : 'any'}
                                                onChange={event => {
                                                    if (event.target.validity.valid) onChange(event.target.valueAsNumber);
                                                }}/>
                : <input type="text" defaultValue={option.default} onChange={event => onChange(event.target.value)}/>}
    </label>;
}

export function DownloadOptions({settings, onChange, disabled}: {
    settings: DownloadSettings;
    onChange: (settings: DownloadSettings) => void;
    disabled: boolean;
}) {
    const [known, setKnown] = useState<DownloaderOptions>();
    const [loading, setLoading] = useState(false);
    const [error, setError] = useState<string>();

    async function load() {
        setLoading(true);
        setError(undefined);
        try {
            setKnown(await api.downloadSessions.options());
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        } finally {
            setLoading(false);
        }
    }

    function setOption(parser: string, name: string, value: DownloaderOptionValue) {
        onChange({
            ...settings,
            parsers: {...settings.parsers, [parser]: {...settings.parsers?.[parser], [name]: value}}
        });
    }

    return <details className="download-options" onToggle={event => {
        if (event.target === event.currentTarget && event.currentTarget.open && !known && !loading && !error) void load();
    }}>
        <summary>Downloader options</summary>

        {loading && <p role="status">Loading options…</p>}
        {error && <p role="alert">Could not load options: {error}
            <button type="button" onClick={() => void load()}>Retry</button>
        </p>}
		{known && <fieldset className="download-option-groups" disabled={disabled}>
            <div className="download-group-body">
                <label className="download-option download-option-toggle">
                    <span>Re-download and re-import existing files</span>
                    <input type="checkbox" checked={settings.force_import ?? known.force_import}
                           onChange={event => onChange({...settings, force_import: event.target.checked})}/>
                </label>
            </div>
			{known.parsers.map(parser => <details className="download-option-group" key={parser.id}>
				<summary><span className="download-group-name">{parser.id}</span>{' '}
					<span className="download-group-count">({parser.options.length})</span>
				</summary>
				<div className="download-group-body">
					<div className="download-option-list">
						{parser.options.length === 0 && <p className="muted">No parser options.</p>}
						{parser.options.map(option => <OptionInput key={option.name} option={option}
																   onChange={value => setOption(parser.id, option.name, value)}/>)}
					</div>
					<ParserHosts parser={parser.id} disabled={disabled}/>
				</div>
            </details>)}
        </fieldset>}
    </details>;
}
