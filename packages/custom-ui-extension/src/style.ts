/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';

/**
 * A CSS-only plugin that applies the centralized custom UI styles.
 */
export const stylePlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:plugin',
  description: 'Applies centralized custom UI styles.',
  autoStart: true,
  activate: (_app: JupyterFrontEnd): void => {
    // CSS-only extension — no runtime logic needed.
  }
};
