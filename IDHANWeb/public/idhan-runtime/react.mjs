/**
 * Runtime shim: hands plugins the host application's own React instance.
 *
 * The host installs its React on `window.__IDHAN_RUNTIME__` at startup (src/plugins/runtime.ts), and
 * the page's import map (index.html) points the bare `react` specifier at this file. Plugins share
 * the host's single React that way, since two copies in one page break hooks. This file is a stable
 * public asset with the same URL in dev and prod, so the import map never chases Vite's hashed URLs.
 *
 * Not for application code. Only dynamically-loaded plugin bundles resolve through here.
 */
const ns = globalThis.__IDHAN_RUNTIME__?.react;
if (!ns) throw new Error('[idhan] React runtime not installed before a plugin imported "react"');

export default ns.default ?? ns;

export const {
  Children,
  Component,
  Fragment,
  Profiler,
  PureComponent,
  StrictMode,
  Suspense,
  cloneElement,
  createContext,
  createElement,
  createRef,
  forwardRef,
  isValidElement,
  lazy,
  memo,
  startTransition,
  use,
  useActionState,
  useCallback,
  useContext,
  useDebugValue,
  useDeferredValue,
  useEffect,
  useId,
  useImperativeHandle,
  useInsertionEffect,
  useLayoutEffect,
  useMemo,
  useOptimistic,
  useReducer,
  useRef,
  useState,
  useSyncExternalStore,
  useTransition,
  version,
} = ns;
