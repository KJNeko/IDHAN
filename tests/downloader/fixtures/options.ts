import {applyOptions, option as setting, parser} from './packages/sdk/decorators.js';
import * as sdk from './packages/sdk/decorators.js';
import {bindParser} from './packages/sdk/runtime.js';
import type {Session} from './packages/sdk/session.js';
import type {ParserMetadata} from './packages/sdk/parser.js';

export let constructions = 0;

@parser({id: 'options', version: 1})
export default class Options {
    declare static readonly metadata: ParserMetadata;

    @setting({type: 'boolean', default: false, description: 'Enabled'})
    accessor enabled = false;

    @sdk.option({type: 'number', default: 2, description: 'Count', minimum: 0, integer: true})
    accessor count = 2;

    @setting({type: 'string', default: 'example', description: 'Title'})
    accessor title = 'example';

    @setting({type: 'string[]', default: [], description: 'Tags'})
    accessor tags: readonly string[] = [];

    @setting({type: 'string[]', default: ['one'], description: 'Excluded'})
    accessor excluded = ['one'];

    constructor(session: Pick<Session, 'options'>) {
        ++constructions;
        applyOptions(this, session.options);
    }
}

export const run = bindParser(Options);

export function verify() {
    const first = new Options({options: {enabled: true, count: 3, tags: ['cat']}});
    const second = new Options({options: {}});
    const rejected: string[] = [];
    for (const options of [{count: -1}, {count: 1.5}, {enabled: 'yes'}, {tags: [1]}, {missing: true}]) {
        try {
            new Options({options});
        } catch {
            rejected.push(Object.keys(options)[0]!);
        }
    }
    return {
        enabled: first.enabled, count: first.count, tags: first.tags,
        defaults: {enabled: second.enabled, count: second.count, title: second.title, tags: second.tags},
        independent: first.tags !== second.tags,
        frozen: Object.isFrozen(first.tags) && Object.isFrozen(second.tags),
        rejected
    };
}
