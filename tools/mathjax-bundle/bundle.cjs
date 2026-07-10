const esbuild = require('esbuild');

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
