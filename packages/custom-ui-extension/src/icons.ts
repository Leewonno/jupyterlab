/*-----------------------------------------------------------------------------
| Copyright (c) Jupyter Development Team.
| Distributed under the terms of the Modified BSD License.
|----------------------------------------------------------------------------*/

/**
 * Overrides the SVG of selected built-in JupyterLab icons with custom artwork.
 *
 * How it works: every JupyterLab icon is a shared `LabIcon` instance. Setting
 * `icon.svgstr` swaps the artwork everywhere that icon is rendered (sidebar,
 * toolbars, file browser, tabs, status bar, ...), including already-mounted
 * icons. We only touch icons here, never the upstream `ui-components` sources,
 * so this stays merge-clean against upstream.
 *
 * To customize an icon: replace the matching file in `style/icons/<name>.svg`
 * with your own SVG and rebuild. To stop overriding one, delete its line in
 * `OVERRIDES` below (and optionally its svg + import).
 */

import {
  addAboveIcon,
  addBelowIcon,
  addIcon,
  bellIcon,
  bugDotIcon,
  bugIcon,
  caretDownIcon,
  caretLeftIcon,
  caretRightIcon,
  caretUpIcon,
  caseSensitiveIcon,
  checkIcon,
  clearIcon,
  closeIcon,
  collapseAllIcon,
  consoleIcon,
  copyIcon,
  cutIcon,
  deleteIcon,
  dotsIcon,
  downloadIcon,
  duplicateIcon,
  editIcon,
  ellipsesIcon,
  expandAllIcon,
  extensionIcon,
  fastForwardIcon,
  fileIcon,
  fileUploadIcon,
  filterIcon,
  folderIcon,
  html5Icon,
  imageIcon,
  inspectorIcon,
  jsonIcon,
  jupyterFaviconIcon,
  jupyterIcon,
  jupyterlabWordmarkIcon,
  kernelIcon,
  LabIcon,
  launcherIcon,
  launchIcon,
  lineFormIcon,
  linkIcon,
  markdownIcon,
  moveDownIcon,
  moveUpIcon,
  newFolderIcon,
  notebookIcon,
  notTrustedIcon,
  paletteIcon,
  pasteIcon,
  pdfIcon,
  pythonIcon,
  redoIcon,
  refreshIcon,
  regexIcon,
  runIcon,
  runningIcon,
  saveIcon,
  searchIcon,
  settingsIcon,
  spreadsheetIcon,
  stopIcon,
  terminalIcon,
  textEditorIcon,
  tocIcon,
  trustedIcon,
  undoIcon,
  wordIcon,
  yamlIcon
} from '@jupyterlab/ui-components';
import type {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';

import folderSvg from '../style/icons/folder.svg';
import runningSvg from '../style/icons/running.svg';
import paletteSvg from '../style/icons/palette.svg';
import extensionSvg from '../style/icons/extension.svg';
import tocSvg from '../style/icons/toc.svg';
import bugSvg from '../style/icons/bug.svg';
import bugDotSvg from '../style/icons/bug-dot.svg';
import inspectorSvg from '../style/icons/inspector.svg';
import saveSvg from '../style/icons/save.svg';
import addSvg from '../style/icons/add.svg';
import cutSvg from '../style/icons/cut.svg';
import copySvg from '../style/icons/copy.svg';
import pasteSvg from '../style/icons/paste.svg';
import runSvg from '../style/icons/run.svg';
import stopSvg from '../style/icons/stop.svg';
import fastforwardSvg from '../style/icons/fast-forward.svg';
import refreshSvg from '../style/icons/refresh.svg';
import newfolderSvg from '../style/icons/new-folder.svg';
import fileuploadSvg from '../style/icons/file-upload.svg';
import downloadSvg from '../style/icons/download.svg';
import undoSvg from '../style/icons/undo.svg';
import redoSvg from '../style/icons/redo.svg';
import clearSvg from '../style/icons/clear.svg';
import editSvg from '../style/icons/edit.svg';
import duplicateSvg from '../style/icons/duplicate.svg';
import deleteSvg from '../style/icons/delete.svg';
import addaboveSvg from '../style/icons/add-above.svg';
import addbelowSvg from '../style/icons/add-below.svg';
import moveupSvg from '../style/icons/move-up.svg';
import movedownSvg from '../style/icons/move-down.svg';
import caretupSvg from '../style/icons/caret-up.svg';
import caretdownSvg from '../style/icons/caret-down.svg';
import caretleftSvg from '../style/icons/caret-left.svg';
import caretrightSvg from '../style/icons/caret-right.svg';
import closeSvg from '../style/icons/close.svg';
import searchSvg from '../style/icons/search.svg';
import launchSvg from '../style/icons/launch.svg';
import linkSvg from '../style/icons/link.svg';
import checkSvg from '../style/icons/check.svg';
import ellipsesSvg from '../style/icons/ellipses.svg';
import dotsSvg from '../style/icons/dots.svg';
import expandallSvg from '../style/icons/expand-all.svg';
import collapseallSvg from '../style/icons/collapse-all.svg';
import filterSvg from '../style/icons/filter.svg';
import notebookSvg from '../style/icons/notebook.svg';
import fileSvg from '../style/icons/file.svg';
import pythonSvg from '../style/icons/python.svg';
import jsonSvg from '../style/icons/json.svg';
import markdownSvg from '../style/icons/markdown.svg';
import texteditorSvg from '../style/icons/text-editor.svg';
import consoleSvg from '../style/icons/console.svg';
import imageSvg from '../style/icons/image.svg';
import pdfSvg from '../style/icons/pdf.svg';
import spreadsheetSvg from '../style/icons/spreadsheet.svg';
import yamlSvg from '../style/icons/yaml.svg';
import html5Svg from '../style/icons/html5.svg';
import launcherSvg from '../style/icons/launcher.svg';
import kernelSvg from '../style/icons/kernel.svg';
import trustedSvg from '../style/icons/trusted.svg';
import nottrustedSvg from '../style/icons/not-trusted.svg';
import lineformSvg from '../style/icons/line-form.svg';
import bellSvg from '../style/icons/bell.svg';
import jupyterSvg from '../style/icons/jupyter.svg';
import jupyterlabwordmarkSvg from '../style/icons/jupyterlab-wordmark.svg';
import jupyterfaviconSvg from '../style/icons/jupyter-favicon.svg';
import settingsSvg from '../style/icons/settings.svg';
import terminalSvg from '../style/icons/terminal.svg';
import casesensitiveSvg from '../style/icons/case-sensitive.svg';
import regexSvg from '../style/icons/regex.svg';
import wordSvg from '../style/icons/word.svg';

const propertyInspectorIcon = new LabIcon({
  name: '@jupyterlab/custom-ui-extension:property-inspector',
  svgstr: settingsSvg
});

/** [built-in icon, custom svg string] pairs to override. */
const OVERRIDES: Array<[LabIcon, string]> = [
  // Sidebar tabs
  [folderIcon, folderSvg],
  [runningIcon, runningSvg],
  [paletteIcon, paletteSvg],
  [extensionIcon, extensionSvg],
  [tocIcon, tocSvg],
  [bugIcon, bugSvg],
  [bugDotIcon, bugDotSvg],
  [inspectorIcon, inspectorSvg],
  // Notebook/file toolbar
  [saveIcon, saveSvg],
  [addIcon, addSvg],
  [cutIcon, cutSvg],
  [copyIcon, copySvg],
  [pasteIcon, pasteSvg],
  [runIcon, runSvg],
  [stopIcon, stopSvg],
  [fastForwardIcon, fastforwardSvg],
  [refreshIcon, refreshSvg],
  // File browser toolbar
  [newFolderIcon, newfolderSvg],
  [fileUploadIcon, fileuploadSvg],
  [downloadIcon, downloadSvg],
  // Editing
  [undoIcon, undoSvg],
  [redoIcon, redoSvg],
  [clearIcon, clearSvg],
  [editIcon, editSvg],
  [duplicateIcon, duplicateSvg],
  [deleteIcon, deleteSvg],
  // Cell ops
  [addAboveIcon, addaboveSvg],
  [addBelowIcon, addbelowSvg],
  [moveUpIcon, moveupSvg],
  [moveDownIcon, movedownSvg],
  // Arrows
  [caretUpIcon, caretupSvg],
  [caretDownIcon, caretdownSvg],
  [caretLeftIcon, caretleftSvg],
  [caretRightIcon, caretrightSvg],
  // General
  [closeIcon, closeSvg],
  [searchIcon, searchSvg],
  [launchIcon, launchSvg],
  [linkIcon, linkSvg],
  [checkIcon, checkSvg],
  [ellipsesIcon, ellipsesSvg],
  [dotsIcon, dotsSvg],
  [expandAllIcon, expandallSvg],
  [collapseAllIcon, collapseallSvg],
  [filterIcon, filterSvg],
  // Filetypes
  [notebookIcon, notebookSvg],
  [fileIcon, fileSvg],
  [pythonIcon, pythonSvg],
  [jsonIcon, jsonSvg],
  [markdownIcon, markdownSvg],
  [textEditorIcon, texteditorSvg],
  [consoleIcon, consoleSvg],
  [imageIcon, imageSvg],
  [pdfIcon, pdfSvg],
  [spreadsheetIcon, spreadsheetSvg],
  [yamlIcon, yamlSvg],
  [html5Icon, html5Svg],
  [launcherIcon, launcherSvg],
  // Status bar
  [kernelIcon, kernelSvg],
  [trustedIcon, trustedSvg],
  [notTrustedIcon, nottrustedSvg],
  [lineFormIcon, lineformSvg],
  [bellIcon, bellSvg],
  // Logo
  [jupyterIcon, jupyterSvg],
  [jupyterlabWordmarkIcon, jupyterlabwordmarkSvg],
  [jupyterFaviconIcon, jupyterfaviconSvg],
  // Settings/terminal
  [settingsIcon, settingsSvg],
  [terminalIcon, terminalSvg],
  // Search overlay
  [caseSensitiveIcon, casesensitiveSvg],
  [regexIcon, regexSvg],
  [wordIcon, wordSvg]
];

export const iconPlugin: JupyterFrontEndPlugin<void> = {
  id: '@jupyterlab/custom-ui-extension:icons',
  description: 'Overrides built-in icon SVGs with custom artwork.',
  autoStart: true,
  activate: (app: JupyterFrontEnd): void => {
    for (const [icon, svgstr] of OVERRIDES) {
      icon.svgstr = svgstr;
    }

    // Property Inspector uses the shared buildIcon, which is also used by
    // Notebook Tools. Override only this sidebar widget to avoid changing both.
    void app.started.then(() => {
      const propertyInspector = Array.from(app.shell.widgets('right')).find(
        widget => widget.id === 'jp-property-inspector'
      );

      if (propertyInspector) {
        propertyInspector.title.icon = propertyInspectorIcon;
      }
    });
  }
};
