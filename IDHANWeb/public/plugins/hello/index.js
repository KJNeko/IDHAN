/**
 * Example IDHAN WebUI plugin.
 *
 * Hand-written plain JS, with no JSX and no build step, so it can be served and loaded verbatim. A
 * real plugin would be authored in TSX and built with `react`, `react-dom`, `react/jsx-runtime` and
 * `@idhan/host` marked external; the host's import map resolves those bare specifiers at runtime.
 *
 * `react` and `@idhan/host` below resolve through that import map to the host's own instances, so
 * this shares the host's single React and registers into the host's catalog.
 */

import { createElement as h, useState } from 'react';
import { registerPanel } from '@idhan/host';

function HelloPanel({ host }) {
  const [count, setCount] = useState(0);

  return h(
    'div',
    { className: 'panel-body', style: { padding: '1rem', display: 'flex', flexDirection: 'column', gap: '0.5rem' } },
    h('h2', null, 'Hello from a plugin! 👋'),
    h('p', { className: 'muted' }, 'This panel was loaded at runtime from ', h('code', null, '/plugins/hello/index.js'), '.'),
    h('p', { className: 'muted' }, 'Instance id: ', h('code', null, host.instanceId)),
    h(
      'button',
      {
        type: 'button',
        className: 'toolbar-button',
        style: { alignSelf: 'flex-start' },
        onClick: () => {
          setCount((c) => c + 1);
          host.ui.toast('Hello from the example plugin', { kind: 'success' });
        },
      },
      `Clicked ${count} time${count === 1 ? '' : 's'}`,
    ),
  );
}

registerPanel({
  type: 'example.hello',
  title: 'Hello',
  description: 'Example plugin panel demonstrating runtime loading and the host API.',
  component: HelloPanel,
});
