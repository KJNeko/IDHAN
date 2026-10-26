/** A mutation invalidates both cached candidates and requests made before it committed. */
export function createLookaheadQueue<T>() {
    let generation = 0;
    let values: T[] = [];
    let running: Promise<void> | null = null;

    return {
        invalidate() {
            generation++;
            values = [];
            running = null;
        },
        take(): T | undefined {
            return values.shift();
        },
        fill(load: () => Promise<T[]>): Promise<void> {
            if (running !== null) return running;
            const started = generation;
            const request = load().then((loaded) => {
                if (started === generation) values = loaded;
            }).finally(() => {
                if (started === generation) running = null;
            });
            running = request;
            return request;
        },
    };
}
