/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

/**
 * 런처 커스터마이즈 플러그인
 *   1. 여러 섹션(Notebook, Console 등)을 단일 섹션(시작하기)으로 병합 (첫 섹션에 모든 카드를 모음)
 *   2. 단일 헤더 제목을 "시작하기"(로케일별 번역)로 변경 (아이콘은 창 모양 아이콘으로 변경)
 *   3. 개별 카드 이름을 RENAME_RULES 매핑대로 교체
 */

import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import { ILabShell } from '@jupyterlab/application';
import { ITranslator } from '@jupyterlab/translation';
import { LabIcon } from '@jupyterlab/ui-components';

import appWindowSvg from '../style/icons/app-window.svg';

const appWindowIcon = new LabIcon({
  name: '@jupyterlab/custom-ui-extension:app-window',
  svgstr: appWindowSvg
});

/**
 * 병합된 단일 섹션 헤더 제목의 로케일별 번역.
 */
const HEADER_TITLES: Readonly<Record<string, string>> = {
  ko: '시작하기',
  en: 'Get Started',
  ja: 'はじめる',
  'zh-cn': '开始使用',
  'zh-tw': '開始使用'
};

/**
 * 로케일 코드('ko_KR', 'zh-CN' 등)에 맞는 헤더 제목을 고른다.
 * 정확한 코드 → 기본 언어(zh 는 간체) 순으로 매칭하고, 없으면 영어를 쓴다.
 */
function resolveHeaderTitle(languageCode: string): string {
  const locale = languageCode.toLowerCase().replace(/_/g, '-');
  const base = locale.split('-')[0];
  return (
    HEADER_TITLES[locale] ??
    HEADER_TITLES[base] ??
    (base === 'zh' ? HEADER_TITLES['zh-cn'] : HEADER_TITLES.en)
  );
}

/**
 * 병합된 단일 섹션 헤더의 제목. activate 시 현재 로케일로 결정된다.
 */
let headerTitle = HEADER_TITLES.ko;

/**
 * 카드 이름 변경
 */
const RENAME_RULES: ReadonlyArray<{
  command: string;
  match?: (args: Record<string, unknown>) => boolean;
  label: string;
}> = [
  {
    command: 'fileeditor:create-new',
    match: a => normExt(a.fileExt) === 'py',
    label: 'Python'
  },
  { command: 'notebook:create-new', label: 'Python (Notebook)' },
  { command: 'console:create', label: 'Python (Console)' },
  { command: 'terminal:create-new', label: 'Terminal' },
  {
    command: 'fileeditor:create-new',
    match: a => !a.fileExt,
    label: 'Text File'
  },
  { command: 'fileeditor:create-new-markdown-file', label: 'Markdown File' }
];

/**
 * fileExt 값 정규화
 */
function normExt(value: unknown): string {
  return typeof value === 'string' ? value.replace(/^\./, '') : '';
}

/**
 * 카드의 원본 라벨 읽기(최초 1회 data-launcher-original 에 저장)
 */
function getCardLabel(
  card: HTMLElement
): { node: HTMLElement; original: string } | null {
  const node = card.querySelector<HTMLElement>('.jp-LauncherCard-label p');
  if (!node) {
    return null;
  }
  let original = node.getAttribute('data-launcher-original');
  if (original === null) {
    original = (node.textContent ?? '').trim();
    node.setAttribute('data-launcher-original', original);
  }
  return { node, original };
}

/**
 * 카드에서 command id 와 args 를 읽는다(표준 런처가 data 속성으로 노출)
 */
function getCardCommand(card: HTMLElement): {
  command: string;
  args: Record<string, unknown>;
} {
  const command = card.getAttribute('data-command') ?? '';
  let args: Record<string, unknown>;
  try {
    args = JSON.parse(card.getAttribute('data-launcher-args') ?? '{}');
  } catch {
    args = {};
  }
  return { command, args };
}

/**
 * 하나의 런처 노드(.jp-Launcher)에 병합 + 필터 + 정렬 + 이름 변경을 적용
 */
