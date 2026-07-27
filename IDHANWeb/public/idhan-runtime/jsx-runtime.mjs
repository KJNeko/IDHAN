/**
 * Runtime shim for `react/jsx-runtime` — the automatic JSX runtime a built plugin's compiled output
 * imports. Re-exports the host's instance so plugin JSX and host JSX share one React. See react.mjs.
 */
const ns = globalThis.__IDHAN_RUNTIME__?.reactJsxRuntime;
if (!ns) throw new Error('[idhan] react/jsx-runtime not installed before a plugin imported it');

export const { Fragment, jsx, jsxs } = ns;
