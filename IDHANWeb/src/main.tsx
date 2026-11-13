import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './app/App';
import { AuthProvider } from './auth/AuthProvider';
import { registerBuiltinPanels } from './panels/builtins';
import './theme/global.css';

registerBuiltinPanels();

const root = document.getElementById('root');
if (!root) throw new Error('#root missing from index.html');

createRoot(root).render(
  <StrictMode>
    <AuthProvider>
      <App />
    </AuthProvider>
  </StrictMode>,
);