function applyCustomizations(root: HTMLElement): void {
  const content = root.querySelector<HTMLElement>('.jp-Launcher-content');
  if (!content) {
    return;
  }

  const sections = Array.from(
    content.querySelectorAll<HTMLElement>(':scope > .jp-Launcher-section')
  );
  if (sections.length === 0) {
    return;
  }

  const [first, ...rest] = sections;
  const container = first.querySelector<HTMLElement>(
    '.jp-Launcher-cardContainer'
  );
  if (!container) {
    return;
  }

  // 단일 헤더 제목 교체
  const title = first.querySelector<HTMLElement>('.jp-Launcher-sectionTitle');
  if (title && title.textContent !== headerTitle) {
    title.textContent = headerTitle;
  }

  // 런처 섹션 헤더 아이콘을 app-window 아이콘으로 교체
  const sectionTitle = first.querySelector<HTMLElement>(
    '.jp-Launcher-sectionTitle'
  );
  const iconContainer =
    sectionTitle?.previousElementSibling as HTMLElement | null;
  if (iconContainer && !iconContainer.hasAttribute('data-custom-icon')) {
    const next = appWindowIcon.element({ stylesheet: 'launcherSection' });
    next.setAttribute('data-custom-icon', 'app-window');
    iconContainer.replaceWith(next);
  }

  // 모든 섹션의 카드를 모은다.
  const allCards = Array.from(
    content.querySelectorAll<HTMLElement>('.jp-LauncherCard')
  );

  // RENAME_RULES 순서대로 매칭되는 카드를 골라 (카드, 새 라벨) 목록을 만든다.
  // 한 카드는 가장 먼저 매칭되는 규칙 하나에만 할당된다.
  const placed = new Set<HTMLElement>();
  const ordered: Array<{ card: HTMLElement; label: string }> = [];
  for (const rule of RENAME_RULES) {
    for (const card of allCards) {
      if (placed.has(card)) {
        continue;
      }
      const { command, args } = getCardCommand(card);
      if (command === rule.command && (!rule.match || rule.match(args))) {
        placed.add(card);
        ordered.push({ card, label: rule.label });
      }
    }
  }

  // 매칭이 하나도 안 되면(예: data 속성 누락) 카드를 전부 지우는 대신
  // 원본 런처를 그대로 둔다 — 빈 런처 방지용 안전장치.
  if (ordered.length === 0) {
    return;
  }

  // 규칙에 없는 카드는 제거한다(노출하지 않음).
  for (const card of allCards) {
    if (!placed.has(card)) {
      card.remove();
    }
  }

  // 규칙 순서대로 첫 섹션 컨테이너에 다시 배치하고 이름을 적용한다.
  for (const { card, label } of ordered) {
    const info = getCardLabel(card);
    if (info && info.node.textContent !== label) {
      info.node.textContent = label;
    }
    container.appendChild(card);
  }

  // 비워진 나머지 섹션 제거
  for (const section of rest) {
    section.remove();
  }
}

/**
 * 런처 노드 하나에 대해 MutationObserver 를 붙여 재렌더 시마다 커스터마이즈를
 * 다시 적용한다. 자기 변경으로 인한 무한 루프를 막기 위해 적용 중에는 observer
 * 를 잠시 끊는다.
 *
 * MutationObserver 콜백은 마이크로태스크라서 브라우저 페인트 전에 실행된다.
 * 여기서 rAF 등으로 미루면 원본 런처가 한 프레임 그려진 뒤에야 커스터마이즈가
 * 적용되어 깜빡임이 생기므로 반드시 동기적으로 적용한다.
 */
function enhanceLauncher(node: HTMLElement): MutationObserver {
  const observer = new MutationObserver(() => {
    observer.disconnect();
    try {
      applyCustomizations(node);
    } finally {
      observer.observe(node, { childList: true, subtree: true });
    }
  });
  observer.observe(node, { childList: true, subtree: true });
  applyCustomizations(node);
  return observer;
}

/**
 * 런처 커스터마이즈 플러그인.
 */
export const launcherPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:launcher',
  description:
    'Merges launcher categories into a single section and renames items.',
  autoStart: true,
  optional: [ILabShell, ITranslator],
  activate: (
    app: JupyterFrontEnd,
    labShell: ILabShell | null,
    translator: ITranslator | null
  ): void => {
    headerTitle = resolveHeaderTitle(translator?.languageCode ?? 'ko');

    const observers = new Map<HTMLElement, MutationObserver>();

    const attach = (node: HTMLElement): void => {
      if (!observers.has(node)) {
        observers.set(node, enhanceLauncher(node));
      }
    };

    const detach = (node: HTMLElement): void => {
      const observer = observers.get(node);
      if (observer) {
        observer.disconnect();
        observers.delete(node);
      }
    };

    const collectLaunchers = (node: Node): HTMLElement[] => {
      if (!(node instanceof HTMLElement)) {
        return [];
      }
      if (node.classList.contains('jp-Launcher')) {
        return [node];
      }
      return Array.from(node.querySelectorAll<HTMLElement>('.jp-Launcher'));
    };

    // 셸 전체를 관찰해 런처 노드가 DOM 에 추가되는 즉시(첫 페인트 전에)
    // 커스터마이즈 observer 를 붙인다. layoutModified 는 디바운스되고
    // app.restored 는 복원 후에야 resolve 되어 첫 페인트보다 늦을 수 있으므로
    // 그 시점에 붙이면 원본 런처가 잠깐 보이는 깜빡임이 생긴다.
    const shellObserver = new MutationObserver(records => {
      for (const record of records) {
        for (const removed of record.removedNodes) {
          for (const launcher of collectLaunchers(removed)) {
            detach(launcher);
          }
        }
        for (const added of record.addedNodes) {
          for (const launcher of collectLaunchers(added)) {
            attach(launcher);
          }
        }
      }
    });
    shellObserver.observe(app.shell.node, { childList: true, subtree: true });

    // 이미 DOM 에 존재하는 런처(플러그인이 늦게 활성화된 경우 등)를 위한 보조 스캔.
    const scan = (): void => {
      for (const launcher of app.shell.node.querySelectorAll<HTMLElement>(
        '.jp-Launcher'
      )) {
        attach(launcher);
      }
    };

    if (labShell) {
      labShell.layoutModified.connect(scan);
    }
    void app.restored.then(scan);
  }
};
