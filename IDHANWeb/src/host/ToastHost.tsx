/**
 * Renders host.ui.toast() messages. Subscribes to the global toast stream and auto-dismisses each
 * after its timeout. Mounted once near the app root.
 */

import { useEffect, useState } from 'react';
import { globalServices, type Toast } from './globalServices';

export function ToastHost() {
  const [toasts, setToasts] = useState<Toast[]>([]);

  useEffect(() => {
    return globalServices.toasts.subscribe((toast) => {
      setToasts((current) => [...current, toast]);
      window.setTimeout(() => {
        setToasts((current) => current.filter((t) => t.id !== toast.id));
      }, toast.timeoutMs);
    });
  }, []);

  if (toasts.length === 0) return null;

  return (
    <div className="toast-host">
      {toasts.map((toast) => (
        <div key={toast.id} className={`toast toast-${toast.kind}`} role="status">
          {toast.message}
        </div>
      ))}
    </div>
  );
}
