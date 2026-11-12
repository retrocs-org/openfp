<?php
// SPDX-License-Identifier: MIT
// Public tagged downloads; no local package is advertised as a published release.
const OFP_RELEASE_GITHUB = 'https://github.com/retrocs-org/openfp';
const OFP_RELEASE_LIMIT = 2097152;

function ofp_release_hosts(): array
{
    return [
        'github' => ['repo' => OFP_RELEASE_GITHUB, 'api' => 'https://api.github.com/repos/retrocs-org/openfp/releases?per_page=30'],
    ];
}

function ofp_release_payload(array $raw, string $provider, int $now): ?array
{
    if ($provider !== 'github') return null;
    $tag = $raw['tag_name'] ?? '';
    if (!is_string($tag) || !preg_match('/^v?([0-9]+\.[0-9]+(?:\.[0-9]+)?)$/D', $tag, $match)) return null;
    $version = $match[1];
    if (($raw['draft'] ?? true) !== false || ($raw['prerelease'] ?? true) !== false) return null;
    $date = $raw['published_at'] ?? null;
    if (!is_string($date) || ($published = strtotime($date)) === false || $published > $now) return null;
    $repo = OFP_RELEASE_GITHUB;
    $items = $raw['assets'] ?? null;
    if (!is_array($items)) return null;
    $assets = [];
    foreach ($items as $item) {
        if (!is_array($item)) continue;
        $name = $item['name'] ?? '';
        if (!is_string($name) || !preg_match('/^[A-Za-z0-9][A-Za-z0-9._-]{0,180}$/D', $name) || isset($assets[$name])) continue;
        $url = $item['browser_download_url'] ?? '';
        $expected = $repo . '/releases/download/' . rawurlencode($tag) . '/' . rawurlencode($name);
        if (($item['state'] ?? '') !== 'uploaded' || $url !== $expected || !is_int($item['size'] ?? null) || $item['size'] <= 0) continue;
        $assets[$name] = ['name' => $name, 'url' => $url, 'size' => $item['size']];
    }
    return ['tag' => $tag, 'version' => $version, 'published_at' => $published, 'provider' => $provider,
        'url' => $repo . '/releases/tag/' . rawurlencode($tag), 'assets' => $assets];
}

function ofp_release_fetch(string $provider): array
{
    $url = ofp_release_hosts()[$provider]['api'];
    $headers = ['User-Agent: OpenFP-website/0.1', 'Accept: application/json'];
    if (function_exists('curl_init')) {
        $curl = curl_init($url);
        $body = '';
        curl_setopt_array($curl, [CURLOPT_HTTPHEADER => $headers, CURLOPT_CONNECTTIMEOUT => 2, CURLOPT_TIMEOUT => 4,
            CURLOPT_FOLLOWLOCATION => false, CURLOPT_SSL_VERIFYPEER => true, CURLOPT_SSL_VERIFYHOST => 2,
            CURLOPT_WRITEFUNCTION => static function ($handle, string $chunk) use (&$body): int {
                if (strlen($body) + strlen($chunk) > OFP_RELEASE_LIMIT) return 0;
                $body .= $chunk;
                return strlen($chunk);
            }]);
        $ok = curl_exec($curl);
        $status = $ok === false ? 0 : (int)curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        unset($curl);
        return ['status' => $status, 'body' => $body];
    }
    $context = stream_context_create(['http' => ['header' => implode("\r\n", $headers), 'timeout' => 4,
        'follow_location' => 0, 'ignore_errors' => true], 'ssl' => ['verify_peer' => true, 'verify_peer_name' => true]]);
    $http_response_header = [];
    $body = @file_get_contents($url, false, $context, 0, OFP_RELEASE_LIMIT + 1);
    preg_match('/\s([0-9]{3})\s/', $http_response_header[0] ?? '', $m);
    return ['status' => is_string($body) && strlen($body) <= OFP_RELEASE_LIMIT ? (int)($m[1] ?? 0) : 0,
        'body' => is_string($body) ? $body : ''];
}

function ofp_release_cache_write(string $path, array $value): void
{
    if (is_link($path) || !is_dir(dirname($path))) return;
    $temporary = @tempnam(dirname($path), 'openfp-releases-');
    if ($temporary === false) return;
    @chmod($temporary, 0600);
    if (@file_put_contents($temporary, json_encode($value, JSON_UNESCAPED_SLASHES), LOCK_EX) !== false) @rename($temporary, $path);
    if (is_file($temporary)) @unlink($temporary);
}

