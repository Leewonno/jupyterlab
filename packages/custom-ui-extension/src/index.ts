/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

/**
 * @packageDocumentation
 * @module custom-ui-extension
 *
 * Centralized package for custom UI additions (styles, splash screen,
 * sidebar panel, toolbar submit button). Each feature is a separate plugin
 * so it can be enabled/disabled independently via its plugin id.
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

export default [stylePlugin, splashPlugin, sidebarPlugin, toolbarPlugin];
