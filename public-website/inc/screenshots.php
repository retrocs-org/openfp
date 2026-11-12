<?php
// SPDX-License-Identifier: MIT
// Full rendered frames: dated filenames avoid stale deployment/browser caches.
function site_screenshots(): array
{
    return [
        ['20261111-desktop-classic', 'Classic on desktop', 'Capture the flag and fight through destructible bases.', 1152, 648, true],
        ['20261111-desktop-editor', 'Desktop map editor', 'Create maps, place buildings and shape team bases.', 768, 432, true],
        ['20261111-desktop-split', 'Four-player split screen', 'Four players share the battlefield in the macOS client.', 1152, 648, true],
        ['amiga-editor', 'AmigaOS map editor', 'Build your own battlefield with the native AmigaOS editor.', 960, 768, false],
        ['20261111-amiga-split', 'AmigaOS split screen', 'Two players share the battlefield in the native 16-colour client.', 320, 256, false],
        ['20261111-amiga-winter', 'AmigaOS · Winter in 16 colours', 'Snow-covered terrain, vehicles and a compact HUD in the native 16-colour client.', 320, 256, false],
        ['20261111-r2-desktop-jungle', 'Jungle battlefield', 'Dense green terrain and destructible compounds in an enhanced world.', 1152, 648, false],
        ['20261111-r2-desktop-swamp', 'Swamp battlefield', 'Dark marsh terrain, water crossings and a nearby tactical radar.', 1152, 648, false],
        ['20261111-desktop-winter', 'Winter battlefield', 'Snow-covered terrain in an enhanced world.', 1152, 648, false],
        ['20261111-desktop-moon', 'Moon Base battlefield', 'A dry lunar world with crater trenches instead of water.', 1152, 648, false],
        ['20261111-desktop-volcano', 'Volcano battlefield', 'Volcanic terrain and its own vehicle artwork.', 1152, 648, false],
        ['20261111-desktop-overview', 'Map overview and team areas', 'See the full battlefield and jump to another part of the editor.', 768, 432, false],
    ];
}
function render_screenshots(bool $highlights = false, bool $extras = false): void
{
    echo '<div class="gallery screenshots">';
    foreach (site_screenshots() as [$file, $title, $description, $width, $height, $highlight]) {
        if (($highlights && !$highlight) || ($extras && $highlight)) continue;
        $src = '/assets/img/' . $file . '.png';
        echo '<figure><a href="' . h($src) . '" data-zoom><img src="' . h($src) . '" alt="' . h($title)
           . '" width="' . $width . '" height="' . $height . '" loading="lazy"></a><figcaption><b>'
           . h($title) . '</b><span>' . h($description) . '</span></figcaption></figure>';
    }
    echo '</div>';
}
