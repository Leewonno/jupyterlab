/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import { Dialog, showDialog } from '@jupyterlab/apputils';
import { IMainMenu } from '@jupyterlab/mainmenu';
import { ITranslator, nullTranslator } from '@jupyterlab/translation';
import type { Menu } from '@lumino/widgets';
import * as React from 'react';

const COMMAND_ID = 'custom-ui:about';
const ORIGINAL_COMMAND_ID = 'help:about';

export const aboutPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:about',
  description: 'Overrides the About dialog with a custom version.',
  autoStart: true,
  requires: [IMainMenu],
  optional: [ITranslator],
  activate: (
    app: JupyterFrontEnd,
    mainMenu: IMainMenu,
    translator: ITranslator | null
  ): void => {
    const { commands } = app;
    const trans = (translator ?? nullTranslator).load('jupyterlab');

    commands.addCommand(COMMAND_ID, {
      label: trans.__('About %1', app.name),
      execute: () => {
        const title = (
          <span className="jp-About-header">
            <div className="jp-About-header-info">
              <h2>{app.name}</h2>
              <div
                className="jp-About-body"
                style={{
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '1rem'
                }}
              >
                <span className="jp-About-version-info">
                  <span className="jp-About-version">Version 0.0.1</span>
                </span>
                <span
                  className="jp-About-copyright"
                  style={{
                    paddingTop: '0'
                  }}
                >
                  © 2026 TILON Co., Ltd. All Rights Reserved.
                </span>
              </div>
            </div>
          </span>
        );

        const body = (
          <div className="jp-About-body">
            <span className="jp-About-copyright"></span>
          </div>
        );

        return showDialog({
          title,
          body,
          buttons: [Dialog.cancelButton({ label: trans.__('Close') })]
        });
      }
    });

    // help:about 메뉴 항목을 custom-ui:about 으로 교체
    void app.restored.then(() => {
      const menu = mainMenu.helpMenu as unknown as Menu;

      const originalItem = menu.items.find(
        item => item.command === ORIGINAL_COMMAND_ID
      );
      if (!originalItem) {
        return;
      }

      const itemIndex = menu.items.indexOf(originalItem);
      menu.removeItem(originalItem);
      menu.insertItem(itemIndex, { command: COMMAND_ID });
    });
  }
};
