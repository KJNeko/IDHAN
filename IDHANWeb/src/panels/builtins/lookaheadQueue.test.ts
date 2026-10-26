import {describe, expect, it, vi} from 'vitest';
import {createLookaheadQueue} from './lookaheadQueue';

function deferred<T>() {
    let resolve!: (value: T) => void;
    let reject!: (reason: Error) => void;
    const promise = new Promise<T>((done, fail) => { resolve = done; reject = fail; });
    return {promise, resolve, reject};
}

describe('relationship lookahead invalidation', () => {
    it('discards candidates resolved by a group-level ruling', async () => {
        const queue = createLookaheadQueue<number>();
        await queue.fill(async () => [1, 2]);
        queue.invalidate();
        expect(queue.take()).toBeUndefined();
        await queue.fill(async () => [3]);
        expect(queue.take()).toBe(3);
    });

    it('ignores an old response arriving after a fresh response', async () => {
        const queue = createLookaheadQueue<number>();
        const stale = deferred<number[]>();
        const beforeRuling = queue.fill(() => stale.promise);
        queue.invalidate();
        await queue.fill(async () => [3]);
        stale.resolve([1, 2]);
        await beforeRuling;
        expect(queue.take()).toBe(3);
        expect(queue.take()).toBeUndefined();
    });

    it('does not let an old request clear the newer in-flight request', async () => {
        const queue = createLookaheadQueue<number>();
        const stale = deferred<number[]>();
        const fresh = deferred<number[]>();
        const beforeRuling = queue.fill(() => stale.promise);
        queue.invalidate();
        const afterRuling = queue.fill(() => fresh.promise);
        stale.resolve([1]);
        await beforeRuling;
        const redundant = vi.fn(async () => [4]);
        expect(queue.fill(redundant)).toBe(afterRuling);
        expect(redundant).not.toHaveBeenCalled();
        fresh.resolve([3]);
        await afterRuling;
        expect(queue.take()).toBe(3);
    });

    it('reports failures and permits retrying', async () => {
        const queue = createLookaheadQueue<number>();
        await expect(queue.fill(async () => { throw new Error('offline'); })).rejects.toThrow('offline');
        await queue.fill(async () => [1]);
        expect(queue.take()).toBe(1);
    });
});
