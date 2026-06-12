/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

/**
 * @packageDocumentation
 * @module custom-ui-extension
 */

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';

import { ISplashScreen } from '@jupyterlab/apputils';

import '../style/components/sidebar.css';
import '../style/components/panel.css';
import '../style/components/launcher.css';
import '../style/components/loading.css';

const plugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:plugin',
  description: 'Applies centralized custom UI styles.',
  autoStart: true,
  activate: (_app: JupyterFrontEnd): void => {
    // CSS-only extension — no runtime logic needed.
  }
};

const splashPlugin: JupyterFrontEndPlugin<ISplashScreen> = {
  id: '@jupyterlab/custom-ui-extension:splash',
  description: 'Custom logo splash screen.',
  autoStart: true,
  provides: ISplashScreen,
  activate: (app: JupyterFrontEnd): ISplashScreen => {
    const { restored } = app;

    const splash = document.createElement('div');
    splash.id = 'custom-splash';
    splash.innerHTML = `
      <div class="custom-splash-logo">
        <div class="splash-circle splash-tl">
          <svg viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
            <defs>
              <radialGradient id="csg-0" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(122.898 108.26) rotate(90) scale(117.634 117.634)">
                <stop offset="0.5" stop-color="#E05C10"/>
                <stop offset="1" stop-color="#D44516"/>
              </radialGradient>
            </defs>
            <path d="M118.133 236.32C183.413 236.32 236.267 183.413 236.267 118.187C236.267 52.96 183.413 0 118.133 0C52.8533 0 0 52.9067 0 118.133C0 183.36 52.9067 236.267 118.133 236.267V236.32Z" fill="url(#csg-0)"/>
          </svg>
        </div>
        <div class="splash-circle splash-tr">
          <svg viewBox="256 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
            <defs>
              <radialGradient id="csg-1" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(399.277 108.26) rotate(90) scale(117.634 117.634)">
                <stop offset="0.5" stop-color="#F3D623"/>
                <stop offset="1" stop-color="#F5BE1C"/>
              </radialGradient>
            </defs>
            <path d="M393.867 236.32C459.147 236.32 512 183.413 512 118.187C512 52.96 459.093 0 393.867 0C328.64 0 275.733 52.9067 275.733 118.133C275.733 183.36 328.64 236.267 393.867 236.267V236.32Z" fill="url(#csg-1)"/>
          </svg>
        </div>
        <div class="splash-circle splash-bl">
          <svg viewBox="0 256 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
            <defs>
              <radialGradient id="csg-2" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(122.898 385.28) rotate(90) scale(117.634)">
                <stop offset="0.5" stop-color="#0767AD"/>
                <stop offset="1" stop-color="#015086"/>
              </radialGradient>
            </defs>
            <path d="M118.133 512C183.413 512 236.267 459.093 236.267 393.867C236.267 328.64 183.36 275.733 118.133 275.733C52.9067 275.733 0 328.587 0 393.867C0 459.147 52.9067 512 118.133 512Z" fill="url(#csg-2)"/>
          </svg>
        </div>
        <div class="splash-circle splash-br">
          <svg viewBox="256 256 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
            <defs>
              <radialGradient id="csg-3" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(399.864 384.64) rotate(90) scale(117.634)">
                <stop offset="0.5" stop-color="#0E8C41"/>
                <stop offset="1" stop-color="#0E7A3B"/>
              </radialGradient>
            </defs>
            <path d="M393.867 512C459.147 512 512 459.093 512 393.867C512 328.64 459.093 275.733 393.867 275.733C328.64 275.733 275.733 328.64 275.733 393.867C275.733 459.093 328.64 512 393.867 512Z" fill="url(#csg-3)"/>
          </svg>
        </div>
      </div>
    `;

    let splashCount = 0;

    return {
      show: (light = true) => {
        splash.classList.remove('splash-fade');
        splash.classList.toggle('light', light);
        splash.classList.toggle('dark', !light);

        // Re-trigger CSS animations on repeated show
        const circles = splash.querySelectorAll<HTMLElement>('.splash-circle');
        circles.forEach(c => {
          c.style.animation = 'none';
          void c.offsetHeight; // force reflow
          c.style.animation = '';
        });

        splashCount++;
        document.body.appendChild(splash);

        let disposed = false;
        return {
          get isDisposed() {
            return disposed;
          },
          dispose: () => {
            if (disposed) {
              return;
            }
            disposed = true;
            void restored.then(() => {
              if (--splashCount === 0) {
                splash.classList.add('splash-fade');
                window.setTimeout(() => {
                  if (splash.parentNode) {
                    document.body.removeChild(splash);
                  }
                }, 500);
              }
            });
          }
        };
      }
    };
  }
};

export default [plugin, splashPlugin];
