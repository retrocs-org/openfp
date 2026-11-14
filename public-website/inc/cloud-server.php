<?php
// SPDX-License-Identifier: MIT
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
$name=$argv[1]??'';
if (!preg_match('/\A[a-z0-9][a-z0-9_-]{0,31}\z/D',$name)) { fwrite(STDERR,"usage: php inc/cloud-server.php SERVER_NAME\n"); exit(1); }
$secret=bin2hex(random_bytes(32));
echo "Add to config.inc.php accounts.servers:\n  '$name' => '",hash('sha256',$secret),"',\n\nAdd to the dedicated server configuration (keep the secret private):\ncloud_server = $name\ncloud_secret = $secret\n";
