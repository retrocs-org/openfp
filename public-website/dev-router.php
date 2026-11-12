<?php
// SPDX-License-Identifier: MIT
// Try the site on your own machine, as it is on the real server
// (the pages at /, /server/, /play/, the listing API at /api/):
//
//   php -S 127.0.0.1:8080 public-website/dev-router.php
//
// Not for production: see README.md here for Apache.
$uri = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
if (preg_match('#^/(inc|data)(/|$)|config\.inc|dev-router|\.(md|sql)$#', $uri)) { http_response_code(404); exit; }
$path = __DIR__ . $uri;
if (is_dir($path)) $path = rtrim($path, '/') . '/index.php';
if (!is_file($path)) { http_response_code(404); echo "not found\n"; exit; }
if (substr($path, -4) === '.php') {
    chdir(dirname($path));
    $_SERVER['SCRIPT_FILENAME'] = $path;
    require $path;
    return true;
}
$types = ['css' => 'text/css', 'js' => 'application/javascript', 'png' => 'image/png', 'svg' => 'image/svg+xml',
          'ico' => 'image/x-icon', 'json' => 'application/json'];
header('Content-Type: ' . ($types[strtolower(pathinfo($path, PATHINFO_EXTENSION))] ?? 'application/octet-stream'));
readfile($path);
return true;
