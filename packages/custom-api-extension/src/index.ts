// Copyright (c) Jupyter Development Team.
// Distributed under the terms of the Modified BSD License.

/* 외부 서버와 통신을 위한 중앙 관리 영역 */

/**
 * @packageDocumentation
 * @module custom-api-extension
 */

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import {
  IDocumentManager,
  IDocumentWidgetOpener
} from '@jupyterlab/docmanager';
import { CustomApi } from './api';
import { ICustomApi } from './tokens';

export * from './tokens';
export { CustomApi } from './api';

// ===============================================
//  중앙 API 클라이언트를 제공하는 플러그인
// ===============================================
const apiPlugin: JupyterFrontEndPlugin<ICustomApi> = {
  id: '@jupyterlab/custom-api-extension:api',
  description: 'Provides a central client for all external API connections.',
  autoStart: true,
  provides: ICustomApi,
  activate: (_app: JupyterFrontEnd): ICustomApi => {
    return new CustomApi();
  }
};

// ===============================================
// 파일 제출 여부 확인 플러그인
// ===============================================
const fileOpenReporterPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-api-extension:file-open-reporter',
  description: 'Reports opened file names to the external server.',
  autoStart: false,
  requires: [ICustomApi, IDocumentManager, IDocumentWidgetOpener],
  activate: (
    _app: JupyterFrontEnd,
    api: ICustomApi,
    docManager: IDocumentManager,
    widgetOpener: IDocumentWidgetOpener
  ): void => {
    widgetOpener.opened.connect((_, widget) => {
      const context = docManager.contextForWidget(widget);
      if (!context) {
        return;
      }
      void api
        .checkSubmitFile({
          path: context.path,
          name: context.contentsModel?.name ?? context.path,
          openedAt: new Date().toISOString()
        })
        .catch(err => {
          console.error('Failed to report opened file:', err);
        });
    });
  }
};

export default [apiPlugin, fileOpenReporterPlugin];
