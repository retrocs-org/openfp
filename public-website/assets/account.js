// SPDX-License-Identifier: MIT
'use strict';
for (const picker of document.querySelectorAll('.tank-picker')) {
  const cards = Array.from(picker.querySelectorAll('.tank-choice'));
  const radios = cards.map(card => card.querySelector('input'));
  const position = picker.querySelector('.tank-picker-position');
  function show(index) {
    radios[index].checked = true;
    cards.forEach((card, i) => { card.hidden = i !== index; });
    position.textContent = `${cards[index].querySelector('strong').textContent} · ${index + 1} / ${cards.length}`;
  }
  function step(direction) {
    const index = radios.findIndex(radio => radio.checked);
    show((index + direction + cards.length) % cards.length);
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
