/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import {
  EditorExtensionRegistry,
  IEditorExtensionRegistry
} from '@jupyterlab/codemirror';
import type { IEditorExtensionFactory } from '@jupyterlab/codemirror';
import { placeholder } from '@codemirror/view';

const PLACEHOLDER_TEXT = '코드를 입력하세요.';

/** 코드 셀과 구분 */
const MARKDOWN_MIME = 'text/x-ipythongfm';

/** 노트북 코드 셀이 비어 있을 때 placeholder 안내 문구 */
export const placeholderPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:placeholder',
  description:
    'Shows a placeholder hint inside empty notebook code cell inputs.',
  autoStart: true,
  requires: [IEditorExtensionRegistry],
  activate: (
    _app: JupyterFrontEnd,
    extensions: IEditorExtensionRegistry
  ): void => {
    extensions.addExtension(
      Object.freeze({
        name: '@jupyterlab/custom-ui-extension:placeholder',
        factory: ({ inline, model }: IEditorExtensionFactory.IOptions) => {
          // 노트북/콘솔 셀(인라인) 이외의 전체 문서 에디터는 제외.
          if (!inline || model.mimeType === MARKDOWN_MIME) {
            return null;
          }
          return EditorExtensionRegistry.createImmutableExtension(
            placeholder(PLACEHOLDER_TEXT)
          );
        }
      })
    );
  }
};
