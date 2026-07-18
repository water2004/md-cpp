const {mathjax} = require('@mathjax/src/js/mathjax.js');
const {TeX} = require('@mathjax/src/js/input/tex.js');
const {SVG} = require('@mathjax/src/js/output/svg.js');
const {MathJaxNewcmFont} = require('@mathjax/mathjax-newcm-font/js/svg.js');
const {liteAdaptor} = require('@mathjax/src/js/adaptors/liteAdaptor.js');
const {RegisterHTMLHandler} = require('@mathjax/src/js/handlers/html.js');
require('@mathjax/src/js/input/tex/base/BaseConfiguration.js');
require('@mathjax/src/js/input/tex/ams/AmsConfiguration.js');
require('@mathjax/src/js/input/tex/newcommand/NewcommandConfiguration.js');
require('@mathjax/src/js/input/tex/noundefined/NoUndefinedConfiguration.js');
require('@mathjax/src/js/input/tex/boldsymbol/BoldsymbolConfiguration.js');
require('@mathjax/src/js/input/tex/braket/BraketConfiguration.js');
require('@mathjax/src/js/input/tex/cancel/CancelConfiguration.js');
require('@mathjax/src/js/input/tex/cases/CasesConfiguration.js');
require('@mathjax/src/js/input/tex/color/ColorConfiguration.js');
require('@mathjax/src/js/input/tex/configmacros/ConfigMacrosConfiguration.js');
require('@mathjax/src/js/input/tex/extpfeil/ExtpfeilConfiguration.js');
require('@mathjax/src/js/input/tex/mathtools/MathtoolsConfiguration.js');
require('@mathjax/src/js/input/tex/physics/PhysicsConfiguration.js');
require('@mathjax/src/js/input/tex/textmacros/TextMacrosConfiguration.js');
require('@mathjax/src/js/input/tex/unicode/UnicodeConfiguration.js');
require('@mathjax/src/js/input/tex/upgreek/UpgreekConfiguration.js');
require('@mathjax/src/js/input/tex/verb/VerbConfiguration.js');

const adaptor = liteAdaptor({fontSize: 16});
RegisterHTMLHandler(adaptor);
mathjax.asyncLoad = name => globalThis.FoliaLoadMathJaxModule(String(name));
mathjax.asyncIsSynchronous = true;
globalThis.FoliaMathJaxFont = MathJaxNewcmFont;
const input = new TeX({
  packages: ['base', 'ams', 'newcommand', 'noundefined', 'boldsymbol', 'braket', 'cancel', 'cases', 'color', 'configmacros', 'extpfeil', 'mathtools', 'physics', 'textmacros', 'unicode', 'upgreek', 'verb'],
  formatError(jax, error) { throw error; }
});
const output = new SVG({
  fontData: MathJaxNewcmFont,
  fontCache: 'none',
  exFactor: 0.5,
  displayOverflow: 'linebreak',
  linebreaks: {inline: true}
});
const document = mathjax.document('', {InputJax: input, OutputJax: output});

globalThis.FoliaMathJax = {
  render(tex, display, em, width) {
    const node = document.convert(String(tex), {
      display: Boolean(display),
      em: Number(em) || 16,
      ex: (Number(em) || 16) * 0.5,
      containerWidth: Number(width) || 1280
    });
    return adaptor.serializeXML(node);
  }
};
