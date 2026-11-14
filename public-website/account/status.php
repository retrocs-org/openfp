<?php
// SPDX-License-Identifier: MIT
// The header only needs the signed-in name and selected avatar.
require __DIR__ . '/../inc/accounts.php';
header('Cache-Control: private, no-store');
header('Content-Type: application/json; charset=utf-8');
header('X-Content-Type-Options: nosniff');
header('Vary: Cookie');
$profile = null;
if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'GET') {
    header('Allow: GET'); http_response_code(405);
} elseif (ofp_account_https() && ofp_account_config()['enabled'] &&
          is_string($_COOKIE['ofp_account'] ?? null) && $_COOKIE['ofp_account'] !== '') {
    session_name('ofp_account');
    session_set_cookie_params(['secure'=>true, 'httponly'=>true, 'samesite'=>'Lax', 'path'=>'/account/']);
    ini_set('session.use_strict_mode', '1');
    session_start(['read_and_close'=>true]);
    if (is_string($_SESSION['account'] ?? null) && is_string($_SESSION['password_version'] ?? null)) {
        try {
            $db = ofp_db();
            $q = $db->prepare('SELECT username, password_hash, email_verified FROM accounts WHERE id = ?');
            $q->execute([$_SESSION['account']]); $account = $q->fetch(PDO::FETCH_ASSOC);
            if ($account && ofp_account_active($account) &&
                hash_equals(hash('sha256', $account['password_hash']), $_SESSION['password_version'])) {
                $preferences = ofp_account_preferences($db, $_SESSION['account']);
                $profile = ['username'=>$account['username'], 'avatar'=>max(0, min(9, (int)($preferences['avatar'] ?? 0)))];
            }
        } catch (Throwable $e) {
            error_log('OpenFP account header unavailable: ' . $e->getMessage());
            http_response_code(503);
        }
    }
}
echo json_encode(['profile'=>$profile]);
