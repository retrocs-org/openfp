// SPDX-License-Identifier: MIT
// Automatic map filters and community map previews.
(function () {
  'use strict';
  var ground = { classic: '#6f8d3a', desert: '#d8b878', jungle: '#3f7a2a', winter: '#e8eef2', scifi: '#3a3f48',
                 volcanic: '#3a3230', wasteland: '#8a7448', moon: '#9a9aa2', swamp: '#4a5a32' };
  var colour = { '=': '#707070', '~': '#2f68b8', '^': '#5a5048', 'T': '#2c5a1e', ',': '#6a5a3a', '#': '#9a8f80', ':': '#857a6c',
                 'x': '#4a4038', 'P': '#3b82ff', 'E': '#ff4030', 'p': '#9cc0ff', 'e': '#ff9a88', 'i': '#d25ff0' };
  function refreshPreviews(root) {
    if (!window.fetch) return;
    var canvases = Array.prototype.slice.call(root.querySelectorAll('canvas[data-map]'));
    for (var start = 0; start < canvases.length; start += 60) {
      drawPreviews(canvases.slice(start, start + 60));
    }
  }
  function drawPreviews(canvases) {
    fetch('/api/?action=map_thumbs&ids=' + canvases.map(function (c) { return c.dataset.map; }).join(','))
      .then(function (r) { return r.json(); }).then(function (thumbs) {
        canvases.forEach(function (c) {
          var t = thumbs[c.dataset.map], g = c.getContext('2d'), i;
          g.fillStyle = ground[c.dataset.world] || ground.classic; g.fillRect(0, 0, 128, 128);
          if (!t) return;
          for (i = 0; i < t.length; i++) {
            var k = t.charAt(i), big = 'PE'.indexOf(k) >= 0;
            if (!colour[k]) continue;
            g.fillStyle = c.dataset.world === 'moon' && k === '~' ? '#302f37' : colour[k];
            g.fillRect((i % 64) * 2 - (big ? 2 : 0), Math.floor(i / 64) * 2 - (big ? 2 : 0), big ? 6 : 2, big ? 6 : 2);
          }
        });
      }).catch(function () {});
  }

  var form = document.querySelector('.map-filters');
  var results = document.querySelector('[data-map-results]');
  var status = document.querySelector('[data-map-filter-status]');
  var clear = document.querySelector('[data-clear-map-filters]');
  refreshPreviews(document);
  if (!form || !results) return;

  function selectedUrl() {
    var url = new URL(form.action);
    new FormData(form).forEach(function (value, key) {
      if (value) url.searchParams.set(key, value);
    });
    url.hash = location.hash;
    return url;
  }
  function showClear() {
    clear.hidden = !form.elements.world.value && !form.elements.mode.value;
  }
  var pending, revision = 0;
  function filterMaps(url, push) {
    if (!window.fetch || !window.AbortController) { location.assign(url.href); return; }
    if (pending) pending.abort();
    pending = new AbortController();
    var current = ++revision;
    results.setAttribute('aria-busy', 'true');
    status.textContent = 'Updating maps…';
    fetch(url.href, {signal: pending.signal}).then(function (response) {
      if (!response.ok) throw new Error('Map list unavailable');
      return response.text();
    }).then(function (html) {
      if (current !== revision) return;
      var updated = new DOMParser().parseFromString(html, 'text/html').querySelector('[data-map-results]');
      if (!updated) throw new Error('Map list missing');
      results.replaceChildren.apply(results, Array.from(updated.childNodes));
      if (push && url.href !== location.href) history.pushState(null, '', url.href);
      refreshPreviews(results);
      status.textContent = 'Map filters updated.';
    }).catch(function (error) {
      // Keep ordinary server filtering available if the background request fails.
      if (error.name !== 'AbortError' && current === revision) location.assign(url.href);
    }).finally(function () {
      if (current === revision) results.removeAttribute('aria-busy');
    });
  }
  form.addEventListener('change', function (event) {
    if (event.target.tagName !== 'SELECT') return;
    showClear();
    filterMaps(selectedUrl(), true);
  });
  form.addEventListener('submit', function (event) {
    event.preventDefault();
    filterMaps(selectedUrl(), true);
  });
  clear.addEventListener('click', function (event) {
    event.preventDefault();
    form.elements.world.value = '';
    form.elements.mode.value = '';
    showClear();
    filterMaps(selectedUrl(), true);
  });
  window.addEventListener('popstate', function () {
    var url = new URL(location.href);
    ['world', 'mode'].forEach(function (key) {
      form.elements[key].value = url.searchParams.get(key) || '';
      if (form.elements[key].selectedIndex < 0) form.elements[key].value = '';
    });
    showClear();
    filterMaps(url, false);
  });
})();
