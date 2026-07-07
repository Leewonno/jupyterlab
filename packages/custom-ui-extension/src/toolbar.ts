/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import { Dialog } from '@jupyterlab/apputils';
import { ICustomApi } from '@jupyterlab/custom-api-extension';
import type { ISubmitMetadata } from '@jupyterlab/custom-api-extension';
import type { DocumentRegistry } from '@jupyterlab/docregistry';
import type { IDocumentWidget } from '@jupyterlab/docregistry';
import { IEditorTracker } from '@jupyterlab/fileeditor';
import { INotebookTracker } from '@jupyterlab/notebook';
import { ToolbarButton } from '@jupyterlab/ui-components';
import { Widget } from '@lumino/widgets';
import { nullTranslator } from '@jupyterlab/translation';

/** select option */
interface IOption {
  value: string;
  label: string;
  disabled: boolean;
}

/** 모달에서 사용자가 입력/선택한 값 */
interface ISubmitValue {
  subject: string;
  week: string;
  filename: string;
}

/** 제출 모달의 본문 위젯 (과목명 select, 주차 select, 파일명 input) */
class SubmitDialogBody
  extends Widget
  implements Dialog.IBodyWidget<ISubmitValue>
{
  constructor(metadata: ISubmitMetadata, defaultFilename: string) {
    super();
    this.addClass('jp-submit-dialog-body');

    const note = document.createElement('p');
    note.className = 'jp-submit-dialog-note';
    note.textContent =
      '파일이 저장 후 전송됩니다. 제출 후에는 수정할 수 없습니다.';
    this.node.appendChild(note);

    this._subject = SubmitDialogBody._createSelect(
      '과목명',
      metadata.subjects.map(subject => ({
        value: subject,
        label: subject,
        disabled: false
      }))
    );
    this._week = SubmitDialogBody._createSelect(
      '주차',
      metadata.weeks.map(week => ({
        value: week.title,
        // 마감된 주차는 라벨에 표시하고 선택을 막는다.
        label: week.isDeadline ? `${week.title} (마감)` : week.title,
        disabled: week.isDeadline
      }))
    );
    this._filename = SubmitDialogBody._createInput('파일명', defaultFilename);

    this.node.appendChild(this._subject.field);
    this.node.appendChild(this._week.field);
    this.node.appendChild(this._filename.field);
  }

  getValue(): ISubmitValue {
    return {
      subject: this._subject.element.value,
      week: this._week.element.value,
      filename: this._filename.element.value.trim()
    };
  }

  /** label + select 묶음을 생성한다. */
  private static _createSelect(
    label: string,
    options: IOption[]
  ): { field: HTMLElement; element: HTMLSelectElement } {
    const field = document.createElement('label');
    field.className = 'jp-submit-dialog-field';
    field.textContent = label;

    const select = document.createElement('select');
    select.classList.add('jp-mod-styled');

    const placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = '선택하세요';
    placeholder.disabled = true;
    placeholder.selected = true;
    select.appendChild(placeholder);

    for (const option of options) {
      const opt = document.createElement('option');
      opt.value = option.value;
      opt.textContent = option.label;
      if (option.disabled) {
        opt.classList.add('jp-submit-option-disabled');
        opt.dataset.blocked = 'true';
      }
      select.appendChild(opt);
    }

    // 선택 불가(마감) 옵션을 고르면 placeholder 로 되돌린다.
    select.addEventListener('change', () => {
      const selected = select.options[select.selectedIndex];
      if (selected?.dataset.blocked === 'true') {
        select.value = '';
      }
    });

    field.appendChild(select);
    return { field, element: select };
  }

  /** label + text input 묶음을 생성한다. */
  private static _createInput(
    label: string,
    value: string
  ): { field: HTMLElement; element: HTMLInputElement } {
    const field = document.createElement('label');
    field.className = 'jp-submit-dialog-field';
    field.textContent = label;

    const input = document.createElement('input');
    input.type = 'text';
    input.classList.add('jp-mod-styled');
    input.value = value;

    field.appendChild(input);
    return { field, element: input };
  }

  private _subject: { field: HTMLElement; element: HTMLSelectElement };
  private _week: { field: HTMLElement; element: HTMLSelectElement };
  private _filename: { field: HTMLElement; element: HTMLInputElement };
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

// 제출 버튼을 눌러도 입력이 비어 있으면 제출 X (모달 닫히면 안됨)
// 오버라이드하는 이유, 기존 showDialog는 제출 버튼 누르면 모달이 무조건 닫힘 (입력값 검증 실패해도 닫힘)
class SubmitDialog extends Dialog<ISubmitValue> {
  constructor(
    private _form: SubmitDialogBody,
    private _buttonList: ReadonlyArray<Dialog.IButton>,
    options: Partial<Dialog.IOptions<ISubmitValue>>
  ) {
    super(options);
  }

  resolve(index: number): void {
    const button = this._buttonList[index];
    if (button?.accept) {
      const { subject, week, filename } = this._form.getValue();

      /* 파일 확장자 입력 확인 */
      const lastDotIndex = filename.lastIndexOf('.');
      if (lastDotIndex <= 0 || lastDotIndex === filename.length - 1) {
        alert('확장자를 포함한 파일명을 입력해주세요.');
        return;
      }

      /* 확장자 앞 텍스트 입력 확인 */
      const sliceFilename = filename.slice(0, lastDotIndex);

      // 과목명, 주차, 파일명 검증
      if (!subject || !week || !sliceFilename) {
        alert('과목명, 주차, 파일명을 모두 입력해주세요.');
        return;
      }
    }
    super.resolve(index);
  }
}

/** 제출 모달을 열고 사용자가 입력한 값을 반환 */
async function promptSubmit(
  api: ICustomApi,
  defaultFilename: string
): Promise<ISubmitValue | null> {
  const metadata = await api.getSubmitMetadata();
  const body = new SubmitDialogBody(metadata, defaultFilename);
  const trans = (Dialog.translator ?? nullTranslator).load('jupyterlab');
  const buttons = [
    Dialog.cancelButton({ label: trans.__('취소') }),
    Dialog.okButton({ label: trans.__('제출') })
  ];
  const result = await new SubmitDialog(body, buttons, {
    title: '제출',
    body,
    buttons,
    defaultButton: buttons.length - 1
  }).launch();

  if (!result.button.accept || !result.value) return null;

  return result.value;
}

// 버튼 생성 및 이벤트 등록
function addButton(
  api: ICustomApi,
  panel: IDocumentWidget,
  fileType: 'ipynb' | 'etc'
): void {
  const button = makeButton(async () => {
    const context = panel.context as DocumentRegistry.Context;
    const path = context?.path;
    if (!path) {
      alert('제출할 파일이 열려있지 않습니다.');
      return;
    }
    try {
      const currentName = path.split('/').pop() ?? path;
      const value = await promptSubmit(api, currentName);
      if (!value) return;
      const { subject, week, filename } = value;

      // 파일명 변경 시 바뀐 이름으로 Rename
      if (filename !== currentName) {
        await context.rename(filename);
      }
      await context.save();

      // 저장된 파일 내용을 메타데이터와 함께 서버로 전송
      const submittedName = context.path.split('/').pop() ?? context.path;
      const content = context.model.toString();
      const mimeType =
        context.contentsModel?.mimetype ??
        (fileType === 'ipynb' ? 'application/json' : 'text/plain');
      await api.submitFile(submittedName, content, mimeType, { subject, week });

      alert(`제출 완료`);
    } catch (error) {
      console.error(error);
      alert(`제출 실패`);
    }
  });
  // 오른쪽 정렬
  button.node.style.marginLeft = 'auto';
  panel.toolbar.addItem('submitButton', button);
}

// 노트북, 파일 열릴 때 감지해서 필요한 버튼 주입
export const toolbarPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:toolbar',
  description:
    'Sends the currently active document to the submission API when clicked.',
  autoStart: false,
  requires: [INotebookTracker, IEditorTracker, ICustomApi],
  activate: (
    _app: JupyterFrontEnd,
    notebookTracker: INotebookTracker,
    editorTracker: IEditorTracker,
    api: ICustomApi
  ): void => {
    // Notebook panels (.ipynb)
    notebookTracker.widgetAdded.connect((_, panel) =>
      addButton(api, panel, 'ipynb')
    );

    // File editor panels (.js, .md, .py, .txt, etc.)
    editorTracker.widgetAdded.connect((_, panel) =>
      addButton(api, panel, 'etc')
    );
  }
};
