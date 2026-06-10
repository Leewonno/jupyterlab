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

import '../style/components/sidebar.css';
import '../style/components/panel.css';
import '../style/components/notebook.css';
import '../style/components/launcher.css';

const plugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:plugin',
  description: 'Applies centralized custom UI styles.',
  autoStart: true,
  activate: (_app: JupyterFrontEnd): void => {
    // CSS-only extension — no runtime logic needed.
  }
};

export default [plugin];
