// SPDX-License-Identifier: MIT
// OpenFP site: a few small touches. Everything works without it.
(function () {
  'use strict';

  function revealAnchor() {
    var id;
    try { id = decodeURIComponent(location.hash.slice(1)); } catch (_) { return; }
    var target = id && document.getElementById(id);
    if (!target) return;
    var section = target.closest('details');
    if (section) section.open = true;
  }
  revealAnchor();
  window.addEventListener('hashchange', revealAnchor);

  // Keep the account cookie scoped to /account/ while showing its avatar on every page.
  var accountLink = document.querySelector('[data-account-link]');
  var accountProfile = null;
  function showAccount(profile) {
    accountProfile = profile;
    accountLink.classList.toggle('nav-account-signed-in', !!profile);
    if (!profile) {
      accountLink.textContent = 'Account';
      accountLink.removeAttribute('title');
      accountLink.removeAttribute('aria-label');
      return;
    }
    var avatar = document.createElement('span');
    avatar.className = 'nav-account-avatar';
    avatar.style.setProperty('--avatar', profile.avatar);
    avatar.setAttribute('aria-hidden', 'true');
    accountLink.replaceChildren(avatar);
    accountLink.title = 'Account: ' + profile.username;
    accountLink.setAttribute('aria-label', 'Account: ' + profile.username);
  }
  function refreshAccount() {
    if (!accountLink || !window.fetch) return;
    fetch('/account/status.php', {credentials:'same-origin', cache:'no-store'})
      .then(function (response) { if (!response.ok) throw new Error('Account unavailable'); return response.json(); })
      .then(function (data) { showAccount(data.profile); })
      .catch(function () {});
  }
  refreshAccount();
  window.addEventListener('pageshow', function (event) { if (event.persisted) refreshAccount(); });
  document.addEventListener('ofp-profile-saved', function (event) {
    if (accountProfile) showAccount({username:accountProfile.username, avatar:event.detail.avatar});
    else refreshAccount();
  });

  // screenshots, big
  document.addEventListener('click', function (e) {
    var a = e.target.closest('a[data-zoom]');
    if (!a) return;
    e.preventDefault();
    var box = document.createElement('div');
    var picture = document.createElement('img');
    var close = document.createElement('button');
    var caption = document.createElement('p');
    var previousFocus = document.activeElement;
    box.className = 'lightbox';
    box.setAttribute('role', 'dialog');
    box.setAttribute('aria-modal', 'true');
    box.setAttribute('aria-label', 'Screenshot');
    picture.src = a.href;
    picture.alt = a.querySelector('img').alt;
    caption.textContent = a.closest('figure').querySelector('figcaption').innerText;
    close.type = 'button'; close.textContent = 'Close';
    function dismiss() {
      document.removeEventListener('keydown', keyboard);
      document.body.classList.remove('zoom-open');
      box.remove();
      if (previousFocus) previousFocus.focus();
    }
    function keyboard(k) {
      if (k.key === 'Escape') dismiss();
      if (k.key === 'Tab') { k.preventDefault(); close.focus(); }
    }
    close.addEventListener('click', dismiss);
    box.addEventListener('click', function (event) { if (event.target === box) dismiss(); });
    document.addEventListener('keydown', keyboard);
    box.appendChild(close); box.appendChild(picture); box.appendChild(caption);
    document.body.appendChild(box);
    document.body.classList.add('zoom-open');
    close.focus();
  });

  // copy a server's address
  document.addEventListener('click', function (e) {
    var b = e.target.closest('button[data-copy]');
    if (!b || !navigator.clipboard) return;
    navigator.clipboard.writeText(b.dataset.copy).then(function () {
      b.textContent = 'Copied'; b.classList.add('done');
      setTimeout(function () { b.textContent = 'Copy'; b.classList.remove('done'); }, 1500);
    });
  });

  // search and filters on the server list
  var list = document.querySelector('[data-servers]');
  var filter = document.querySelector('[data-filter]');
  var hideFull = document.querySelector('[data-hide-full]');
  var hideLocked = document.querySelector('[data-hide-locked]');
  var limitSel = document.querySelector('select[data-limit]');
  var botsSel = document.querySelector('select[data-bots]');
  function applyFilters() {
    if (!list) return;
    var q = filter ? filter.value.trim().toLowerCase() : '';
    list.querySelectorAll('.server').forEach(function (s) {
      var show = (!q || (s.dataset.text || '').indexOf(q) >= 0) &&
                 !(hideFull && hideFull.checked && s.classList.contains('full')) &&
                 !(hideLocked && hideLocked.checked && s.classList.contains('locked')) &&
                 !(limitSel && limitSel.value && s.dataset.limit !== limitSel.value) &&
                 !(botsSel && botsSel.value && s.dataset.bots !== botsSel.value);
      s.style.display = show ? '' : 'none';
    });
  }
  [filter, hideFull, hideLocked, limitSel, botsSel].forEach(function (el) {
    if (el) { el.addEventListener('input', applyFilters); el.addEventListener('change', applyFilters); }
  });

  // keep the numbers fresh: the server list's totals every 30 s, and the whole
  // list (by reloading it quietly) when it has changed
  var api = list ? list.dataset.api : (document.querySelector('[data-live]') ? '/api/' : null);
  var updated = document.querySelector('[data-updated]');
  var last = Date.now();
  function refresh() {
    if (!api) return;
    fetch(api, { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (d) {
      var t = d.totals || {};
      document.querySelectorAll('[data-live] [data-k]').forEach(function (el) {
        if (t[el.dataset.k] !== undefined) el.textContent = t[el.dataset.k];
      });
      if (list) {
        var have = list.querySelectorAll('.server').length;
        if (have !== (d.servers || []).length) location.reload();
      }
      last = Date.now();
    }).catch(function () {});
  }
  if (api) setInterval(refresh, 30000);
  if (updated) setInterval(function () {
    var s = Math.round((Date.now() - last) / 1000);
    updated.textContent = s < 5 ? 'Updated just now' : 'Updated ' + s + ' s ago';
  }, 5000);
})();
