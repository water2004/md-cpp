const fs = require('fs');
const path = require('path');

const output = path.resolve(__dirname, '../../src/app-winui/Assets/mermaid');
fs.mkdirSync(output, {recursive: true});
fs.copyFileSync(
  require.resolve('mermaid/dist/mermaid.min.js'),
  path.join(output, 'mermaid.min.js')
);
fs.copyFileSync(
  require.resolve('mermaid/LICENSE'),
  path.join(output, 'LICENSE-Mermaid.txt')
);
