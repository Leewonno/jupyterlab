// Copyright (c) Jupyter Development Team.
// Distributed under the terms of the Modified BSD License.

import { expect, test } from '@jupyterlab/galata';
import type { Page } from '@playwright/test';

/**
 * E2E tests for the `@jupyterlab/custom-toolbar-extension` package.
 *
 * The extension adds a "저장 및 제출" button to document toolbars. Clicking it
 * fetches subject/week metadata from a local API, opens a submit dialog, and
 * POSTs the saved file back. Both external endpoints (127.0.0.1:9000) are
 * mocked so the tests do not require the submission server to be running.
 */

const META_URL = /.*127\.0\.0\.1:9000\/meta.*/;
const FILE_URL = /.*127\.0\.0\.1:9000\/file.*/;

const SELECTORS = {
  toolbarButton: '.jp-submit-button',
  dialog: '.jp-Dialog',
  subjectSelect: '.jp-submit-dialog-body select >> nth=0',
  weekSelect: '.jp-submit-dialog-body select >> nth=1',
  filenameInput: '.jp-submit-dialog-body input'
};

/** Mock the metadata endpoint returning two subjects and three weeks. */
async function mockMetadata(page: Page): Promise<void> {
  await page.route(META_URL, route =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({
        subjects: ['파이썬 기초', '자료구조'],
        weeks: [
          { title: '1주차', isDeadline: true }, // 마감되어 선택 불가
          { title: '2주차', isDeadline: false },
          { title: '3주차', isDeadline: false }
        ]
      })
    })
  );
}

/** Collect every native alert message and auto-accept it. */
function collectAlerts(page: Page): string[] {
  const messages: string[] = [];
  page.on('dialog', async dialog => {
    messages.push(dialog.message());
    await dialog.accept();
  });
  return messages;
}

test.describe('custom-toolbar-extension', () => {
  test('adds the submit button to a notebook toolbar', async ({ page }) => {
    await page.notebook.createNew();

    const button = page.locator(SELECTORS.toolbarButton);
    await expect(button).toBeVisible();
    await expect(button).toContainText('저장 및 제출');
  });

  test('opens the submit dialog populated from the metadata API', async ({
    page
  }) => {
    await mockMetadata(page);
    await page.notebook.createNew();

    await page.locator(SELECTORS.toolbarButton).click();

    const dialog = page.locator(SELECTORS.dialog);
    await expect(dialog).toBeVisible();
    await expect(dialog).toContainText('제출');

    // Subjects from the mocked API are present.
    await expect(page.locator(SELECTORS.subjectSelect)).toContainText(
      '파이썬 기초'
    );
    await expect(page.locator(SELECTORS.subjectSelect)).toContainText(
      '자료구조'
    );

    // The deadline week is rendered with a "(마감)" suffix and marked blocked.
    await expect(page.locator(SELECTORS.weekSelect)).toContainText(
      '1주차 (마감)'
    );
    const deadlineOption = page
      .locator(`${SELECTORS.weekSelect} >> option`)
      .filter({ hasText: '1주차 (마감)' });
    await expect(deadlineOption).toHaveClass(/jp-submit-option-disabled/);
    await expect(deadlineOption).toHaveAttribute('data-blocked', 'true');

    // Choosing the deadline week is reverted back to no selection.
    await page.locator(SELECTORS.weekSelect).selectOption('1주차');
    await expect(page.locator(SELECTORS.weekSelect)).toHaveValue('');
  });

  test('blocks submission until every field is filled', async ({ page }) => {
    const alerts = collectAlerts(page);
    await mockMetadata(page);
    await page.notebook.createNew();

    await page.locator(SELECTORS.toolbarButton).click();
    const dialog = page.locator(SELECTORS.dialog);
    await expect(dialog).toBeVisible();

    // Submit without choosing subject/week.
    await page.locator(`${SELECTORS.dialog} .jp-mod-accept`).click();

    // An alert is shown and the dialog stays open (submission blocked).
    await expect
      .poll(() => alerts.length, { timeout: 5000 })
      .toBeGreaterThan(0);
    expect(alerts[alerts.length - 1]).toContain('모두 입력해주세요');
    await expect(dialog).toBeVisible();
  });

  test('submits the file when all fields are valid', async ({ page }) => {
    const alerts = collectAlerts(page);
    await mockMetadata(page);

    let submittedBody: string | null = null;
    await page.route(FILE_URL, route => {
      submittedBody = route.request().postData();
      return route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ saved: ['ok.ipynb'] })
      });
    });

    await page.notebook.createNew();
    await page.locator(SELECTORS.toolbarButton).click();
    await expect(page.locator(SELECTORS.dialog)).toBeVisible();

    await page.locator(SELECTORS.subjectSelect).selectOption('자료구조');
    await page.locator(SELECTORS.weekSelect).selectOption('2주차');
    // Filename is pre-filled with the notebook name (keeps a valid extension).

    await page.locator(`${SELECTORS.dialog} .jp-mod-accept`).click();

    // The dialog closes and the success alert is shown.
    await expect(page.locator(SELECTORS.dialog)).toBeHidden();
    await expect
      .poll(() => alerts.length, { timeout: 5000 })
      .toBeGreaterThan(0);
    expect(alerts[alerts.length - 1]).toContain('제출 완료');

    // The mocked endpoint actually received the multipart payload.
    expect(submittedBody).not.toBeNull();
  });
});
