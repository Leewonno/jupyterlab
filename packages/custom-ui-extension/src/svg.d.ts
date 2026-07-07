// Copyright (c) Jupyter Development Team.
// Distributed under the terms of the Modified BSD License.

// Allows importing svg files as raw strings, e.g. `import xSvg from './x.svg'`.
// The app bundler (rspack) resolves these via raw-loader; this declaration only
// satisfies the TypeScript compiler. Mirrors `ui-components/src/svg.d.ts`.

declare module '*.svg' {
  const value: string;
  export default value;
}
