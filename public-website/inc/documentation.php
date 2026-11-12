<?php
// SPDX-License-Identifier: MIT
require_once __DIR__ . '/site.php';

function documentation_guides(): array
{
    return [
        'overview' => ['Documentation', 'Guides for players, map makers and server operators.'],
        'gameplay' => ['Gameplay', 'Learn the objectives, tank combat and different game modes.'],
        'clients' => ['Clients & controls', 'Install a client, sign in, set up your controls and join a game.'],
        'server' => ['Server setup', 'Run and configure a Linux dedicated server.'],
        'maps' => ['Map editor', 'Create a battlefield, build team bases and share your maps.'],
        'troubleshooting' => ['Troubleshooting', 'Check connections, performance and common setup problems.'],
    ];
}

function documentation_start(string $guide): void
{
    [$title, $description] = documentation_guides()[$guide];
    page_start($title, 'documentation', $description);
    echo '<section class="wrap prose documentation-page"><p class="eyebrow">OpenFP / Guides</p><h1>' . h($title) . '</h1><p class="lead">' . h($description) . '</p>';
    echo '<nav class="section-nav documentation-nav" aria-label="Documentation guides">';
    foreach (documentation_guides() as $key => [$label]) {
        $url = '/documentation/' . ($key === 'overview' ? '' : $key . '/');
        echo '<a href="' . h($url) . '"' . ($key === $guide ? ' aria-current="page"' : '') . '>' . h($key === 'overview' ? 'Overview' : $label) . '</a>';
    }
    echo '</nav>';
}

function documentation_end(): void
{
    echo '</section>';
    page_end();
}
