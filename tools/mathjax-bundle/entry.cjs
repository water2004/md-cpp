const {mathjax} = require('@mathjax/src/js/mathjax.js');
const {TeX} = require('@mathjax/src/js/input/tex.js');
const {SVG} = require('@mathjax/src/js/output/svg.js');
const {liteAdaptor} = require('@mathjax/src/js/adaptors/liteAdaptor.js');
const {RegisterHTMLHandler} = require('@mathjax/src/js/handlers/html.js');
require('@mathjax/src/js/input/tex/base/BaseConfiguration.js');
require('@mathjax/src/js/input/tex/ams/AmsConfiguration.js');
require('@mathjax/src/js/input/tex/newcommand/NewcommandConfiguration.js');
require('@mathjax/src/js/input/tex/noundefined/NoUndefinedConfiguration.js');

const adaptor = liteAdaptor({fontSize: 16});
RegisterHTMLHandler(adaptor);
const input = new TeX({packages: ['base', 'ams', 'newcommand', 'noundefined'], formatError(jax, error) { throw error; }});
const output = new SVG({fontCache: 'none', exFactor: 0.5});
const document = mathjax.document('', {InputJax: input, OutputJax: output});

globalThis.ElMdMathJax = {
  render(tex, display, em, width) {
    const node = document.convert(String(tex), {
      display: Boolean(display),
      em: Number(em) || 16,
      ex: (Number(em) || 16) * 0.5,
      containerWidth: Number(width) || 1280
    });
    return adaptor.outerHTML(node);
  }
};
