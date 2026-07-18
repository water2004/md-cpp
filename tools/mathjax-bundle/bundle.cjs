const esbuild = require('esbuild');
const fs = require('fs');
const path = require('path');

esbuild.buildSync({
  entryPoints: ['entry.cjs'],
  bundle: true,
  platform: 'neutral',
  format: 'iife',
  target: 'es2020',
  minify: true,
  banner: {
    js: '/*! MathJax 4 is licensed under Apache-2.0. See LICENSE-MathJax.txt. */'
  },
  outfile: '../../src/app-winui/Assets/mathjax/mathjax-quickjs.js'
});

const sourceDir = path.resolve('node_modules/@mathjax/mathjax-newcm-font/cjs/svg/dynamic');
const outputDir = path.resolve('../../src/app-winui/Assets/mathjax/font');
fs.mkdirSync(outputDir, {recursive: true});
for (const name of fs.readdirSync(sourceDir).filter(name => name.endsWith('.js'))) {
  const source = fs.readFileSync(path.join(sourceDir, name), 'utf8');
  const marker = 'svg_js_1.MathJaxNewcmFont.dynamicSetup';
  const start = source.indexOf(marker);
  if (start < 0) throw new Error(`Missing dynamic font setup in ${name}`);
  const body = source.slice(start).replaceAll(marker, 'globalThis.FoliaMathJaxFont.dynamicSetup').replace(/\/\/\# sourceMappingURL=.*$/m, '');
  fs.writeFileSync(path.join(outputDir, name), `/*! MathJax NewCM is licensed under Apache-2.0. */\n${body}`);
}