function ofp_release_catalog(?callable $fetch = null, ?string $cachePath = null, ?int $now = null): array
{
    $now ??= time();
    $cachePath ??= sys_get_temp_dir() . '/openfp-releases-' . hash('sha256', __DIR__) . '.json';
    $cache = [];
    if (!is_link($cachePath) && is_file($cachePath) && filesize($cachePath) <= OFP_RELEASE_LIMIT * 3) {
        $cache = json_decode((string)@file_get_contents($cachePath), true) ?: [];
        if (!is_array($cache)) $cache = [];
    }
    // Discard cached providers that are no longer public release sources.
    $cache = array_intersect_key($cache, ofp_release_hosts());
    $offline = $fetch === null && getenv('OFP_RELEASE_OFFLINE') === '1';
    $fetch ??= 'ofp_release_fetch';
    $releases = []; $failed = false;
    foreach (ofp_release_hosts() as $provider => $host) {
        $record = $cache[$provider] ?? [];
        if (!is_array($record)) $record = [];
        $age = $now - (int)($record['checked_at'] ?? 0);
        if ($age < 0 || $age >= (!empty($record['failed']) ? 60 : 300) || !$record || $offline) {
            $payload = null;
            try {
                $response = $offline ? ['status' => 0] : $fetch($provider);
                if (($response['status'] ?? 0) === 404) $payload = [];
                elseif (($response['status'] ?? 0) === 200 && is_string($response['body'] ?? null) && strlen($response['body']) <= OFP_RELEASE_LIMIT) {
                    $decoded = json_decode($response['body'], true);
                    if (is_array($decoded) && ($decoded === [] || array_keys($decoded) === range(0, count($decoded) - 1)) && count($decoded) <= 30) $payload = $decoded;
                }
            } catch (Throwable $error) { /* A release host outage must not break the site. */ }
            if ($payload !== null) $record = ['checked_at' => $now, 'confirmed_at' => $now, 'failed' => false, 'payload' => $payload];
            else $record = array_merge($record, ['checked_at' => $now, 'failed' => true]);
        }
        $cache[$provider] = $record;
        $failed = $failed || !empty($record['failed']);
        $confirmedAge = $now - (int)($record['confirmed_at'] ?? 0);
        if ($confirmedAge < 0 || $confirmedAge >= 86400 || !is_array($record['payload'] ?? null)) continue;
        foreach ($record['payload'] as $raw) {
            if (is_array($raw) && ($release = ofp_release_payload($raw, $provider, $now)) !== null) $releases[] = $release;
        }
    }
    if (!$offline) ofp_release_cache_write($cachePath, $cache);
    usort($releases, static function (array $a, array $b): int {
        return version_compare($b['version'], $a['version']) ?: (strcmp($a['provider'], $b['provider']) ?: ($b['published_at'] <=> $a['published_at']));
    });
    $groups = [];
    foreach ($releases as $release) {
        $v = $release['version'];
        $names = [
            'macos' => ["openfp-client-macos-universal-$v.dmg" => 'DMG', "openfp-client-macos-universal-$v.zip" => 'ZIP'],
            'windows' => ["openfp-client-windows-x86_64-$v.msi" => 'MSI', "openfp-client-windows-x86_64-$v.zip" => 'ZIP'],
            'linux-x86_64' => ["openfp-client-linux-x86_64-$v.tar.gz" => 'tar.gz'],
            'linux-aarch64' => ["openfp-client-linux-aarch64-$v.tar.gz" => 'tar.gz'],
            'amiga' => ["openfp-client-amigaos-m68k-$v.lha" => 'Installer LHA'],
            'server-x86_64' => ["openfp-server-linux-x86_64-$v.tar.gz" => 'Server tar.gz'],
            'server-aarch64' => ["openfp-server-linux-aarch64-$v.tar.gz" => 'Server tar.gz'],
            'source' => ["OpenFirePower-$v-source.tar.gz" => 'Download full source'],
        ];
        foreach ($names as $key => $files) {
            if (isset($groups[$key]) && $groups[$key]['version'] !== $v) continue;
            foreach ($files as $name => $label) {
                if (!isset($release['assets'][$name])) continue;
                if (!isset($groups[$key])) $groups[$key] = array_diff_key($release, ['assets' => true]) + ['downloads' => []];
                if (isset($groups[$key]['downloads'][$name])) continue;
                $groups[$key]['downloads'][$name] = $release['assets'][$name] + ['label' => $label,
                    'checksum' => $release['assets'][$name . '.sha256']['url'] ?? ($release['assets']['SHA256SUMS']['url'] ?? null)];
            }
        }
    }
    return ['status' => $failed ? ($releases ? 'stale' : 'unavailable') : ($releases ? 'ready' : 'empty'),
        'groups' => $groups, 'version' => $releases[0]['version'] ?? ''];
}

