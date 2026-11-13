import { useEffect, useState } from 'react';
import { globalServices, type Toast } from './globalServices';

export function ToastHost() {
  const [toasts, setToasts] = useState<Toast[]>([]);

  useEffect(() => {
      const timers = new Set<number>();
      const unsubscribe = globalServices.toasts.subscribe((toast) => {
      setToasts((current) => [...current, toast]);
          if (toast.timeoutMs > 0) {
              const timer = window.setTimeout(() => {
                  timers.delete(timer);
                  setToasts((current) => current.filter((t) => t.id !== toast.id));
              }, toast.timeoutMs);
              timers.add(timer);
          }
    });
      return () => {
          unsubscribe();
          for (const timer of timers) window.clearTimeout(timer);
      };
  }, []);

  if (toasts.length === 0) return null;

  return (
    <div className="toast-host">
      {toasts.map((toast) => (
          <div key={toast.id} className={`toast toast-${toast.kind}`}
               role={toast.kind === 'error' ? 'alert' : 'status'}>
              <span>{toast.message}</span>
              <button type="button" className="toast-dismiss" aria-label="Dismiss notification"
                      onClick={() => setToasts(current => current.filter(entry => entry.id !== toast.id))}>×
              </button>
        </div>
      ))}
    </div>
  );
}
