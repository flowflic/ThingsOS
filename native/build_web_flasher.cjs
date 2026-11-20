// SPDX-License-Identifier: Apache-2.0
const path = require('node:path');
const deps = path.resolve(__dirname, '../../.tools/web-deploy/node_modules');
const esbuild = require(path.join(deps, 'esbuild'));
const pkg = require(path.join(deps, 'esptool-js/package.json'));
if (pkg.version !== '0.6.1') throw Error('Requalify esptool-js before changing its version');
esbuild.buildSync({
  entryPoints: [path.join(deps, 'esptool-js/lib/index.js')],
  outfile: path.join(process.argv[2], 'vendor/esptool.js'),
  bundle: true, format: 'esm', platform: 'browser', target: 'es2020', minify: true,
  legalComments: 'eof'
});
