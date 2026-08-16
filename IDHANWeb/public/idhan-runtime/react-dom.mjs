/**
 * Runtime shim for `react-dom`. A panel rarely needs this, since the host owns the render root, but
 * `createPortal` and a few utilities live here. Re-exports the host's instance. See react.mjs.
 */
const ns = globalThis.__IDHAN_RUNTIME__?.reactDom;
if (!ns) throw new Error('[idhan] react-dom not installed before a plugin imported it');

export default ns.default ?? ns;

export const {
  createPortal,
  flushSync,
  preconnect,
  prefetchDNS,
  preinit,
  preinitModule,
  preload,
  preloadModule,
  requestFormReset,
  unstable_batchedUpdates,
  useFormState,
  useFormStatus,
  version,
} = ns;
