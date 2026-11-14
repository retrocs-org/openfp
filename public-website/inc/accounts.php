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
function ofp_account_activate(PDO $db, string $token): ?array {
    if (!preg_match('/\A[0-9a-f]{64}\z/D', $token)) return null;
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT a.id, a.username, a.password_hash FROM accounts a JOIN account_verifications v ON v.account_id = a.id WHERE v.token_hash = ? AND v.expires > ?' .
            ($db->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([hash('sha256', $token), time()]); $account = $q->fetch(PDO::FETCH_ASSOC);
        if (!$account) { $db->rollBack(); return null; }
        $id = $account['id'];
        $q = $db->prepare('UPDATE accounts SET email_verified = 1 WHERE id = ? AND email_verified = 0'); $q->execute([$id]);
        $changed = $q->rowCount() === 1;
        $db->prepare('DELETE FROM account_verifications WHERE account_id = ?')->execute([$id]);
        $db->commit(); return $changed ? $account : null;
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

function ofp_account_client(string $client): string {
    if ($client !== '' && !in_array($client, ['macos','windows','linux','amigaos','tvos'], true))
        throw new InvalidArgumentException('Unknown client platform.');
    return $client;
}
function ofp_account_preferences(PDO $db, string $id, string $client = ''): array {
    ofp_account_client($client);
    $q = $db->prepare('SELECT favorite_tank, avatar, steering FROM account_preferences WHERE account_id = ?');
    $q->execute([$id]); $p = $q->fetch(PDO::FETCH_ASSOC);
    if (!$p) return ['preferences'=>0];
    $result = ['preferences'=>1, 'favorite_tank'=>(int)$p['favorite_tank'], 'avatar'=>(int)$p['avatar'], 'steering'=>(int)$p['steering']];
    if ($client !== '') {
        $q = $db->prepare('SELECT steering FROM account_client_preferences WHERE account_id = ? AND client = ?');
        $q->execute([$id, $client]); $steering = $q->fetchColumn();
        $result['steering'] = $steering === false ? 0 : (int)$steering;
        $result['steering_saved'] = $steering === false ? 0 : 1;
    }
    return $result;
}
function ofp_account_save_preferences(PDO $db, string $id, array $values, string $client = ''): void {
    ofp_account_client($client); $p = [];
    foreach (['favorite_tank'=>3, 'avatar'=>9, 'steering'=>1] as $key=>$max) {
        if ($key === 'steering' && !array_key_exists($key, $values)) continue;
        $v = $values[$key] ?? null;
        if ((!is_int($v) && !is_string($v)) || !preg_match('/\A[0-9]\z/D', (string)$v) || (int)$v > $max)
            throw new InvalidArgumentException('Choose a valid avatar, favorite tank and steering mode.');
        $p[$key] = (int)$v;
    }
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT id FROM accounts WHERE id = ?' . ($db->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([$id]);
        if (!$q->fetchColumn() || !ofp_account_id_active($db, $id)) throw new InvalidArgumentException('Sign in again to save your profile.');
        $previous = ofp_account_preferences($db, $id);
        // Browser edits and platform-aware clients must not overwrite legacy steering.
        $legacySteering = $client === '' && isset($p['steering']) ? $p['steering'] : ($previous['steering'] ?? 0);
        $db->prepare('DELETE FROM account_preferences WHERE account_id = ?')->execute([$id]);
        $db->prepare('INSERT INTO account_preferences(account_id, favorite_tank, avatar, steering) VALUES(?, ?, ?, ?)')
            ->execute([$id, $p['favorite_tank'], $p['avatar'], $legacySteering]);
        if ($client !== '' && isset($p['steering'])) {
            $db->prepare('DELETE FROM account_client_preferences WHERE account_id = ? AND client = ?')->execute([$id, $client]);
            $db->prepare('INSERT INTO account_client_preferences(account_id, client, steering) VALUES(?, ?, ?)')->execute([$id, $client, $p['steering']]);
        }
        $db->commit();
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}

// Only presentation metadata changes; replay inputs, terrain and checksums stay intact.
function ofp_map_forget_author(string $text, string $name, string $email): string {
    if (preg_match('/^author=([^\r\n]*)/m', $text, $m) && !strcasecmp(trim($m[1]), $name) ||
        $email !== '' && preg_match('/^contact=([^\r\n]*)/m', $text, $m) && stripos($m[1], $email) !== false)
        return preg_replace('/^(author|contact)=[^\r\n]*/m', '$1=', $text);
    return $text;
}
function ofp_replay_map(string $replay, callable $transform): string {
    if (!preg_match('/^map=(\d+)\r?\n/m', $replay, $m, PREG_OFFSET_CAPTURE)) return $replay;
    $start=$m[0][1]; $offset=$start+strlen($m[0][0]); $length=(int)$m[1][0];
    if ($length<1 || $length>=1000000 || $offset+$length>strlen($replay)) return $replay;
    $map=$transform(substr($replay,$offset,$length));
    return substr($replay,0,$start).'map='.strlen($map)."\n".$map.substr($replay,$offset+$length);
}
function ofp_replay_forget_name(string $replay, string $name): string {
    return preg_replace_callback('/^(player=[^\r\n]*\bname=)([^\s]+)(\r?)$/m',
        fn($m) => $m[1] . (!strcasecmp(str_replace('_', ' ', $m[2]), str_replace('_', ' ', $name)) ? 'DELETED_PLAYER' : $m[2]) . $m[3], $replay);
}
function ofp_account_delete(PDO $db, string $id, string $password): void {
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT * FROM accounts WHERE id = ?' . ($db->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite' ? '' : ' FOR UPDATE'));
        $q->execute([$id]); $a = $q->fetch(PDO::FETCH_ASSOC);
        if (!$a || strlen($password)>72 || str_contains($password,"\0") || !password_verify($password, $a['password_hash']))
            throw new InvalidArgumentException('Enter your current password to delete your account.');
        $name = $a['username']; $ladderTeams=[];
        foreach (ofp_ladders() as $ladder) foreach ($ladder['teams'] ?? [] as $team=>$members)
            foreach ($members as $member) if (!strcasecmp($member, $name)) $ladderTeams[$ladder['slug']]=$team;
        foreach ($db->query('SELECT id, players FROM matches')->fetchAll(PDO::FETCH_ASSOC) as $match) {
            $players = json_decode($match['players'] ?: '[]', true) ?: []; $changed = false;
            foreach ($players as &$p) {
                if (($p['account_id'] ?? '') !== $id && (isset($p['account_id']) || strcasecmp($p['name'] ?? '', $name))) continue;
                // Keep ladder membership as team labels, with no former username.
                $p['ladder_teams']=$ladderTeams;
                $p['name'] = 'Deleted player'; $p['deleted'] = 1; $p['account_id'] = $id; $changed = true;
            }
            unset($p);
            if ($changed) $db->prepare('UPDATE matches SET players = ? WHERE id = ?')->execute([json_encode($players), $match['id']]);
        }
        // Scrub every hosted copy, including daily challenges and live snapshots.
        foreach (['replays'=>'match_id', 'live'=>null, 'daily'=>'id', 'daily_queue'=>'id'] as $table=>$key) {
            foreach ($db->query("SELECT * FROM $table")->fetchAll(PDO::FETCH_ASSOC) as $row) {
                $text = is_resource($row['replay']) ? stream_get_contents($row['replay']) : (string)$row['replay'];
                $text = ofp_replay_map(ofp_replay_forget_name($text, $name), fn($map)=>ofp_map_forget_author($map, $name, $a['email']));
                if ($table === 'daily' || $table === 'daily_queue') {
                    if (!strcasecmp($row['name'], $name)) {
                        // Daily entries are personal records; remove them rather than merge two deleted users.
                        $db->prepare("DELETE FROM $table WHERE id = ?")->execute([$row['id']]); continue;
                    }
                }
                if ($key) $db->prepare("UPDATE $table SET replay = ? WHERE $key = ?")->execute([$text, $row[$key]]);
                else $db->prepare('UPDATE live SET replay = ? WHERE host = ? AND port = ?')->execute([$text, $row['host'], $row['port']]);
            }
        }
        $db->prepare('DELETE FROM leaders WHERE LOWER(name) = ?')->execute([strtolower($name)]);
        foreach ($db->query('SELECT id, author, text FROM gallery')->fetchAll(PDO::FETCH_ASSOC) as $map) {
            $text=ofp_map_forget_author($map['text'], $name, $a['email']);
            if (strcasecmp($map['author'] ?? '', $name) && $text===$map['text']) continue;
            $text=preg_replace('/^(author|contact)=[^\r\n]*/m', '$1=', $text);
            $hash=sha1($text);
            $q=$db->prepare('SELECT id FROM gallery WHERE hash = ? AND id <> ?');$q->execute([$hash,$map['id']]);
            if ($q->fetchColumn()) $db->prepare('DELETE FROM gallery WHERE id = ?')->execute([$map['id']]);
            else $db->prepare("UPDATE gallery SET author = '', text = ?, hash = ?, size = ?, ip = '' WHERE id = ?")->execute([$text,$hash,strlen($text),$map['id']]);
        }
        foreach ($db->query('SELECT host, port, people FROM servers')->fetchAll(PDO::FETCH_ASSOC) as $server) {
            $people = array_filter(explode(',', $server['people'] ?? ''), fn($n)=>strcasecmp(trim($n), $name));
            $db->prepare('UPDATE servers SET people = ? WHERE host = ? AND port = ?')->execute([implode(',', $people), $server['host'], $server['port']]);
        }
        foreach (['account_client_preferences','account_preferences','account_sessions','account_tickets','account_resets','account_devices','account_verifications'] as $table)
            $db->prepare("DELETE FROM $table WHERE account_id = ?")->execute([$id]);
        $db->prepare('DELETE FROM account_limits WHERE bucket = ?')->execute([hash('sha256', 'username:'.strtolower($name))]);
        $db->prepare('INSERT INTO deleted_accounts(id, ladder_teams) VALUES(?, ?)')->execute([$id,json_encode($ladderTeams)]);
        $db->prepare('DELETE FROM accounts WHERE id = ?')->execute([$id]);
        $db->commit();
    } catch (Throwable $e) { $db->rollBack(); throw $e; }
}

