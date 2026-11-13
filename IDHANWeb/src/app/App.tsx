import { useEffect } from 'react';
import { LoginScreen } from '../auth/LoginScreen';
import { useAuth } from '../auth/AuthProvider';
import { LayoutToolbar } from '../layout/LayoutToolbar';
import { Workspace } from '../layout/Workspace';
import { useLayoutStore } from '../layout/store';
import { loadPlugins } from '../plugins/loader';
import { ToastHost } from '../host/ToastHost';
import { StaleBuildNotice } from './StaleBuildNotice';

function AppShell() {
  const { logout } = useAuth();

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
      <StaleBuildNotice />
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
