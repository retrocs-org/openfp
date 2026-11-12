<?php
// SPDX-License-Identifier: MIT
// Offline checks: fixture release metadata, never real uploads or credentials.
require __DIR__ . '/../public-website/inc/site.php';
function check_release(bool $condition, string $message): void {
    if (!$condition) throw new RuntimeException($message);
}
function fixture_release(string $provider, string $version = '0.1', ?array $files = null): array {
    $files ??= ["openfp-client-macos-universal-$version.dmg", "openfp-client-macos-universal-$version.zip", "openfp-client-amigaos-m68k-$version.lha",
        "openfp-client-linux-x86_64-$version.tar.gz", "openfp-server-linux-x86_64-$version.tar.gz", "OpenFirePower-$version-source.tar.gz"];
    $tag = 'v' . $version;
    $raw = ['tag_name' => $tag, 'draft' => false, 'prerelease' => false,
        'published_at' => '2026-11-01T12:00:00Z', 'released_at' => '2026-11-01T12:00:00Z'];
    $items = [];
    foreach ($files as $name) {
        $items[] = ['name' => $name, 'size' => 1234567, 'state' => 'uploaded',
            'browser_download_url' => OFP_RELEASE_GITHUB . '/releases/download/' . $tag . '/' . $name,
            'url' => 'https://gitlab.kladhest.se/api/v4/projects/retrocs-org%2Fopenfp/packages/generic/openfirepower/' . $version . '/' . $name];
    }
    $raw['assets'] = $provider === 'github' ? $items : ['links' => $items];
    return $raw;
}
$folder = sys_get_temp_dir() . '/ofp-website-release-tests-' . bin2hex(random_bytes(8));
mkdir($folder, 0700);
$now = strtotime('2026-11-12T12:00:00Z');
try {
    check_release(site_release_version() === '0.1', 'Source examples remain at the explicit version');
    foreach (['github', 'gitlab'] as $host) {
        $raw = fixture_release($host);
        $parsed = ofp_release_payload($raw, $host, $now);
        check_release($parsed !== null && count($parsed['assets']) === 6, 'Parse ' . $host . ' package links');
        $bad = $raw; $bad['tag_name'] = 'development';
        check_release(ofp_release_payload($bad, $host, $now) === null, 'Only stable version tags');
        $bad = $raw; $bad['tag_name'] = '0.1-rc1';
        check_release(ofp_release_payload($bad, $host, $now) === null, 'Exclude prerelease tags');
        $bad = $raw; $bad['published_at'] = $bad['released_at'] = '2099-01-01T00:00:00Z';
        check_release(ofp_release_payload($bad, $host, $now) === null, 'Exclude future publications');
    }
    foreach (['draft', 'prerelease'] as $flag) {
        $bad = fixture_release('github'); $bad[$flag] = true;
        check_release(ofp_release_payload($bad, 'github', $now) === null, 'Exclude ' . $flag);
    }
    $bad = fixture_release('gitlab'); $bad['upcoming_release'] = true;
    check_release(ofp_release_payload($bad, 'gitlab', $now) === null, 'Exclude upcoming GitLab releases');
    foreach (['https://evil.invalid/file.zip', OFP_RELEASE_GITHUB . '/releases/download/v0.2/openfp-client-macos-universal-0.1.dmg'] as $url) {
        $bad = fixture_release('github'); $bad['assets'][0]['browser_download_url'] = $url;
        check_release(count(ofp_release_payload($bad, 'github', $now)['assets']) === 5, 'Reject unrelated host/tag');
    }
    $bad = fixture_release('gitlab'); $bad['assets']['links'][0]['url'] = 'https://gitlab.kladhest.se/another/project/app.dmg';
    check_release(count(ofp_release_payload($bad, 'gitlab', $now)['assets']) === 5, 'Reject unrelated project');
    $empty = static fn($host) => ['status' => 200, 'body' => '[]'];
    $none = ofp_release_catalog($empty, $folder . '/empty.json', $now);
    check_release($none['status'] === 'empty' && $none['groups'] === [], 'No releases before first upload');
    ob_start(); render_release_downloads($none); $html = ob_get_clean();
    check_release(substr_count($html, 'Not released yet.') === 7, 'Each client/server has explicit empty state');
    check_release(strpos($html, '/assets/releases/') === false, 'Local packages never become published downloads');
    $missing = ofp_release_catalog(static fn($host) => ['status' => 404], $folder . '/404.json', $now);
    check_release($missing['status'] === 'empty', 'Missing public repository/release means not released');
    $calls = 0;
    $fetch = static function ($host) use (&$calls): array {
        $calls++;
        $raw = fixture_release($host);
        if ($host === 'gitlab') $raw['released_at'] = '2026-11-02T12:00:00Z';
        $raw = [$raw];
        return ['status' => 200, 'body' => json_encode($raw)];
    };
    $path = $folder . '/ready.json';
    $catalog = ofp_release_catalog($fetch, $path, $now);
    check_release($catalog['groups']['macos']['provider'] === 'github', 'Prefer GitHub over mirror');
    check_release(isset($catalog['groups']['server-x86_64']) && !isset($catalog['groups']['windows']), 'Server bundle and missing-platform separation');
    check_release(count($catalog['groups']['macos']['downloads']) === 2, 'Deduplicate mirrored assets');
    check_release($catalog['groups']['macos']['downloads']['openfp-client-macos-universal-0.1.dmg']['checksum'] === null, 'Do not invent checksums');
    ofp_release_catalog($fetch, $path, $now + 10);
    check_release($calls === 2, 'Cache prevents per-page API calls');
    $fail = static fn($host) => ['status' => 403, 'body' => 'rate limited'];
    check_release(ofp_release_catalog($fail, $path, $now + 301)['status'] === 'stale', 'Label cached links during failure');
    check_release(ofp_release_catalog($fail, $path, $now + 90000)['status'] === 'unavailable', 'Expire old links');
    $fallback = ofp_release_catalog(static fn($host) => $host === 'github' ? ['status' => 0] : ['status' => 200, 'body' => json_encode([fixture_release($host)])], $folder . '/fallback.json', $now);
    check_release($fallback['groups']['macos']['provider'] === 'gitlab', 'GitLab works during GitHub outage');
    $split = ofp_release_catalog(static fn($host) => ['status' => 200, 'body' => json_encode([fixture_release($host, '0.1', $host === 'github' ? ['openfp-client-macos-universal-0.1.dmg'] : ['openfp-client-windows-x86_64-0.1.zip'])])], $folder . '/split.json', $now);
    check_release(isset($split['groups']['macos'], $split['groups']['windows']), 'Combine separately available platform packages');
    $clientOnly = ofp_release_catalog(static fn($host) => ['status' => 200, 'body' => json_encode([fixture_release($host, '0.1', ['openfp-client-linux-x86_64-0.1.tar.gz'])])], $folder . '/client-only.json', $now);
    check_release(isset($clientOnly['groups']['linux-x86_64']) && !isset($clientOnly['groups']['server-x86_64']), 'Client archive never masquerades as server');
    ob_start(); render_release_downloads($catalog); $rendered = ob_get_clean();
    check_release(substr_count($rendered, 'class="client-download-row"') === 5 && substr_count($rendered, 'class="server-download-panel"') === 1, 'Compact clients and one separate server panel');
    check_release(strpos($rendered, 'download-card') === false, 'No large per-client cards');
    $mismatch = fixture_release('github', '0.2', ['openfp-client-macos-universal-0.1.dmg']);
    $matched = ofp_release_catalog(static fn($host) => ['status' => 200, 'body' => json_encode([$mismatch])], $folder . '/mismatch.json', $now);
    check_release($matched['groups'] === [], 'Never attach wrong-version package to a tag');
    $latest = ofp_release_catalog(static fn($host) => ['status' => 200, 'body' => json_encode([fixture_release($host), fixture_release($host, '0.2')])], $folder . '/latest.json', $now);
    check_release($latest['groups']['amiga']['version'] === '0.2', 'Discover later releases without changing app version');
    $checksums = fixture_release('github', '0.1', ['openfp-client-macos-universal-0.1.dmg', 'openfp-client-macos-universal-0.1.dmg.sha256']);
    $withSums = ofp_release_catalog(static fn($host) => $host === 'github' ? ['status' => 200, 'body' => json_encode([$checksums])] : ['status' => 200, 'body' => '[]'], $folder . '/sums.json', $now);
    check_release(str_ends_with($withSums['groups']['macos']['downloads']['openfp-client-macos-universal-0.1.dmg']['checksum'], '.sha256'), 'Use attached checksum');
    check_release(ofp_release_catalog($empty, $path, $now + 91000)['groups'] === [], 'Deleted releases disappear after refresh');
    foreach ([['status' => 200, 'body' => '<html>bad gateway</html>'], ['status' => 200, 'body' => '{"message":"error"}'], ['status' => 500]] as $response) {
        $result = ofp_release_catalog(static fn($host) => $response, $folder . '/failure-' . count(glob($folder . '/*')) . '.json', $now);
        check_release($result['status'] === 'unavailable' && !$result['groups'], 'Do not mistake API errors for releases');
    }
    $throw = static function ($host) { throw new RuntimeException('timeout'); };
    check_release(ofp_release_catalog($throw, $folder . '/throw.json', $now)['status'] === 'unavailable', 'Handle network exceptions');
    echo "Website tagged-release, platform/server download, mirror, checksum and cache checks passed.\n";
} finally {
    foreach (glob($folder . '/*') as $file) unlink($file);
    rmdir($folder);
}
