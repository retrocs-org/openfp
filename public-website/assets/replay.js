// SPDX-License-Identifier: MIT
// Replays in a web page: read an .ofprep file, and play it with the game's own core
// (ofp.wasm), so it goes exactly as the match went. Works in browsers and in Node.
(function (root) {
  'use strict';

  // the file: header, players, options, the map, then runs of buttons and checks
  function parse(text) {
    var lines = text.split('\n'), r = { players: [], time_limit: 0, rule: 0, weather: 0, map: '', runs: [], checks: [] };
    var i = 0, m;
    m = (lines[0] || '').match(/^OFPREPLAY ([12])\r?$/);
    if (!m) throw new Error('not a replay');
    r.bot_commands = +m[1] === 2 ? 1 : 0;
    for (i = 1; i < lines.length; i++) {
      var l = lines[i];
      if ((m = l.match(/^seed=(\d+) biome=(\d+) time=(\d+) garages=(\d+) versus=(\d+) ai=(\d+) players=(\d+) tanks=(\d+)/))) {
        r.seed = +m[1]; r.biome = +m[2]; r.time = +m[3]; r.garages = +m[4]; r.versus = +m[5]; r.ai = +m[6];
        r.nplayers = +m[7]; r.tanks = +m[8];
      } else if ((m = l.match(/^player=(\d+) cls=(\d+) team=(\d+) steer=(\d+) bot=(\d+)(?: name=(\S+))?/))) {
        r.players[+m[1]] = { cls: +m[2], team: +m[3], steer: +m[4], bot: +m[5], name: m[6] ? m[6].replace(/_/g, ' ') : '' };
      } else if ((m = l.match(/^time_limit=(\d+) rule=(\d+)/))) { r.time_limit = +m[1]; r.rule = +m[2]; }
      else if ((m = l.match(/^weather=(\d+)/))) r.weather = +m[1];
      else if ((m = l.match(/^classic_clear=(\d+)/))) r.classic_clear = +m[1] === 1;
      else if ((m = l.match(/^world_rules=1 lives=(\d+)/))) { r.world_rules = 1; r.lives = +m[1]; }
      else if ((m = l.match(/^relaxed_pace=(\d+)/))) r.relaxed_pace = +m[1];
      else if ((m = l.match(/^surface_rules=1 powerups=(\d+)/))) { r.surface_rules = 1; r.powerups = +m[1]; }
      else if ((m = l.match(/^rocket_tanks=(\d+)/))) r.rocket_tanks = +m[1];
      else if ((m = l.match(/^leaders=(\d+)/))) r.leaders = +m[1];
      else if ((m = l.match(/^extras=(\d+)/))) r.extras = +m[1];
      else if ((m = l.match(/^map_size=(\d+)/))) r.map_size = +m[1];
      else if ((m = l.match(/^map=(\d+)/))) {         // the map file: exactly that many bytes follow
        var len = +m[1], rest = lines.slice(i + 1).join('\n');
        var enc = new TextEncoder(), dec = new TextDecoder(), bytes = enc.encode(rest);
        r.map = dec.decode(bytes.slice(0, len));
        lines = dec.decode(bytes.slice(len)).split('\n');
        i = -1;
        while (lines.length && lines[0] !== 'ticks') lines.shift();
      } else if (l === 'ticks') break;
    }
    for (i = i + 1; i < lines.length; i++) {
      var t = lines[i];
      if ((m = t.match(/^(\d+) ([0-9a-f]*)$/))) {
        var b = [], k;
        for (k = 0; k < m[2].length; k += 2) b.push(parseInt(m[2].substr(k, 2), 16));
        r.runs.push([+m[1], b]);
      } else if ((m = t.match(/^check (\d+) ([0-9a-f]{8})/))) r.checks.push([+m[1], parseInt(m[2], 16) >>> 0]);
      else if (t === 'end') break;
    }
    r.ticks = r.runs.reduce(function (a, x) { return a + x[0]; }, 0);
    var inputs = new Uint8Array(r.ticks * r.tanks), at = 0;      // every tick's buttons
    r.runs.forEach(function (run) { for (var n = 0; n < run[0]; n++, at++) for (var q = 0; q < r.tanks; q++) inputs[at * r.tanks + q] = run[1][q] || 0; });
    r.inputs = inputs;
    return r;
  }

  // a match from a replay, ready to step through
  function Player(wasm, rep) {
    var x = wasm.exports, mem = function () { return new Uint8Array(x.memory.buffer); };
    this.x = x; this.rep = rep; this.tick = 0;
    var per = x.ofp_alloc(32), map = 0, k;              // (all eight players' cls, team, steer, bot: ofp_start reads eight)
    mem().fill(0, per, per + 32);
    for (k = 0; k < 8; k++) {
      var p = rep.players[k] || { cls: 0, team: 0, steer: 0, bot: 0 };
      mem().set([p.cls, p.team, p.steer, p.bot], per + k * 4);
    }
    if (rep.map) {
      var bytes = new TextEncoder().encode(rep.map + '\0');
      map = x.ofp_alloc(bytes.length);
      mem().set(bytes, map);
    }
    this.args = [rep.seed | 0, rep.biome, rep.time, rep.garages, rep.versus, rep.ai, rep.nplayers, per, rep.time_limit, rep.rule, rep.weather, map, rep.extras | 0, rep.map_size | 0, rep.tanks | 0, rep.bot_commands | 0, rep.classic_clear ? 1 : 0];
    this.args.push(rep.world_rules | 0, rep.lives | 0, rep.relaxed_pace | 0, rep.rocket_tanks | 0, rep.leaders | 0, rep.surface_rules | 0, rep.powerups | 0);
    this.inbuf = x.ofp_alloc(10);                  // MAX_TANKS in the browser core
    this.restart();
  }
  Player.prototype.restart = function () {
    if (this.x.ofp_start.apply(null, this.args) !== 0) throw new Error('the match would not start');
    this.tick = 0;
  };
  Player.prototype.step = function () {               // one tick; false at the end
    if (this.tick >= this.rep.ticks) return false;
    var m = new Uint8Array(this.x.memory.buffer), t = this.rep.tanks;
    m.set(this.rep.inputs.subarray(this.tick * t, this.tick * t + t), this.inbuf);
    this.x.ofp_tick(this.inbuf);
    this.tick++;
    return true;
  };
  Player.prototype.seek = function (to) {              // (there's no going back: from the start)
    if (to < this.tick) this.restart();
    while (this.tick < to && this.step()) {}
  };
  Player.prototype.checksum = function () { return this.x.ofp_checksum() >>> 0; };

  function load(bytes) { return WebAssembly.instantiate(bytes, {}).then(function (r) { return r.instance; }); }

  var api = { parse: parse, Player: Player, load: load };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.OfpReplay = api;
})(this);
