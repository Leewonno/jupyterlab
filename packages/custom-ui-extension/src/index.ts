/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

/**
 * @packageDocumentation
 * @module custom-ui-extension
 */

import '../style/components/sidebar.css';
import '../style/components/panel.css';
import '../style/components/launcher.css';
import '../style/components/loading.css';
import '../style/components/toolbar.css';

import { stylePlugin } from './style';
import { splashPlugin } from './splash';
import { sidebarPlugin } from './sidebar';
import { toolbarPlugin } from './toolbar';
import { iconPlugin } from './icons';
import { launcherPlugin } from './launcher';
import { menuPlugin } from './menu';
import { placeholderPlugin } from './placeholder';
import { aboutPlugin } from './about';

export default [
  stylePlugin,
  splashPlugin,
  sidebarPlugin,
  toolbarPlugin,
  iconPlugin,
  launcherPlugin,
  menuPlugin,
  placeholderPlugin,
  aboutPlugin
];
