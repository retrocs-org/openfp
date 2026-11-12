<?php
// SPDX-License-Identifier: MIT
// Mode families follow the desktop map picker: Hardpoint uses Conquest maps,
// and Kill Confirmed uses Team Deathmatch arenas. Custom maps select one mode.
const OFP_MAP_WORLDS = ['classic' => 'Classic', 'moon' => 'Moon Base', 'winter' => 'Winter',
    'desert' => 'Desert', 'jungle' => 'Jungle', 'swamp' => 'Swamp', 'volcanic' => 'Volcano', 'wasteland' => 'Wasteland'];
const OFP_MAP_MODES = ['classic' => 'Classic', 'flag' => 'Capture the Flag', 'conquest' => 'Conquest',
    'hardpoint' => 'Hardpoint', 'deathmatch' => 'Team Deathmatch', 'killconfirmed' => 'Kill Confirmed',
    'annihilation' => 'Annihilation', 'warlord' => 'Warlord'];

function ofp_map_modes(array $fields, string $world): array
{
    $mode = $fields['mode'] ?? '';
    if (isset(OFP_MAP_MODES[$mode])) return [$mode];
    if ($world === 'classic') return ['classic'];
    $modes = trim($fields['modes'] ?? '');
    if ($modes === '') {
        $tokens = ($fields['arena'] ?? '') === 'yes' ? ['annihilation'] : ['flag', 'conquest', 'deathmatch', 'warlord'];
    } else {
        $tokens = preg_split('/[\s,]+/', $modes, -1, PREG_SPLIT_NO_EMPTY);
    }
    if (in_array('conquest', $tokens, true) || in_array('hardpoint', $tokens, true)) array_push($tokens, 'conquest', 'hardpoint');
    if (in_array('deathmatch', $tokens, true) || in_array('killconfirmed', $tokens, true)) array_push($tokens, 'deathmatch', 'killconfirmed');
    return array_values(array_intersect(array_keys(OFP_MAP_MODES), $tokens));
}
