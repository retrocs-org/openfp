<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/accounts.php';
header('Cache-Control: no-store'); header('Content-Type: text/plain; charset=utf-8'); header('X-Content-Type-Options: nosniff');
function account_reply(int $status, array $values): never {
    http_response_code($status);
    foreach ($values as $k=>$v) echo $k,'=',str_replace(["\r","\n"],' ',(string)$v),"\n";
    exit;
}
if (!ofp_account_https()) account_reply(426,['error'=>'Cloud accounts require HTTPS.']);
if (($_SERVER['REQUEST_METHOD']??'')!=='POST') account_reply(405,['error'=>'POST required.']);
if ((int)($_SERVER['CONTENT_LENGTH']??0)>4096) account_reply(413,['error'=>'Request too large.']);
if (!ofp_account_config()['enabled']) account_reply(503,['error'=>'Cloud accounts are disabled.']);
try {
    $db=ofp_db(); $action=is_string($_POST['action']??null)?$_POST['action']:''; $ip=ofp_client_ip();
    $get=fn($k)=>is_string($_POST[$k]??null)?$_POST[$k]:'';
    $client=ofp_account_client($get('client'));
    if (!in_array($action,['register','login','device_start','device_poll','ticket','logout','verify','profile','profile_save'],true)) account_reply(400,['error'=>'Unknown action.']);
    if ($action==='verify') {
        if (!ofp_cloud_server($get('server'),$get('secret'))) account_reply(403,['error'=>'Unknown cloud server.']);
        $a=ofp_cloud_verify($db,$get('ticket'),$get('server'),$get('server_id'),$get('nonce'),$get('key_id'));
        if (!$a) account_reply(403,['error'=>'Cloud ticket expired or invalid.']);
        account_reply(200,['id'=>$a['id'],'username'=>$a['username']]);
    }
    $limit=in_array($action,['login','register'],true)?12:120;
    if (!ofp_account_rate($db,"api:$action:$ip",$limit,600)) account_reply(429,['error'=>'Too many attempts. Try again later.']);
    ofp_account_prune($db);
    if ($action==='login' || $action==='register') {
        if (!ofp_account_rate($db,'username:'.strtolower(substr($get('username'),0,15)),40,600)) account_reply(429,['error'=>'Too many attempts. Try again later.']);
        if ($action==='register') {
            ofp_account_register($db,$get('username'),$get('email'),$get('password'));
            if (ofp_account_config()['require_email_verification'])
                account_reply(202,['status'=>'verification_required','message'=>'Check your email to activate your account, then sign in.']);
        }
        $a=ofp_account_login($db,$get('username'),$get('password'));
        if (!$a) account_reply(401,['error'=>'Username or password is incorrect.']);
    } elseif ($action==='device_start') {
        account_reply(200,ofp_device_start($db)+['url'=>'https://openfp.retrocs.org/account/pin']);
    } elseif ($action==='device_poll') {
        $a=ofp_device_poll($db,$get('device'));
        if (!$a) account_reply(202,['status'=>'pending']);
    } else {
        $a=ofp_account_from_token($db,$get('session'));
        if (!$a) account_reply(401,['error'=>'Sign in to your cloud profile again.']);
        if ($action==='profile') account_reply(200, ['username'=>$a['username'],'key'=>$a['player_key'],'session'=>$get('session')] + ofp_account_preferences($db, $a['id'], $client));
        if ($action==='profile_save') {
            $values=['favorite_tank'=>$get('favorite_tank'), 'avatar'=>$get('avatar')];
            if (array_key_exists('steering', $_POST)) $values['steering']=$get('steering');
            ofp_account_save_preferences($db, $a['id'], $values, $client);
            account_reply(200, ['status'=>'saved']);
        }
        if ($action==='logout') {
            $db->prepare('DELETE FROM account_sessions WHERE token_hash = ?')->execute([hash('sha256',$get('session'))]);
            account_reply(200,['status'=>'signed out']);
        }
        if ($action==='ticket') account_reply(200,['ticket'=>ofp_cloud_ticket($db,$a,$get('server'),$get('server_id'),$get('nonce'),$get('key_id'))]);
        account_reply(400,['error'=>'Unknown action.']);
    }
    account_reply(200,['id'=>$a['id'],'username'=>$a['username'],'key'=>$a['player_key'],'session'=>ofp_account_session($db,$a['id'])] + ofp_account_preferences($db, $a['id'], $client));
} catch (InvalidArgumentException $e) { account_reply(400,['error'=>$e->getMessage()]); }
catch (Throwable $e) { error_log('OpenFP account API failed: '.$e->getMessage()); account_reply(503,['error'=>'Account service unavailable. Try again later.']); }
