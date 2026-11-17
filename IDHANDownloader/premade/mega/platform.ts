import {AbortController, AbortSignal} from "abort-controller/dist/abort-controller.js";

declare const idhan: { delay(milliseconds: number): Promise<void> };
const environment = globalThis as unknown as Record<string, unknown>;
let nextTimer = 0;
const timers = new Set<number>();

function enqueue(callback: () => void): void {
	void Promise.resolve().then(callback);
}

function timer(callback: (...args: unknown[]) => void, milliseconds = 0, ...args: unknown[]): number {
	const id = ++nextTimer;
	timers.add(id);
	const run = () => {
		if (timers.delete(id)) callback(...args);
	};
	if (milliseconds <= 0) enqueue(run);
	else void idhan.delay(Math.min(milliseconds, 300000)).then(run, () => timers.delete(id));
	return id;
}

// MEGAJS's browser stream dependencies consult these globals during module evaluation.
if (!environment.AbortController) environment.AbortController = AbortController;
if (!environment.AbortSignal) environment.AbortSignal = AbortSignal;
if (!environment.self) environment.self = globalThis;
if (!environment.queueMicrotask) environment.queueMicrotask = enqueue;
if (!environment.setTimeout) environment.setTimeout = timer;
if (!environment.clearTimeout) environment.clearTimeout = (id: number) => timers.delete(id);
