// Copyright (c) Jupyter Development Team.
// Distributed under the terms of the Modified BSD License.
/**
 * @packageDocumentation
 * @module custom-toolbar-extension
 */

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import type { IDocumentWidget } from '@jupyterlab/docregistry';
import { IEditorTracker } from '@jupyterlab/fileeditor';
import { INotebookTracker } from '@jupyterlab/notebook';
import type { Contents } from '@jupyterlab/services';
import { ToolbarButton } from '@jupyterlab/ui-components';

const SUBMIT_API_URL = 'http://127.0.0.1:9000/file/';
const CONFIRM_MESSAGE =
  '파일이 저장 후 전송됩니다. 제출 후에는 수정할 수 없습니다. 계속하시겠습니까?';

async function submitFile(
  path: string,
  content: string,
  mimeType: string
): Promise<void> {
  const filename = path.split('/').pop() ?? path;
  const formData = new FormData();
  formData.append('files', new Blob([content], { type: mimeType }), filename);

  const response = await fetch(SUBMIT_API_URL, {
    method: 'POST',
    body: formData
  });

  if (!response.ok) {
    throw new Error(`서버 오류: ${response.status} ${response.statusText}`);
  }

  const result = await response.json();
  alert(`제출 완료 (${result.saved?.length ?? 0}개 파일)`);
}

// JupyterLab 내장 클래스를 통해 버튼 생성
function makeButton(onClick: () => Promise<void>): ToolbarButton {
  return new ToolbarButton({
    label: '저장 및 제출',
    tooltip: '현재 파일 제출',
    className: 'jp-submit-button',
    onClick
  });
}

// 버튼 생성 및 이벤트 등록
function addButton(
  contents: Contents.IManager,
  panel: IDocumentWidget,
  fileType: 'ipynb' | 'etc'
): void {
  const button = makeButton(async () => {
    const context = panel.context;
    const path = context?.path;
    if (!path) {
      alert('제출할 파일이 없습니다.');
      return;
    }
    if (!confirm(CONFIRM_MESSAGE)) return;
    try {
      await context.save();
      const model = await contents.get(path, { content: true });
      if (fileType === 'ipynb') {
        const content = JSON.stringify(model.content, null, 2);
        await submitFile(path, content, 'application/json');
      } else {
        await submitFile(path, model.content as string, 'text/plain');
      }
    } catch (err) {
      alert(`제출 실패: ${err instanceof Error ? err.message : String(err)}`);
    }
  });
  button.node.style.marginLeft = 'auto';
  panel.toolbar.addItem('submitButton', button);
}

/**
 * A plugin that adds a submit button to the toolbar of all document types.
 */
const plugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-toolbar-extension:plugin',
  description:
    'Sends the currently active document to the submission API when clicked.',
  autoStart: true,
  requires: [INotebookTracker, IEditorTracker],
  activate: (
    app: JupyterFrontEnd,
    notebookTracker: INotebookTracker,
    editorTracker: IEditorTracker
  ): void => {
    const contents = app.serviceManager.contents;

    // Notebook panels (.ipynb)
    notebookTracker.widgetAdded.connect((_, panel) =>
      addButton(contents, panel, 'ipynb')
    );

    // File editor panels (.js, .md, .py, .txt, etc.)
    editorTracker.widgetAdded.connect((_, panel) =>
      addButton(contents, panel, 'etc')
    );
  }
};

export default [plugin];
