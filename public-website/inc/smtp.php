<?php
// SPDX-License-Identifier: MIT
require_once __DIR__ . '/listing.php';
function ofp_smtp_config(): array {
    return array_merge(['host'=>'', 'port'=>587, 'encryption'=>'starttls', 'username'=>'', 'password'=>'',
        'from'=>'', 'from_name'=>'OpenFP', 'ehlo'=>'localhost', 'bind'=>'', 'timeout'=>10], ofp_settings()['smtp'] ?? []);
}
function ofp_smtp_ready(): bool {
    $c = ofp_smtp_config();
    return $c['host'] !== '' && filter_var($c['from'], FILTER_VALIDATE_EMAIL) &&
        in_array($c['encryption'], ['none','starttls','tls'], true) && (int)$c['port'] > 0 && (int)$c['port'] <= 65535;
}
function ofp_smtp_reply($socket, array $expected): string {
    $reply = ''; $n = 0;
    do {
        $line = fgets($socket, 1024);
        if ($line === false || ++$n > 100 || !preg_match('/\A[0-9]{3}[ -]/', $line)) throw new RuntimeException('SMTP reply failed.');
        $reply .= $line;
    } while ($line[3] === '-');
    if (!in_array((int)substr($line, 0, 3), $expected, true)) throw new RuntimeException('SMTP rejected the message (code ' . substr($line, 0, 3) . ').');
    return $reply;
}
function ofp_smtp_write($socket, string $text): void {
    for ($done=0, $len=strlen($text); $done<$len; $done += $n) {
        $n = fwrite($socket, substr($text, $done));
        if (!$n) throw new RuntimeException('SMTP write failed.');
    }
}
function ofp_smtp_send(string $to, string $subject, string $body): void {
    $c=ofp_smtp_config();
    if (!ofp_smtp_ready()) throw new RuntimeException('Password recovery is disabled: configure SMTP.');
    foreach ([$to,$subject,$c['from'],$c['from_name'],$c['ehlo'],$c['host']] as $v)
        if (preg_match('/[\r\n\x00]/', $v)) throw new InvalidArgumentException('Invalid mail header.');
    if (!filter_var($to,FILTER_VALIDATE_EMAIL) || !preg_match('/\A[A-Za-z0-9.:-]+\z/D',$c['host']) ||
        !preg_match('/\A[A-Za-z0-9.\[\]:-]+\z/D',$c['ehlo'])) throw new InvalidArgumentException('Invalid SMTP address.');
    $options=['ssl'=>['verify_peer'=>true,'verify_peer_name'=>true,'peer_name'=>$c['host']]];
    if ($c['bind'] !== '') $options['socket']=['bindto'=>$c['bind']];
    $context=stream_context_create($options);
    $host=str_contains($c['host'],':') ? '['.$c['host'].']' : $c['host'];
    $socket=@stream_socket_client(($c['encryption']==='tls'?'tls':'tcp')."://$host:".(int)$c['port'],$errno,$errstr,(int)$c['timeout'],STREAM_CLIENT_CONNECT,$context);
    if (!$socket) throw new RuntimeException('Could not connect to SMTP.');
    stream_set_timeout($socket,max(1,min(30,(int)$c['timeout'])));
    try {
        ofp_smtp_reply($socket,[220]); ofp_smtp_write($socket,"EHLO {$c['ehlo']}\r\n"); $cap=ofp_smtp_reply($socket,[250]);
        if ($c['encryption']==='starttls') {
            if (!preg_match('/^250[ -]STARTTLS\b/im',$cap)) throw new RuntimeException('SMTP does not offer STARTTLS.');
            ofp_smtp_write($socket,"STARTTLS\r\n"); ofp_smtp_reply($socket,[220]);
            if (stream_socket_enable_crypto($socket,true,STREAM_CRYPTO_METHOD_TLS_CLIENT)!==true) throw new RuntimeException('SMTP TLS failed.');
            ofp_smtp_write($socket,"EHLO {$c['ehlo']}\r\n"); ofp_smtp_reply($socket,[250]);
        }
        if ($c['username'] !== '') {
            ofp_smtp_write($socket,"AUTH LOGIN\r\n"); ofp_smtp_reply($socket,[334]);
            ofp_smtp_write($socket,base64_encode($c['username'])."\r\n"); ofp_smtp_reply($socket,[334]);
            ofp_smtp_write($socket,base64_encode($c['password'])."\r\n"); ofp_smtp_reply($socket,[235]);
        }
        ofp_smtp_write($socket,"MAIL FROM:<{$c['from']}>\r\n"); ofp_smtp_reply($socket,[250]);
        ofp_smtp_write($socket,"RCPT TO:<$to>\r\n"); ofp_smtp_reply($socket,[250,251]);
        ofp_smtp_write($socket,"DATA\r\n"); ofp_smtp_reply($socket,[354]);
        $body=str_replace(["\r\n","\r"],"\n",$body); $body=preg_replace('/^\./m','..',$body);
        $headers='From: =?UTF-8?B?'.base64_encode($c['from_name']).'?= <'.$c['from'].">\r\nTo: <$to>\r\nSubject: =?UTF-8?B?".base64_encode($subject)."?=\r\nDate: ".gmdate('r')."\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n";
        ofp_smtp_write($socket,$headers.str_replace("\n","\r\n",$body)."\r\n.\r\n"); ofp_smtp_reply($socket,[250]); ofp_smtp_write($socket,"QUIT\r\n");
    } finally { fclose($socket); }
}
