/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import { ILayoutRestorer } from '@jupyterlab/application';
import { ISettingRegistry } from '@jupyterlab/settingregistry';
import { ITranslator, nullTranslator } from '@jupyterlab/translation';
import { listIcon } from '@jupyterlab/ui-components';
import { Widget } from '@lumino/widgets';

class CustomSidebarWidget extends Widget {
  constructor(translator?: ITranslator) {
    super();
    const trans = (translator ?? nullTranslator).load('jupyterlab');
    this.id = 'jp-custom-sidebar';
    this.title.icon = listIcon;
    this.title.caption = trans.__('Custom Sidebar');

    this.node.innerHTML = `
      <div class="jp-CustomSidebar">
        <p class="jp-CustomSidebar-desc">좌측 사이드바 추가 테스트입니다.</p>
      </div>
    `;
  }
}

// 사이드바 Extension 버튼 및 기능 비활성화
export const hideExtensionManagerPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:hide-extension-manager',
  description: 'Disables the Extension Manager from the left sidebar.',
  autoStart: true,
  requires: [ISettingRegistry],
  activate: async (app: JupyterFrontEnd, registry: ISettingRegistry) => {
    try {
      const settings = await registry.load(
        '@jupyterlab/extensionmanager-extension:plugin'
      );
      await settings.set('enabled', false);
    } catch (e) {
      console.warn('Failed to disable Extension Manager:', e);
    }
  }
};

export const sidebarPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:sidebar',
  description: 'Adds a hello world panel to the left sidebar.',
  autoStart: false,
  optional: [ILayoutRestorer, ITranslator],
  activate: (
    app: JupyterFrontEnd,
    restorer: ILayoutRestorer | null,
    translator: ITranslator | null
  ): void => {
    const panel = new CustomSidebarWidget(translator ?? undefined);

    if (restorer) {
      restorer.add(panel, 'custom-sidebar');
    }

    // rank 500 : 맨마지막
    app.shell.add(panel, 'left', { rank: 500 });
  }
};
