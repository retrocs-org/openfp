// SPDX-License-Identifier: MIT
// The replay page: plays a match with the game's own core (ofp.wasm) and draws it from above.
// (text from a replay, players' names: shown as text, whatever characters they have)
function escapeHtml(t) {
  return String(t).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; });
}
(function () {
  'use strict';
  var box = document.querySelector('.viewer');
  if (!box) return;
  var cv = box.querySelector('canvas'), g = cv.getContext('2d'), status = box.querySelector('.viewer-status');
  var playBtn = box.querySelector('[data-play]'), speedBtn = box.querySelector('[data-speed]');
  var seek = box.querySelector('[data-seek]'), timeEl = box.querySelector('.viewer-time');
  var scoreEl = box.querySelector('.viewer-score tbody');
  var wasmBytes = fetch('/assets/ofp.wasm?v=6').then(function (r) { return r.arrayBuffer(); });
  var player = null, x = null, playing = false, speed = 1, acc = 0, last = 0, ground = null, groundTick = -1;
  var zoom = 1, follow = -1, lastRows = '';
  var live = box.dataset.live || '';                             // 1: the whole map; 2, 4: closer, round the followed tank
  var zoomBtn = box.querySelector('[data-zoom]');
  var sheets = ['classic', 'desert', 'jungle', 'winter', 'scifi'].map(function (b) {   // the game's own tanks
    var im = new Image(); im.src = '/assets/sprites/tanks-' + b + '.png'; return im;
  });
  var SCALE = 4, W = 128, TICK = 1000 / 50;
  var GROUND = ['#7c9a44', '#d8b878', '#4a8530', '#e6ecf0', '#3b414b',          // classic, desert, jungle, winter, sci-fi,
                '#333333', '#8a5a34', '#969696', '#4a5530'];                      // volcanic, wasteland, moon, swamp
  var TILE = [null, '#6f6f6f', '#2f68b8', '#5a5048', '#2c5a1e', '#6a5a3a', '#9a8f80', '#857a6c', '#a59a88', '#4a4038', '#b8b0a0', '#1e4a1c', '#b5d9e8', '#22232b', '#302f37'];   // (11: trees overhead)
  var TEAM = ['#3b82ff', '#ff4030'], TEAM_LIGHT = ['#a8c6ff', '#ffa898'];
  var TANKS = ['Jackal', 'Bison', 'Mammoth'];

  function fmt(t) { var s = Math.floor(t / 50); return Math.floor(s / 60) + ':' + ('0' + s % 60).slice(-2); }

  function drawGround() {                                // the tiles: redrawn now and then (they change slowly)
    W = x.ofp_info(28) || 128;                     // (the map's own size: small 64 .. huge 192)
    var img = g.createImageData(W, W), d = img.data, biome = x.ofp_info(4), base = GROUND[biome] || GROUND[0];
    var bc = [parseInt(base.substr(1, 2), 16), parseInt(base.substr(3, 2), 16), parseInt(base.substr(5, 2), 16)];
    for (var ty = 0; ty < W; ty++) for (var tx = 0; tx < W; tx++) {
      var v = x.ofp_tile(tx, ty), t = v & 255, team = (v >> 8) & 15, alive = (v >> 12) & 1, o = (ty * W + tx) * 4, c = bc;
      if (team && alive) c = team === 1 ? [70, 100, 170] : team === 2 ? [170, 70, 60] : [160, 95, 180];
      else if (TILE[t]) c = [parseInt(TILE[t].substr(1, 2), 16), parseInt(TILE[t].substr(3, 2), 16), parseInt(TILE[t].substr(5, 2), 16)];
      d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
    }
    var off = document.createElement('canvas'); off.width = W; off.height = W;
    off.getContext('2d').putImageData(img, 0, 0);
    ground = off; groundTick = player.tick;
  }

  function draw() {
    var i, n;
    if (!ground || player.tick - groundTick > 25 || player.tick < groundTick) drawGround();
    g.imageSmoothingEnabled = false;
    // the view: the whole map, or closer round the tank being followed
    var cell = SCALE * zoom, cx = W / 2, cy = W / 2;
    if (zoom > 1 && follow >= 0 && follow < x.ofp_info(1)) { cx = x.ofp_tank(follow, 0) / 16; cy = x.ofp_tank(follow, 1) / 16; }
    var half = cv.width / 2 / cell;
    cx = Math.max(half, Math.min(W - half, cx)); cy = Math.max(half, Math.min(W - half, cy));
    var ox = cv.width / 2 - cx * cell, oy = cv.height / 2 - cy * cell;
    g.fillStyle = '#111'; g.fillRect(0, 0, cv.width, cv.height);
    g.drawImage(ground, ox, oy, W * cell, W * cell);
    if (x.ofp_info(5) === 2) { g.fillStyle = 'rgba(0,0,30,.35)'; g.fillRect(0, 0, cv.width, cv.height); }   // night
    if (x.ofp_info(6) === 1) { g.fillStyle = 'rgba(120,130,150,.18)'; g.fillRect(0, 0, cv.width, cv.height); } // a storm
    var px = function (v) { return v / 16 * cell + ox; }, py = function (v) { return v / 16 * cell + oy; };
    if (x.ofp_info(11) & 2) {                            // the hill, in the holder's colour
      var hh = x.ofp_info(16), hc = hh >= 0 ? TEAM[hh] : '#f0e6c8', hs = 3 * cell;
      var hx = (x.ofp_info(12) + 0.5) * cell + ox, hy = (x.ofp_info(13) + 0.5) * cell + oy;
      g.fillStyle = hh >= 0 ? (hh ? 'rgba(255,64,48,.18)' : 'rgba(59,130,255,.18)') : 'rgba(240,230,200,.10)';
      g.fillRect(hx - hs, hy - hs, hs * 2, hs * 2);
      g.strokeStyle = hc; g.lineWidth = 2; g.strokeRect(hx - hs, hy - hs, hs * 2, hs * 2);
    }
    var ex = x.ofp_info(11), noflags = (ex & (4 | 8 | 16 | 32 | 64 | 128)) || x.ofp_info(27);   // (modes without flags; arenas)
    if ((ex & 128) && x.ofp_info(17)) {                  // hardpoint: the point that counts, in the colour of the side alone on it
      var ha = x.ofp_info(29), hs2 = 3 * cell, on = [0, 0];
      var hpx = (x.ofp_cp(ha, 0) + 0.5) * cell + ox, hpy = (x.ofp_cp(ha, 1) + 0.5) * cell + oy;
      for (i = 0, n = x.ofp_info(1); i < n; i++) if (x.ofp_tank(i, 4)) {
        var dx = px(x.ofp_tank(i, 0)) - hpx, dy = py(x.ofp_tank(i, 1)) - hpy;
        if (dx * dx + dy * dy < hs2 * hs2) on[x.ofp_tank(i, 3) & 1] = 1;
      }
      var hcol = on[0] !== on[1] ? TEAM[on[0] ? 0 : 1] : '#f0e6c8';
      g.fillStyle = hcol; g.globalAlpha = 0.25; g.fillRect(hpx - hs2, hpy - hs2, hs2 * 2, hs2 * 2); g.globalAlpha = 1;
      g.strokeStyle = hcol; g.lineWidth = 2; g.strokeRect(hpx - hs2, hpy - hs2, hs2 * 2, hs2 * 2);
      g.fillStyle = '#fff'; g.font = '10px sans-serif'; g.fillText('HARDPOINT ' + x.ofp_info(35) + 's', hpx - hs2, hpy - hs2 - 3);
    }
    if (ex & 64) for (i = 0, n = x.ofp_info(34); i < n; i++) if (x.ofp_tag(i, 0)) {   // kill confirmed: the tags, the fallen side's colour
      var tgx = px(x.ofp_tag(i, 1)), tgy = py(x.ofp_tag(i, 2));
      g.fillStyle = TEAM[x.ofp_tag(i, 3) & 1]; g.strokeStyle = '#fff'; g.lineWidth = 1;
      g.beginPath(); g.moveTo(tgx, tgy - 5); g.lineTo(tgx + 5, tgy); g.lineTo(tgx, tgy + 5); g.lineTo(tgx - 5, tgy); g.closePath(); g.fill(); g.stroke();
    }
    if (ex & 4) for (i = 0, n = x.ofp_info(17); i < n; i++) {   // conquest's points: A B C, in their owners' colours
      var co = x.ofp_cp(i, 2), cc = co >= 0 ? TEAM[co] : '#f0e6c8', cs = 3 * cell;
      var cxp = (x.ofp_cp(i, 0) + 0.5) * cell + ox, cyp = (x.ofp_cp(i, 1) + 0.5) * cell + oy, tk = x.ofp_cp(i, 3);
      g.strokeStyle = cc; g.lineWidth = 2; g.strokeRect(cxp - cs, cyp - cs, cs * 2, cs * 2);
      g.fillStyle = cc; g.globalAlpha = co >= 0 ? 0.18 : 0.08; g.fillRect(cxp - cs, cyp - cs, cs * 2, cs * 2); g.globalAlpha = 1;
      g.font = 'bold 12px sans-serif'; g.textAlign = 'center'; g.fillText(String.fromCharCode(65 + i), cxp, cyp + 4); g.textAlign = 'left';
      if (tk) { g.fillStyle = TEAM[tk > 0 ? 0 : 1]; g.fillRect(cxp - cs, cyp + cs + 2, Math.abs(tk) / 250 * cs * 2, 3); }   // (being taken)
    }
    if (x.ofp_info(11) & 1) for (i = 0; i < 6; i++) if (x.ofp_pickup(i, 0)) {   // pickups: repair, mines, fuel
      var kx = (x.ofp_pickup(i, 1) + 0.5) * cell + ox, ky = (x.ofp_pickup(i, 2) + 0.5) * cell + oy, ks = Math.max(4, cell * 0.8);
      g.fillStyle = ['#f0f0eb', '#5a6432', '#c8281e', '#3287e1', '#f0aa2d'][x.ofp_pickup(i, 3)] || '#fff';
      g.fillRect(kx - ks / 2, ky - ks / 2, ks, ks); g.strokeStyle = '#000'; g.lineWidth = 1; g.strokeRect(kx - ks / 2, ky - ks / 2, ks, ks);
    }
    for (i = 0, n = x.ofp_info(8); i < n; i++) if (x.ofp_man(i, 0)) {           // soldiers and prisoners
      g.fillStyle = x.ofp_man(i, 0) === 1 ? '#ffffff' : TEAM_LIGHT[x.ofp_man(i, 3) & 1];
      g.fillRect(px(x.ofp_man(i, 1)) - 1, py(x.ofp_man(i, 2)) - 1, 2, 2);
    }
    for (i = 0, n = x.ofp_info(7); i < n; i++) if (x.ofp_shot(i, 0)) {          // shots
      g.fillStyle = '#ffe28a';
      g.fillRect(px(x.ofp_shot(i, 1)) - 1, py(x.ofp_shot(i, 2)) - 1, 2, 2);
    }
    for (i = 0, n = x.ofp_info(9); i < n; i++) if (x.ofp_heli(i, 0)) {          // helicopters
      var hx = px(x.ofp_heli(i, 1)), hy = py(x.ofp_heli(i, 2));
      g.strokeStyle = '#ff9a40'; g.lineWidth = 2;
      g.beginPath(); g.moveTo(hx - 5, hy - 5); g.lineTo(hx + 5, hy + 5); g.moveTo(hx + 5, hy - 5); g.lineTo(hx - 5, hy + 5); g.stroke();
    }
    for (i = 0, n = x.ofp_info(1); i < n; i++) if (x.ofp_tank(i, 4)) {          // tanks: a triangle, pointing where it goes
      var tx = px(x.ofp_tank(i, 0)), ty = py(x.ofp_tank(i, 1)), ang = x.ofp_tank(i, 2), team = x.ofp_tank(i, 3) & 1;
      var sh = sheets[x.ofp_info(4)] || sheets[0], size = Math.max(10, cell * 2);   // (a tank: 32 px, two tiles)
      if (sh.complete && sh.naturalWidth) {               // the game's sprite: 32 directions, a row per class and side
        var frame = ((ang + 0x400) >> 11) & 31, row = x.ofp_tank(i, 5) * 2 + team;
        g.drawImage(sh, frame * 32, row * 32, 32, 32, tx - size / 2, ty - size / 2, size, size);
        g.strokeStyle = TEAM[team]; g.lineWidth = 2; g.strokeRect(tx - size / 2, ty - size / 2, size, size);
      } else {                                           // (until it loads: a triangle)
        g.save(); g.translate(tx, ty); g.rotate(ang / 65536 * Math.PI * 2);
        g.fillStyle = TEAM[team]; g.strokeStyle = '#000'; g.lineWidth = 1;
        g.beginPath(); g.moveTo(0, -7); g.lineTo(5, 5); g.lineTo(-5, 5); g.closePath(); g.fill(); g.stroke();
        g.restore();
      }
      if (i === follow) { g.strokeStyle = '#f8c440'; g.lineWidth = 2; g.beginPath(); g.arc(tx, ty, size * 0.8, 0, Math.PI * 2); g.stroke(); }
      var pn = player.rep.players[i] && player.rep.players[i].name;
      g.fillStyle = '#fff'; g.font = '10px sans-serif'; g.fillText(i < player.rep.nplayers ? (pn || 'P' + (i + 1)) : 'CPU', tx + 7, ty - 5);
    }
    if (ex & 32) for (i = 0; i < 2; i++) {               // warlord: a ring round each side's warlord
      var jw = x.ofp_info(22 + i);
      if (jw >= 0 && jw < x.ofp_info(1) && x.ofp_tank(jw, 4)) {
        g.strokeStyle = '#f8c440'; g.lineWidth = 2; g.beginPath();
        g.arc(px(x.ofp_tank(jw, 0)), py(x.ofp_tank(jw, 1)), Math.max(9, cell * 1.6), 0, 7); g.stroke();
      }
    }
    for (i = 0; i < 2 && !noflags; i++) {                // the flags (not in the modes without)
      var fx = px(x.ofp_flag(i, 0)), fy = py(x.ofp_flag(i, 1));
      g.fillStyle = TEAM[i]; g.strokeStyle = '#fff'; g.lineWidth = 1.5;
      g.beginPath(); g.moveTo(fx, fy - 7); g.lineTo(fx + 6, fy); g.lineTo(fx, fy + 7); g.lineTo(fx - 6, fy); g.closePath(); g.fill(); g.stroke();
    }
    var w = x.ofp_info(2), over = w >= 0 || x.ofp_info(3) || player.tick >= player.rep.ticks;
    if (over) {
      g.fillStyle = 'rgba(0,0,0,.55)'; g.fillRect(0, 230, cv.width, 52);
      g.fillStyle = '#f8c440'; g.font = 'bold 22px sans-serif'; g.textAlign = 'center';
      var lone = x.ofp_info(26), ln = lone >= 0 ? (lone < player.rep.nplayers ? ((player.rep.players[lone] && player.rep.players[lone].name) || 'Player ' + (lone + 1)) : 'A computer player') : '';
      g.fillText(lone >= 0 ? ln + ' wins' : w === 0 ? 'The camp side wins' : w === 1 ? 'The fortress side wins' : x.ofp_info(3) ? 'Time up: a draw' : 'The end', cv.width / 2, 264);
      g.textAlign = 'left';
    }
    timeEl.textContent = fmt(player.tick) + ' / ' + fmt(player.rep.ticks) +
      ((ex & 2) ? '  ·  hill ' + Math.floor(x.ofp_info(14) / 50) + 's : ' + Math.floor(x.ofp_info(15) / 50) + 's' : '') +
      ((ex & 4) ? '  ·  conquest ' + x.ofp_info(18) + ' : ' + x.ofp_info(19) + ' (300 wins)' : '') +
      ((ex & 16) ? '  ·  kills ' + x.ofp_info(20) + ' : ' + x.ofp_info(21) + ' (20 wins)' : '') +
      ((ex & 32) ? '  ·  warlords down ' + x.ofp_info(24) + ' : ' + x.ofp_info(25) + ' (7 wins)' : '') +
      ((ex & 64) ? '  ·  kills confirmed ' + x.ofp_info(32) + ' : ' + x.ofp_info(33) + ' (15 wins)' : '') +
      ((ex & 128) ? '  ·  hardpoint ' + x.ofp_info(30) + ' : ' + x.ofp_info(31) + ' (120 wins)' : '') +
      ((ex & 8) ? '  ·  every tank for itself' : '');
    seek.value = Math.round(player.tick / Math.max(1, player.rep.ticks) * 1000);
    var rows = '';
    for (i = 0, n = x.ofp_info(1); i < n; i++) {
      var team = x.ofp_tank(i, 3) & 1, pl = player.rep.players[i];
      var who = i < player.rep.nplayers ? (pl && pl.name ? escapeHtml(pl.name) : 'P' + (i + 1)) + (pl && pl.bot ? ' (computer)' : '') : 'Computer';
      rows += '<tr data-follow="' + i + '" class="' + (i === follow ? 'following' : '') + '"><td><span style="color:' + TEAM[team] + '">&#9650;</span> ' + who + '</td><td>' + (TANKS[x.ofp_tank(i, 5)] || '') +
              '</td><td class="n">' + x.ofp_tank(i, 8) + '</td></tr>';
    }
    if (rows !== lastRows) { scoreEl.innerHTML = rows; lastRows = rows; }   // (only when it changes: rows stay clickable)
  }

  function frame(now) {
    if (playing && player) {
      acc += Math.min(200, now - (last || now)) * speed;
      while (acc >= TICK) {
        if (!player.step()) { if (!live) { playing = false; playBtn.textContent = 'Play'; } acc = 0; break; }   // (live: wait for more)
        acc -= TICK;
      }
    }
    last = now;
    if (player) draw();
    requestAnimationFrame(frame);
  }

  function start(text) {
    var rep, weather = ['', 'a thunderstorm', 'changing weather', 'rain', 'snow', 'fog', 'a sandstorm', 'clouds', 'an eruption', 'a meteor shower', 'a snowstorm'];
    try { rep = OfpReplay.parse(text); } catch (e) { status.textContent = 'That isn\'t a replay this page can read.'; return; }
    wasmBytes.then(OfpReplay.load).then(function (inst) {
      x = inst.exports;
      player = new OfpReplay.Player(inst, rep);
      ground = null;
      var sky = weather[x.ofp_info(36)];
      status.textContent = rep.ticks + ' ticks, ' + rep.nplayers + ' player' + (rep.nplayers === 1 ? '' : 's') +
        (sky ? ', ' + sky : '') + '. Played by the game\'s own rules, so it goes exactly as it went.';
      playing = true; playBtn.textContent = 'Pause';
    }).catch(function (e) { status.textContent = 'The replay wouldn\'t play: ' + e.message; });
  }

  scoreEl.addEventListener('click', function (e) {    // a row: follow that tank (closer, if it was the whole map)
    var tr = e.target.closest('tr[data-follow]');
    if (!tr) return;
    var i = +tr.dataset.follow;
    follow = follow === i ? -1 : i;
    if (follow >= 0 && zoom === 1) { zoom = 2; zoomBtn.textContent = 'Zoom 2x'; }
    if (follow < 0) { zoom = 1; zoomBtn.textContent = 'Zoom 1x'; }
  });
  zoomBtn.addEventListener('click', function () {
    zoom = zoom >= 4 ? 1 : zoom * 2;
    if (zoom > 1 && follow < 0) follow = 0;
    zoomBtn.textContent = 'Zoom ' + zoom + 'x';
  });
  playBtn.addEventListener('click', function () { if (!player) return; playing = !playing; playBtn.textContent = playing ? 'Pause' : 'Play'; });
  speedBtn.addEventListener('click', function () { speed = speed >= 16 ? 1 : speed * 2; speedBtn.textContent = speed + 'x'; });
  seek.addEventListener('input', function () {
    if (!player) return;
    player.seek(Math.round(seek.value / 1000 * player.rep.ticks));
    ground = null;
  });
  box.addEventListener('dragover', function (e) { e.preventDefault(); });
  box.addEventListener('drop', function (e) {                 // a replay of your own
    e.preventDefault();
    var f = e.dataTransfer.files[0];
    if (f) f.text().then(start);
  });
  if (box.dataset.replay) fetch('/api/?action=replay&id=' + box.dataset.replay).then(function (r) { return r.text(); }).then(start);
  else if (box.dataset.daily) fetch('/api/?action=daily_replay&id=' + box.dataset.daily).then(function (r) { return r.text(); }).then(start);
  else if (live) {                                     // a match that's on: a little behind, and more every 10 s
    var liveUrl = '/api/?action=live&server=' + encodeURIComponent(live);
    var poll = function (first) {
      fetch(liveUrl, { cache: 'no-store' }).then(function (r) {
        if (!r.ok) throw new Error('over');
        return r.text();
      }).then(function (text) {
        if (first) {
          start(text);
          wasmBytes.then(function () {                  // (once it's going: to about 10 s behind the newest)
            setTimeout(function () { if (player) player.seek(Math.max(0, player.rep.ticks - 500)); }, 300);
          });
        } else if (player) {
          var rep = OfpReplay.parse(text);
          if (rep.seed === player.rep.seed && rep.ticks >= player.rep.ticks) player.rep = rep;   // (the same match: just more of it)
          else { start(text); }                         // (a new match)
        }
        status.textContent = 'Live: ' + fmt(player ? player.rep.ticks : 0) + ' so far; the server sends more every 10 s.';
      }).catch(function () {
        status.textContent = player ? 'The match is over (or it\'s stopped showing).' : 'Nothing on there now.';
        clearInterval(timer);
      });
    };
    var timer = setInterval(function () { poll(false); }, 10000);
    poll(true);
  }
  else status.textContent = 'Drop a replay file here.';
  requestAnimationFrame(frame);
})();
