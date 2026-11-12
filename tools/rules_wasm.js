// SPDX-License-Identifier: MIT
// The rules fingerprint of the web site's WebAssembly core (CI compares it with the other builds'):
//   node tools/rules_wasm.js [public-website/assets/ofp.wasm]
const fs = require('fs');
WebAssembly.instantiate(fs.readFileSync(process.argv[2] || 'public-website/assets/ofp.wasm'), {})
  .then(r => console.log((r.instance.exports.ofp_rules() >>> 0).toString(16).padStart(8, '0')));
