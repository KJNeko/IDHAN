import { LoginScreen } from '../auth/LoginScreen';
import { useAuth } from '../auth/AuthProvider';
import { LayoutToolbar } from '../layout/LayoutToolbar';
import { Workspace } from '../layout/Workspace';
import { ToastHost } from '../host/ToastHost';

/**
 * Authenticated shell: the customizable panel workspace. The old /version status page now lives as
 * the Server Status panel, seeded into the default layout.
 */
function AppShell() {
  const { logout } = useAuth();
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
