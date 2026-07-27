import { useEffect } from 'react';
import { LoginScreen } from '../auth/LoginScreen';
import { useAuth } from '../auth/AuthProvider';
import { LayoutToolbar } from '../layout/LayoutToolbar';
import { Workspace } from '../layout/Workspace';
import { useLayoutStore } from '../layout/store';
import { loadPlugins } from '../plugins/loader';
import { ToastHost } from '../host/ToastHost';

/**
 * Authenticated shell: the customizable panel workspace. The old /version status page now lives as
 * the Server Status panel, seeded into the default layout.
 */
function AppShell() {
  const { logout } = useAuth();

  // Load third-party panel plugins once, now that we hold a credential for the /plugins call. When any
  // register, bump the catalog so the picker and workspace pick up the new types. Failures are handled
  // inside loadPlugins (warn + skip), so this never blocks the shell.
  useEffect(() => {
    void loadPlugins().then((count) => {
      if (count > 0) useLayoutStore.getState().bumpCatalog();
    });
  }, []);

  return (
    <div className="app">
      <LayoutToolbar onSignOut={() => void logout()} />
      <Workspace />
      <ToastHost />
    </div>
  );
}

export function App() {
  const { status } = useAuth();

  if (status === 'restoring') {
    return (
      <main className="shell">
        <p className="muted">Restoring session…</p>
      </main>
    );
  }
  if (status === 'unauthenticated') return <LoginScreen />;
  return <AppShell />;
}