// Public replay streams may arrive after an account was removed while a match ran.
function ofp_public_replay_names(PDO $db, string $replay): string {
    $replay=ofp_replay_map($replay, fn($map)=>preg_replace('/^(author|contact)=[^\r\n]*/m', '$1=', $map));
    return preg_replace_callback('/^(player=[^\r\n]*\bname=)([^\s]+)(\r?)$/m', function($m) use($db) {
        if (preg_match('/\bbot=[1-9][0-9]*\b/', $m[1])) return $m[0];
        $q=$db->prepare('SELECT username FROM accounts WHERE username_key = ?' . ($db->inTransaction() ? ' FOR UPDATE' : ''));
        $q->execute([strtolower($m[2])]); $name=$q->fetchColumn();
        return $m[1] . ($name ?: 'DELETED_PLAYER') . $m[3];
    }, $replay);
}
function ofp_public_people(PDO $db, string $names): string {
    $out=[];
    foreach(explode(',', $names) as $name) {
        $q=$db->prepare('SELECT username FROM accounts WHERE username_key = ?' . ($db->inTransaction() ? ' FOR UPDATE' : ''));$q->execute([strtolower(trim($name))]);
        if($name=$q->fetchColumn()) $out[]=$name;
    }
    return implode(',', $out);
}
