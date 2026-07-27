/**
 * Node 22 ships an experimental `localStorage` global that shadows jsdom's and is inert unless started
 * with --localstorage-file. Install a simple in-memory Storage so store tests have real persistence,
 * independent of that. The app in a browser uses the platform localStorage untouched.
 */

const backing = new Map<string, string>();

const memoryStorage: Storage = {
  get length() {
    return backing.size;
  },
  clear: () => backing.clear(),
  getItem: (key) => (backing.has(key) ? (backing.get(key) as string) : null),
  key: (index) => [...backing.keys()][index] ?? null,
  removeItem: (key) => {
    backing.delete(key);
  },
  setItem: (key, value) => {
    backing.set(key, String(value));
  },
};

Object.defineProperty(globalThis, 'localStorage', {
  value: memoryStorage,
  configurable: true,
  writable: true,
});