function site_releases(): array
{
    static $catalog = null;
    return $catalog ??= ofp_release_catalog();
}

function render_release_actions(?array $group, string $status): void
{
    if (!$group) {
        ?><p class="release-pending"><?= $status === 'unavailable' ? 'Release availability could not be checked.' : 'Not released yet.' ?></p><?php
        return;
    }
    ?>
    <div class="download-options">
      <?php foreach ($group['downloads'] as $download): ?>
        <div class="download-action"><a class="btn" href="<?= h($download['url']) ?>"><?= h($download['label']) ?></a>
          <?php if ($download['size']): ?><span class="small muted"><?= h(number_format($download['size'] / 1048576, 1)) ?> MB</span><?php endif; ?>
          <?php if ($download['checksum']): ?><a class="small" href="<?= h($download['checksum']) ?>">SHA-256</a><?php endif; ?>
        </div>
      <?php endforeach; ?>
    </div>
    <p class="release-meta">Version <?= h($group['version']) ?> · <?= h(gmdate('j M Y', $group['published_at'])) ?> · <a href="<?= h($group['url']) ?>">Release notes</a></p>
    <?php
}

function render_release_downloads(array $catalog): void
{
    $groups = $catalog['groups']; $status = $catalog['status'];
    ?>
    <section class="game-download-section" aria-labelledby="play-game">
    <div class="download-heading" id="download"><p class="kicker">Game clients</p><h2 id="play-game">Play a Game</h2>
      <p>Choose a client for your platform. Play locally or join an online server.</p></div>
    <?php if ($status === 'stale'): ?><p class="notice small">A release host could not be refreshed. These are recently confirmed links; a newer release may be available.</p><?php endif; ?>
    <?php if ($status === 'unavailable'): ?><p class="notice small">We couldn’t reach GitHub. Please check <a href="<?= h(OFP_RELEASE_GITHUB) ?>/releases">the releases page</a>.</p><?php endif; ?>
    <ul class="client-downloads">
      <?php foreach ([
          'macos' => ['macOS', 'Apple silicon + Intel · universal app', 'Drag the app from the DMG to Applications, or extract the ZIP.'],
          'windows' => ['Windows', 'Intel / AMD · x86_64', 'Install the MSI, or extract the portable ZIP.'],
          'linux-x86_64' => ['Linux', 'Intel / AMD · x86_64', 'Ubuntu 24.04 or newer. Extract the archive and run Play.'],
          'linux-aarch64' => ['Linux', 'ARM64 · aarch64', 'Ubuntu 24.04 or newer. Extract the archive and run Play.'],
          'amiga' => ['AmigaOS', 'm68k · 16 colours · 68040 / 68060', 'AmigaOS 3.x, 2 MB Chip RAM and 8 MB Fast RAM. Extract the LHA and run Install.'],
      ] as $key => [$title, $architecture, $description]): ?>
        <li class="client-download-row">
          <div><h3><?= h($title) ?></h3><p class="download-architecture"><?= h($architecture) ?></p><p class="note"><?= h($description) ?></p></div>
          <div class="client-download-actions"><?php render_release_actions($groups[$key] ?? null, $status); ?></div>
        </li>
      <?php endforeach; ?>
    </ul>
    </section>
    <section class="server-download-panel" id="server-downloads" aria-labelledby="host-game">
      <div class="server-download-heading"><div><p class="kicker">Linux dedicated server</p><h2 id="host-game">Host a Game</h2></div><span class="tag">Headless · up to 8 players</span></div>
      <p>The server runs the match and bots for clients on every supported platform. No desktop or SDL is required. Choose the architecture of your Linux host.</p>
      <div class="server-download-options">
        <?php foreach (['x86_64' => 'Intel / AMD · x86_64', 'aarch64' => 'ARM64 · aarch64'] as $arch => $label): ?>
          <div class="server-download-row"><h4><?= h($label) ?></h4><?php render_release_actions($groups['server-' . $arch] ?? null, $status); ?></div>
        <?php endforeach; ?>
      </div>
      <p class="note">Ubuntu 24.04 or newer. Extract the archive, copy <code>server.cfg.example</code> to <code>server.cfg</code>, review it, then run <code>./ofp_server -c server.cfg</code>.</p>
      <a href="/source/#server">Server setup →</a>
    </section>
    <div class="release-links"><a href="<?= h(OFP_RELEASE_GITHUB) ?>/releases">All GitHub releases</a></div>
    <?php
}
