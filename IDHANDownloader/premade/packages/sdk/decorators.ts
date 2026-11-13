import type {
	DiscoveryInput,
	DiscoveryMetadata,
	DiscoveryResult,
	GeneratorInput,
	GeneratorMetadata,
	OptionDefinition,
	OptionMetadata,
	ParserMetadata
} from './parser.js';
import type {DomainContext, Session} from './session.js';
import type {ParameterDefinitions, ParameterValues, URLClassDefinition} from './url-class.js';
import {SDK_VERSION} from './version.js';

interface MethodRegistration {
	readonly id: string;
	readonly parameters: ParameterDefinitions;
}

type GeneratorMethod<This, Parameters extends ParameterDefinitions> =
	(this: This, input: GeneratorInput<ParameterValues<Parameters>>, session: Session) => Promise<void>;

// The decorator implementation owns this metadata. Neither Symbol.metadata nor reflect-metadata
// is required in QuickJS. TypeScript lowers standard decorators before the module is loaded.
const registrations = new WeakMap<object, MethodRegistration>();

const discoveryRegistrations = new WeakMap<object, string>();

const optionRegistrations = new WeakMap<object, OptionMetadata>();

const classOptions = new WeakMap<object, readonly OptionMetadata[]>();

function validateOption(value: unknown, definition: OptionDefinition, name: string):
	boolean | number | string | readonly string[] {
	switch (definition.type) {
		case 'boolean':
			if (typeof value !== 'boolean') {
				throw new Error(`Option '${name}' must be a boolean`);
			}

			return value;

		case 'number':
			if (typeof value !== 'number' || !Number.isFinite(value)) {
				throw new Error(`Option '${name}' must be a finite number`);
			}

			if (definition.integer && !Number.isSafeInteger(value)) {
				throw new Error(`Option '${name}' must be a safe integer`);
			}

			if (definition.minimum !== undefined && value < definition.minimum) {
				throw new Error(`Option '${name}' is below its minimum`);
			}

			return value;

		case 'string':
			if (typeof value !== 'string') {
				throw new Error(`Option '${name}' must be a string`);
			}

			return value;

		case 'string[]':
			if (!Array.isArray(value)) {
				throw new Error(`Option '${name}' must be a string array`);
			}

			return Object.freeze(value.map((entry: unknown) => {
				if (typeof entry !== 'string') {
					throw new Error(`Option '${name}' must contain only strings`);
				}

				return entry;
			}));
	}
}

type OptionAccessorValue<Definition extends OptionDefinition> =
	Definition extends { readonly type: 'boolean' } ?
		boolean :
		Definition extends { readonly type: 'number' } ?
			number :
			Definition extends { readonly type: 'string' } ? string :
				readonly string[];

/** TypeScript rejects an accessor whose type differs from the declared option type. */
export function option<Definition extends OptionDefinition>(definition: Definition) {
	if (definition.type !== 'number' && ('minimum' in definition || 'integer' in definition)) {
		throw new Error('@option minimum and integer constraints require a number accessor');
	}

	return function decorateOption<This, Value extends OptionAccessorValue<Definition>>(
		target: ClassAccessorDecoratorTarget<This, Value>,
		context: ClassAccessorDecoratorContext<This, Value>,
	): ClassAccessorDecoratorResult<This, Value> {
		if (context.static || context.private || typeof context.name !== 'string') {
			throw new Error('@option must decorate a public instance auto-accessor with a string name');
		}

		if (optionRegistrations.has(target.get)) {
			throw new Error(`Option '${context.name}' is decorated more than once`);
		}

		validateOption(definition.default, definition, context.name);
		optionRegistrations.set(target.get, {...definition, name: context.name});

		const name = context.name;

		return {
			init(value: Value): Value {
				// Validate the actual initializer and keep array defaults independent between instances.
				return validateOption(value, definition, name) as Value;
			},
		};
	};
}

