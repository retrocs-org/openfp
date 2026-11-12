// SPDX-License-Identifier: MIT
// The replay fixtures, played back in the web site's WebAssembly core (the replay viewer's): every
// checksum has to match the GCC build's recording. Exit status 1 if any doesn't (CI's webassembly job).
//
//   node tools/replays_wasm.js tests/replays/*.ofprep
const fs = require('fs');
const path = require('path');
const site = path.join(__dirname, '..', 'public-website', 'assets');
const R = require(path.join(site, 'replay.js'));
(async () => {
  let failed = 0;
  for (const file of process.argv.slice(2)) {
    const rep = R.parse(fs.readFileSync(file, 'utf8'));
    const inst = await R.load(fs.readFileSync(path.join(site, 'ofp.wasm')));
    const p = new R.Player(inst, rep);
    let ok = 0, bad = 0, ci = 0;
    while (p.step()) {
      while (ci < rep.checks.length && rep.checks[ci][0] === p.tick) { (rep.checks[ci][1] === p.checksum() ? ok++ : bad++); ci++; }
    }
    if (bad || !ok) failed++;
    console.log(`${path.basename(file)}: ${rep.ticks} ticks, ${ok} checksums match, ${bad} don't${bad ? '  OUT OF STEP' : ''}`);
  }
  process.exit(failed ? 1 : 0);
})();
