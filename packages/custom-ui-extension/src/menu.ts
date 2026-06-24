/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import type { HelpMenu} from '@jupyterlab/mainmenu';
import { IMainMenu } from '@jupyterlab/mainmenu';
import type { IRankedMenu } from '@jupyterlab/ui-components';
import type { Menu } from '@lumino/widgets';

const STATIC_COMMANDS_TO_REMOVE = new Set([
  'help:jupyter-forum',
  'help:open',
  'inspector:toggle'
]);

const isDynamicKernelCommand = (command: string) =>
  command.startsWith('help-menu-');

export const menuPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:menu',
  description: 'Customizes the main menu.',
  autoStart: true,
  requires: [IMainMenu],
  activate: (app: JupyterFrontEnd, mainMenu: IMainMenu): void => {
    const helpMenu = mainMenu.helpMenu as HelpMenu;

    // 커널 실행 시 나오는 Reference 메뉴 버튼 제거
    const origAddGroup = helpMenu.addGroup.bind(helpMenu);
    helpMenu.addGroup = (items: IRankedMenu.IItemOptions[], rank?: number) => {
      const filtered = items.filter(
        item => !isDynamicKernelCommand(item?.command ?? '')
      );
      return filtered.length > 0
        ? origAddGroup(filtered, rank)
        : { isDisposed: false, dispose(): void {} };
    };

    // 고정된 메뉴 아이템 제거
    void app.restored.then(() => {
      const menu = mainMenu.helpMenu as unknown as Menu;
      const itemsToRemove = menu.items.filter(item =>
        STATIC_COMMANDS_TO_REMOVE.has(item.command)
      );
      for (const item of [...itemsToRemove]) {
        menu.removeItem(item);
      }
    });
  }
};