function collectOptions(prototype: object): OptionMetadata[] {
	const options: OptionMetadata[] = [];
	const seenNames = new Set<string>();
	let current: object | null = prototype;

	while (current !== null && current !== Object.prototype) {
		for (const name of Object.getOwnPropertyNames(current)) {
			if (seenNames.has(name)) {
				continue;
			}

			seenNames.add(name);

			const getter = Object.getOwnPropertyDescriptor(current, name)?.get;

			if (getter === undefined) {
				continue;
			}

			const definition = optionRegistrations.get(getter);

			if (definition !== undefined) {
				options.push(definition);
			}
		}

		current = Object.getPrototypeOf(current) as object | null;
	}

	return options;
}

/**
 * Call once from the concrete, most-derived constructor after all option accessors initialize.
 * Reusable base constructors must leave application of overrides to the concrete parser.
 */
export function applyOptions(instance: object, overrides: Readonly<Record<string, unknown>>): void {
	const constructor: unknown = Reflect.get(instance, 'constructor');

	if (typeof constructor !== 'function') {
		throw new Error('Options require a parser instance');
	}

	const definitions = classOptions.get(constructor);

	if (definitions === undefined) {
		throw new Error('Options require a class decorated with @parser');
	}

	for (const name of Object.keys(overrides)) {
		const definition = definitions.find((entry) => entry.name === name);

		if (definition === undefined) {
			throw new Error(`Unknown parser option: ${name}`);
		}

		const value = validateOption(overrides[name], definition, name);

		if (!Reflect.set(instance, name, value)) {
			throw new Error(`Cannot assign parser option: ${name}`);
		}
	}
}

/** IDs identify parser operations, so independently installed URL classes can reference them. */
export function generator<const Parameters extends ParameterDefinitions>(id: string, parameters: Parameters) {
	return function decorateGenerator<This>(
		method: GeneratorMethod<This, Parameters>,
		context: ClassMethodDecoratorContext<This, GeneratorMethod<This, Parameters>>,
	): void {
		if (id.trim() === '') {
			throw new Error('Generator ID must not be empty');
		}

		if (context.static || context.private || typeof context.name !== 'string') {
			throw new Error(`Generator '${id}' must decorate a public instance method with a string name`);
		}

		if (registrations.has(method)) {
			throw new Error(`Method '${context.name}' has multiple @generator decorators`);
		}

		if (discoveryRegistrations.has(method)) {
			throw new Error(`Method '${context.name}' cannot be both a generator and a discovery hook`);
		}

		registrations.set(method, {id, parameters});
	};
}

type DiscoveryMethod<This> = (this: This, input: DiscoveryInput, session: Session) =>
	Promise<DiscoveryResult | null>;

/** Identifies a site's URL class; returning null is an ordinary negative probe result. */
export function discovery(urlClass: string) {
	return function decorateDiscovery<This>(
		method: DiscoveryMethod<This>,
		context: ClassMethodDecoratorContext<This, DiscoveryMethod<This>>,
	): void {
		if (urlClass.trim() === '') {
			throw new Error('Discovery URL-class ID must not be empty');
		}

		if (context.static || context.private || typeof context.name !== 'string') {
			throw new Error('@discovery must decorate a public instance method with a string name');
		}

		if (discoveryRegistrations.has(method) || registrations.has(method)) {
			throw new Error(`Method '${context.name}' already declares a generator or discovery hook`);
		}

		discoveryRegistrations.set(method, urlClass);
	};
}

function collectDiscoveries(prototype: object): DiscoveryMetadata[] {
	const discoveries: DiscoveryMetadata[] = [];
	const seenNames = new Set<string>();
	let current: object | null = prototype;

	while (current !== null && current !== Object.prototype) {
		for (const name of Object.getOwnPropertyNames(current)) {
			if (name === 'constructor' || seenNames.has(name)) {
				continue;
			}

			seenNames.add(name);

			const method: unknown = Object.getOwnPropertyDescriptor(current, name)?.value;

			if (typeof method !== 'function') {
				continue;
			}

			const urlClass = discoveryRegistrations.get(method);

			if (urlClass !== undefined) {
				discoveries.push({urlClass, method: name});
			}
		}

		current = Object.getPrototypeOf(current) as object | null;
	}

	return discoveries;
}

