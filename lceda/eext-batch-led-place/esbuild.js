const esbuild = require('esbuild');

esbuild.build({
  entryPoints: ['./src/index.ts'],
  bundle: true,
  format: 'iife',
  globalName: 'edaEsbuildExportName',
  platform: 'browser',
  outfile: 'dist/index.js',
  treeShaking: true,
  minify: false,
}).then(() => {
  console.log('esbuild build success');
}).catch((err) => {
  console.error(err);
  process.exit(1);
});
