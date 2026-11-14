<?php
// SPDX-License-Identifier: MIT
require_once __DIR__ . '/listing.php';

function ofp_account_config(): array {
    $c = array_merge(['enabled' => true, 'session_days' => 30, 'password_min_length' => 12,
        'require_email_verification' => true, 'servers' => []], ofp_settings()['accounts'] ?? []);
    $c['password_min_length'] = max(4, min(72, (int)$c['password_min_length']));
    return $c;
}
function ofp_account_active(array $account): bool {
    return !ofp_account_config()['require_email_verification'] || !empty($account['email_verified']);
}
function ofp_account_id_active(PDO $db, string $id): bool {
    $q = $db->prepare('SELECT email_verified FROM accounts WHERE id = ?'); $q->execute([$id]);
    $a = $q->fetch(PDO::FETCH_ASSOC);
    return $a && ofp_account_active($a);
}
function ofp_account_https(): bool {
    if (($_SERVER['HTTPS'] ?? '') !== '' && $_SERVER['HTTPS'] !== 'off') return true;
    // Forwarded TLS is trusted only from configured reverse proxies.
    foreach (ofp_config()['trusted_proxies'] as $proxy)
        if (ofp_ip_in($_SERVER['REMOTE_ADDR'] ?? '', [$proxy]) && ($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https') return true;
    return false;
}
function ofp_account_password(string $password): string {
    $min = ofp_account_config()['password_min_length'];
    $length = preg_match_all('/./us', $password);
    if ($length === false || $length < $min || strlen($password) > 72 || str_contains($password, "\0"))
        throw new InvalidArgumentException("Use at least $min characters for your password, with a maximum of 72 bytes.");
    return password_hash($password, PASSWORD_DEFAULT);
}
function ofp_account_register(PDO $db, string $name, string $email, string $password): string {
    $name = trim($name); $email = strtolower(trim($email));
    if (!preg_match('/\A[A-Za-z][A-Za-z0-9_]{2,14}\z/D', $name))
        throw new InvalidArgumentException('Username: 3–15 letters, numbers or underscores; start with a letter.');
    if (strlen($email) > 254 || !filter_var($email, FILTER_VALIDATE_EMAIL)) throw new InvalidArgumentException('Enter a valid email address.');
    $hash = ofp_account_password($password);
    $verify = (bool)ofp_account_config()['require_email_verification'];
    require_once __DIR__ . '/smtp.php';
    if ($verify && !ofp_smtp_ready())
        throw new InvalidArgumentException('Account creation is unavailable: email activation needs the site mail service.');
    $id = bin2hex(random_bytes(16));
    try {
        $db->prepare('INSERT INTO accounts(id, username, username_key, email, password_hash, player_key, created, password_changed, email_verified) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)')
           ->execute([$id, $name, strtolower($name), $email, $hash, bin2hex(random_bytes(32)), time(), time(), $verify ? 0 : 1]);
    } catch (PDOException $e) {
        if (in_array((string)$e->getCode(), ['23000', '23505'], true)) throw new InvalidArgumentException('That username or email is already registered.');
        throw $e;
    }
    if ($verify) {
        try { ofp_account_activation_request($db, $email); }
        catch (Throwable $e) {
            error_log('OpenFP activation mail failed.');
            throw new InvalidArgumentException('Account created, but the activation email could not be sent. Try Resend activation email in a minute.');
        }
    }
    return $id;
}
function ofp_account_login(PDO $db, string $name, string $password): ?array {
    $q = $db->prepare('SELECT * FROM accounts WHERE username_key = ?'); $q->execute([strtolower(trim($name))]);
    $a = $q->fetch(PDO::FETCH_ASSOC);
    // A missing user still pays the password-verification cost.
    $hash = $a['password_hash'] ?? '$2y$12$LmICxz4c2yJPFG3X2nXX7.vFvJ1rGCgUNnjZcbHLmK6G7EKYH7O12';
    $ok = strlen($password) <= 72 && !str_contains($password, "\0") && password_verify($password, $hash);
    if (!$ok || !$a) return null;
    if (!ofp_account_active($a))
        throw new InvalidArgumentException('Activate your account using the email link before signing in.');
    if (password_needs_rehash($a['password_hash'], PASSWORD_DEFAULT))
        $db->prepare('UPDATE accounts SET password_hash = ? WHERE id = ?')->execute([$a['password_hash'] = password_hash($password, PASSWORD_DEFAULT), $a['id']]);
    return $a;
}
function ofp_account_session(PDO $db, string $id): string {
    if (!ofp_account_id_active($db, $id)) throw new InvalidArgumentException('Activate your account before signing in.');
    $token = bin2hex(random_bytes(32));
    $days = max(1, min(90, (int)ofp_account_config()['session_days']));
    $db->prepare('INSERT INTO account_sessions(token_hash, account_id, expires) VALUES(?, ?, ?)')->execute([hash('sha256', $token), $id, time() + $days * 86400]);
    return $token;
}
function ofp_account_from_token(PDO $db, string $token): ?array {
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $token)) return null;
    $q = $db->prepare('SELECT a.* FROM accounts a JOIN account_sessions s ON s.account_id = a.id WHERE s.token_hash = ? AND s.expires > ?');
    $q->execute([hash('sha256', $token), time()]); $a = $q->fetch(PDO::FETCH_ASSOC);
    return $a && ofp_account_active($a) ? $a : null;
}
function ofp_account_rate(PDO $db, string $scope, int $limit, int $seconds): bool {
    $key = hash('sha256', $scope);
    // Serialize each limiter bucket; works across PHP workers and ignores client sessions.
    $db->beginTransaction();
    try {
        $driver = $db->getAttribute(PDO::ATTR_DRIVER_NAME);
        $sql = $driver === 'sqlite' ? 'INSERT OR IGNORE INTO account_limits(bucket, started, attempts) VALUES(?, ?, 0)' :
               'INSERT IGNORE INTO account_limits(bucket, started, attempts) VALUES(?, ?, 0)';
        $db->prepare($sql)->execute([$key, time()]);
        $q = $db->prepare('SELECT started, attempts FROM account_limits WHERE bucket = ?' . ($driver === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([$key]); $v = $q->fetch(PDO::FETCH_ASSOC);
        $fresh = (int)$v['started'] <= time() - $seconds;
        $n = $fresh ? 1 : (int)$v['attempts'] + 1;
        $db->prepare('UPDATE account_limits SET started = ?, attempts = ? WHERE bucket = ?')->execute([$fresh ? time() : $v['started'], min(1000000, $n), $key]);
        $db->commit(); return $n <= $limit;
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}
function ofp_cloud_server(string $id, string $secret): bool {
    $hash = ofp_account_config()['servers'][$id] ?? '';
    return preg_match('/\A[0-9a-f]{64}\z/D', $secret) && strlen($hash) === 64 && hash_equals($hash, hash('sha256', $secret));
}
function ofp_cloud_ticket(PDO $db, array $a, string $server, string $serverId, string $nonce, string $keyId): string {
    if (!ofp_account_id_active($db, $a['id'])) throw new InvalidArgumentException('Activate your account before joining a public server.');
    if (!isset(ofp_account_config()['servers'][$server]) || !preg_match('/\A[0-9a-f]{32}\z/D', $serverId) ||
        !preg_match('/\A[0-9a-f]{16}\z/D', $nonce) || !preg_match('/\A[0-9a-f]{32}\z/D', $keyId)) throw new InvalidArgumentException('Unknown server or invalid challenge.');
    $verifier = hash('sha256', hex2bin($a['player_key']) . hex2bin($serverId), true);
    if (!hash_equals(substr(hash('sha256', $verifier), 0, 32), $keyId)) throw new InvalidArgumentException('Profile key does not match the cloud account.');
    $token = bin2hex(random_bytes(32));
    $db->prepare('INSERT INTO account_tickets(token_hash, account_id, server, server_id, nonce, key_id, expires) VALUES(?, ?, ?, ?, ?, ?, ?)')
       ->execute([hash('sha256', $token), $a['id'], $server, $serverId, $nonce, $keyId, time() + 60]);
    return $token;
}
function ofp_cloud_verify(PDO $db, string $ticket, string $server, string $serverId, string $nonce, string $keyId): ?array {
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $ticket)) return null;
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT a.id, a.username, a.email_verified FROM accounts a JOIN account_tickets t ON a.id = t.account_id WHERE t.token_hash = ? AND t.server = ? AND t.server_id = ? AND t.nonce = ? AND t.key_id = ? AND t.expires > ?');
        $q->execute([hash('sha256', $ticket), $server, $serverId, $nonce, $keyId, time()]); $a = $q->fetch(PDO::FETCH_ASSOC);
        if (!$a || !ofp_account_active($a)) { $db->rollBack(); return null; }
        $q = $db->prepare('DELETE FROM account_tickets WHERE token_hash = ?'); $q->execute([hash('sha256', $ticket)]);
        $used = $q->rowCount(); $db->commit(); return $a && $used === 1 ? $a : null;
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}
function ofp_account_activation_request(PDO $db, string $email): void {
    require_once __DIR__ . '/smtp.php';
    if (!ofp_smtp_ready()) throw new InvalidArgumentException('Activation email is unavailable. Contact the site administrator.');
    $email = strtolower(trim($email));
    $q = $db->prepare('SELECT id FROM accounts WHERE email = ? AND email_verified = 0'); $q->execute([$email]);
    $id = $q->fetchColumn();
    if (!$id || !ofp_account_rate($db, 'activation:' . $id, 1, 60)) return;
    $token = bin2hex(random_bytes(32));
    $db->beginTransaction();
    try {
        $db->prepare('DELETE FROM account_verifications WHERE account_id = ?')->execute([$id]);
        $db->prepare('INSERT INTO account_verifications(token_hash, account_id, expires) VALUES(?, ?, ?)')
           ->execute([hash('sha256', $token), $id, time() + 86400]);
        $db->commit();
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
    $url = rtrim(ofp_settings()['site']['base_url'] ?? 'https://openfp.retrocs.org', '/') . '/account/?verify=' . $token;
    ofp_smtp_send($email, 'Activate your OpenFP account', "Confirm your email address to activate your OpenFP account. Open this link within 24 hours, then choose Activate account:\n\n$url\n\nIf you did not create this account, ignore this email.");
}
function ofp_account_activate(PDO $db, string $token): bool {
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $token)) return false;
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT account_id FROM account_verifications WHERE token_hash = ? AND expires > ?' .
            ($db->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([hash('sha256', $token), time()]); $id = $q->fetchColumn();
        if (!$id) { $db->rollBack(); return false; }
        $q = $db->prepare('UPDATE accounts SET email_verified = 1 WHERE id = ? AND email_verified = 0'); $q->execute([$id]);
        $changed = $q->rowCount() === 1;
        $db->prepare('DELETE FROM account_verifications WHERE account_id = ?')->execute([$id]);
        $db->commit(); return $changed;
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}
function ofp_account_reset_request(PDO $db, string $email): void {
    require_once __DIR__ . '/smtp.php';
    if (!ofp_smtp_ready()) return;
    $q = $db->prepare('SELECT id FROM accounts WHERE email = ?'); $q->execute([strtolower(trim($email))]); $id = $q->fetchColumn();
    if (!$id) return;
    $token = bin2hex(random_bytes(32));
    $db->prepare('DELETE FROM account_resets WHERE account_id = ?')->execute([$id]);
    $db->prepare('INSERT INTO account_resets(token_hash, account_id, expires) VALUES(?, ?, ?)')->execute([hash('sha256', $token), $id, time() + 1800]);
    $url = rtrim(ofp_settings()['site']['base_url'] ?? 'https://openfp.retrocs.org', '/') . '/account/?reset=' . $token;
    ofp_smtp_send($email, 'Reset your OpenFP password', "Use this link within 30 minutes to reset your OpenFP password:\n\n$url\n\nIf you did not request this, ignore this email.");
}
function ofp_account_reset(PDO $db, string $token, string $password): bool {
    $hash = ofp_account_password($password);
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $token)) return false;
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT account_id FROM account_resets WHERE token_hash = ? AND expires > ?' . ($db->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([hash('sha256', $token), time()]); $id = $q->fetchColumn();
        if (!$id) { $db->rollBack(); return false; }
        foreach (['account_sessions', 'account_tickets', 'account_resets', 'account_devices', 'account_verifications'] as $table)
            $db->prepare("DELETE FROM $table WHERE account_id = ?")->execute([$id]);
        $db->prepare('UPDATE accounts SET password_hash = ?, password_changed = ? WHERE id = ?')->execute([$hash, time(), $id]);
        $db->commit(); return true;
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}
function ofp_device_start(PDO $db): array {
    $secret = bin2hex(random_bytes(32));
    $alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; $pin = '';
    for ($i = 0; $i < 8; $i++) $pin .= $alphabet[random_int(0, strlen($alphabet) - 1)];
    $db->prepare('INSERT INTO account_devices(token_hash, pin, expires, account_id) VALUES(?, ?, ?, NULL)')->execute([hash('sha256', $secret), $pin, time() + 600]);
    return ['device' => $secret, 'pin' => $pin];
}
function ofp_device_approve(PDO $db, string $pin, string $account): bool {
    if (!ofp_account_id_active($db, $account)) return false;
    $pin = strtoupper(str_replace(['-', ' '], '', trim($pin)));
    $q = $db->prepare('UPDATE account_devices SET account_id = ? WHERE pin = ? AND expires > ? AND account_id IS NULL');
    $q->execute([$account, $pin, time()]); return $q->rowCount() === 1;
}
function ofp_device_poll(PDO $db, string $secret): ?array {
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $secret)) return null;
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT a.* FROM account_devices d JOIN accounts a ON a.id = d.account_id WHERE d.token_hash = ? AND d.expires > ?');
        $q->execute([hash('sha256', $secret), time()]); $a = $q->fetch(PDO::FETCH_ASSOC);
        if ($a && ofp_account_active($a)) {
            $q = $db->prepare('DELETE FROM account_devices WHERE token_hash = ?'); $q->execute([hash('sha256', $secret)]);
            if ($q->rowCount() !== 1) $a = false;
        }
        $db->commit(); return $a && ofp_account_active($a) ? $a : null;
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}
function ofp_account_prune(PDO $db): void {
    foreach (['account_sessions', 'account_tickets', 'account_resets', 'account_devices', 'account_verifications'] as $t)
        $db->prepare("DELETE FROM $t WHERE expires <= ?")->execute([time()]);
    $db->prepare('DELETE FROM account_limits WHERE started < ?')->execute([time() - 86400]);
}