function collectHandlers(prototype: object): GeneratorMetadata[] {
	const handlers: GeneratorMetadata[] = [];
	const seenNames = new Set<string>();
	const seenIds = new Set<string>();
	let current: object | null = prototype;

	while (current !== null && current !== Object.prototype) {
		for (const name of Object.getOwnPropertyNames(current)) {
			if (name === 'constructor' || seenNames.has(name)) {
				continue;
			}

			seenNames.add(name);

			// Inspect descriptors without invoking getters. An override must be redecorated;
			// inherited methods retain their decorations when there is no override.
			const method: unknown = Object.getOwnPropertyDescriptor(current, name)?.value;

			if (typeof method !== 'function') {
				continue;
			}

			const registration = registrations.get(method);

			if (registration === undefined) {
				continue;
			}

			if (seenIds.has(registration.id)) {
				throw new Error(`Duplicate generator ID: ${registration.id}`);
			}

			seenIds.add(registration.id);
			handlers.push({id: registration.id, method: name, parameters: registration.parameters});
		}

		current = Object.getPrototypeOf(current) as object | null;
	}

	return handlers;
}

export interface ParserDefinition {
	readonly id: string;
	readonly version: number;
	readonly cookies?: ParserMetadata[ 'cookies' ];
	readonly urlClasses?: readonly URLClassDefinition[];
}

/** Collects method declarations at module evaluation, without constructing a parser instance. */
export function parser(definition: ParserDefinition) {
	return function decorateParser<Class extends new (site: DomainContext) => object>(
		constructor: Class,
		_context: ClassDecoratorContext<Class>,
	): Class &
		{
			readonly metadata: ParserMetadata
		} {
		if (definition.id.trim() === '') {
			throw new Error('Parser ID must not be empty');
		}

		if (!Number.isSafeInteger(definition.version) || definition.version < 1) {
			throw new Error(`Invalid version for parser '${definition.id}'`);
		}

		const prototype: unknown = Reflect.get(constructor, 'prototype');

		if (typeof prototype !== 'object' || prototype === null) {
			throw new Error('Parser has no prototype');
		}

		const handlers = collectHandlers(prototype);
		const options = collectOptions(prototype);
		const discoveries = collectDiscoveries(prototype);
		const urlClasses = definition.urlClasses ?? [];
		const classIds = new Set<string>();

		for (const urlClass of urlClasses) {
			if (classIds.has(urlClass.id)) {
				throw new Error(`Duplicate bundled URL class: ${urlClass.id}`);
			}

			classIds.add(urlClass.id);

			if (urlClass.parser !== definition.id) {
				throw new Error(`URL class '${urlClass.id}' selects another parser`);
			}

			const generatorIds = new Set<string>();

			for (const generator of urlClass.generators) {
				if (generatorIds.has(generator.id)) {
					throw new Error(`Duplicate generator '${generator.id}' in '${urlClass.id}'`);
				}

				generatorIds.add(generator.id);

				if (!handlers.some((handler) => handler.id === generator.handler)) {
					throw new Error(`URL class '${urlClass.id}' requires missing handler '${generator.handler}'`);
				}
			}

			for (const pattern of urlClass.url_patterns) {
				// Native registry preparation validates and compiles patterns before publishing routes.
				if (!generatorIds.has(pattern.generator)) {
					throw new Error(`Unknown generator '${pattern.generator}' in '${urlClass.id}'`);
				}
			}
		}

		const metadata: ParserMetadata = {
			formatVersion: 1,
			sdkVersion: SDK_VERSION,
			id: definition.id,
			version: definition.version,
			options,
			handlers,
			discoveries,
			...(definition.cookies === undefined ? {} : {cookies: definition.cookies}),
			urlClasses,
		};
		Object.defineProperty(constructor, 'metadata', {value: Object.freeze(metadata), enumerable: true});
		classOptions.set(constructor, options);

		return constructor as Class & { readonly metadata: ParserMetadata };
	};
}
