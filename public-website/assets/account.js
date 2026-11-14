// SPDX-License-Identifier: MIT
'use strict';
for (const picker of document.querySelectorAll('.tank-picker')) {
  const cards = Array.from(picker.querySelectorAll('.tank-choice'));
  const radios = cards.map(card => card.querySelector('input'));
  const position = picker.querySelector('.tank-picker-position');
  function show(index) {
    radios[index].checked = true;
    cards.forEach((card, i) => { card.hidden = i !== index; });
    position.textContent = `${index + 1} / ${cards.length}`;
    position.setAttribute('aria-label', `${cards[index].querySelector('strong').textContent}, tank ${index + 1} of ${cards.length}`);
  }
  function step(direction) {
    const index = radios.findIndex(radio => radio.checked);
    const next = (index + direction + cards.length) % cards.length;
    show(next);
    radios[next].dispatchEvent(new Event('change', {bubbles: true}));
  }
  picker.querySelector('.tank-picker-nav').hidden = false;
  for (const button of picker.querySelectorAll('[data-tank-step]'))
    button.addEventListener('click', () => step(Number(button.dataset.tankStep)));
  picker.addEventListener('change', event => {
    const index = radios.indexOf(event.target);
    if (index >= 0) show(index);
  });
  picker.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    event.preventDefault();
    step(event.key === 'ArrowLeft' ? -1 : 1);
    if (radios.includes(event.target)) radios.find(radio => radio.checked).focus();
  });
  show(Math.max(0, radios.findIndex(radio => radio.checked)));
}

for (const form of document.querySelectorAll('.account-profile-form')) {
  const status = form.querySelector('.account-save-status');
  const retry = form.querySelector('.account-save-retry');
  const values = () => new URLSearchParams(new FormData(form)).toString();
  let saved = values();
  let busy = false;
  let timer;
  function message(text, state) {
    status.textContent = text;
    status.dataset.state = state;
    retry.hidden = state !== 'error';
  }
  async function save() {
    clearTimeout(timer);
    if (busy) return; // The running request will send the latest choices next.
    const snapshot = values();
    if (snapshot === saved) { message('All changes saved.', 'saved'); return; }
    busy = true;
    message('Saving changes…', 'pending');
    const body = new URLSearchParams(snapshot);
    body.set('format', 'json');
    const abort = new AbortController();
    const deadline = setTimeout(() => abort.abort(), 15000);
    try {
      const response = await fetch('/account/', {method: 'POST', body,
        credentials: 'same-origin', headers: {Accept: 'application/json'},
        signal: abort.signal, keepalive: true});
      const result = await response.json();
      if (!response.ok || result.status !== 'saved')
        throw new Error(result.error || 'Your changes could not be saved. Try again.');
      saved = snapshot;
      document.dispatchEvent(new CustomEvent('ofp-profile-saved', {detail:{avatar:Number(body.get('avatar'))}}));
      message(values() === saved ? 'All changes saved.' : 'Saving changes…', values() === saved ? 'saved' : 'pending');
    } catch (error) {
      saved = null; // A lost response may still have committed: retry the complete selection.
      message(error.name === 'AbortError' || error instanceof TypeError ?
        'Connection lost. Your changes are not saved yet.' : error.message, 'error');
    } finally {
      clearTimeout(deadline);
      busy = false;
      // Never apply an older response over choices made while it was in flight.
      if (values() !== snapshot) timer = setTimeout(save, 0);
    }
  }
  form.addEventListener('change', () => {
    message('Saving changes…', 'pending');
    clearTimeout(timer);
    timer = setTimeout(save, 250);
  });
  form.addEventListener('submit', event => { event.preventDefault(); save(); });
  retry.addEventListener('click', save);
  window.addEventListener('beforeunload', event => {
    if (values() !== saved) { event.preventDefault(); event.returnValue = ''; }
  });
}
