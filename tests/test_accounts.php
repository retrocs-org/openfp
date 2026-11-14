<?php
// SPDX-License-Identifier: MIT
// Uses only an explicitly named disposable database, never the site's configuration.
$dsn=getenv('OFP_ACCOUNT_TEST_DSN')?:'';
if (!preg_match('/\Amysql:.*dbname=openfp_account_test(?:;|\z)/D',$dsn)) {
    fwrite(STDERR,"Set OFP_ACCOUNT_TEST_DSN to a disposable MariaDB database named openfp_account_test.\n");exit(77);
}
$fixture=sys_get_temp_dir().'/ofp-accounts-'.bin2hex(random_bytes(8));mkdir($fixture,0700);mkdir($fixture.'/inc',0700);
$secret=bin2hex(random_bytes(32));
$config=['db'=>['dsn'=>$dsn,'user'=>getenv('OFP_ACCOUNT_TEST_USER')?:'root','password'=>getenv('OFP_ACCOUNT_TEST_PASSWORD')?:'','create_tables'=>true],
 'accounts'=>['require_email_verification'=>false,'servers'=>['test'=>hash('sha256',$secret)]], 'smtp'=>[], 'listing'=>['trusted_proxies'=>['127.0.0.1/32']]];
file_put_contents($fixture.'/config.inc.php',"<?php return ".var_export($config,true).';');
foreach (['accounts','smtp','listing','migrations','map-catalog','reset-plan'] as $f) copy(__DIR__.'/../public-website/inc/'.$f.'.php',$fixture.'/inc/'.$f.'.php');
require $fixture.'/inc/accounts.php';require $fixture.'/inc/smtp.php';require $fixture.'/inc/reset-plan.php';
function account_check($v,string $m): void {if(!$v)throw new RuntimeException($m);}
function account_reject(callable $f,string $m): void {try{$f();}catch(InvalidArgumentException $e){return;}throw new RuntimeException($m);}
try {
 $db=ofp_db();account_check(ofp_schema_version($db)===15,'migration reaches 15');account_check(ofp_migrate($db)===[],'migration is idempotent');
 // Exercise upgrading an existing version 13 schema, as well as fresh installation.
 foreach (array_filter(ofp_reset_tables(),fn($t)=>str_starts_with($t,'account_')||$t==='accounts') as $t) $db->exec("DROP TABLE $t");
 $db->exec('ALTER TABLE servers DROP COLUMN cloud_server');$db->exec("UPDATE ofp_meta SET v='13' WHERE k='schema_version'");
 account_check(count(ofp_migrate($db))===2&&ofp_schema_version($db)===15,'version 13 upgrades to 15');
 // A pre-activation account remains active when version 14 is upgraded.
 $legacy=bin2hex(random_bytes(16));
 $db->exec('DROP TABLE account_verifications');$db->exec('ALTER TABLE accounts DROP COLUMN email_verified');
 $db->exec("UPDATE ofp_meta SET v='14' WHERE k='schema_version'");
 $db->prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?,?)')->execute([$legacy,'Legacy','legacy','legacy@example.invalid',password_hash('legacy-fixture-password',PASSWORD_DEFAULT),bin2hex(random_bytes(32)),time(),time()]);
 account_check(count(ofp_migrate($db))===1&&ofp_schema_version($db)===15,'version 14 upgrades to 15');
 account_check((int)$db->query("SELECT email_verified FROM accounts WHERE username='Legacy'")->fetchColumn()===1,'existing accounts stay active');
 $id=ofp_account_register($db,'Pilot','pilot@example.invalid','fixture-password-123');
 account_reject(fn()=>ofp_account_register($db,'pilot','other@example.invalid','fixture-password-123'),'case-insensitive duplicate rejected');
 account_reject(fn()=>ofp_account_register($db,'Other','PILOT@example.invalid','fixture-password-123'),'duplicate email rejected');
 account_reject(fn()=>ofp_account_register($db,'Bad name','bad@example.invalid','fixture-password-123'),'bad username rejected');
 account_reject(fn()=>ofp_account_register($db,'Short','short@example.invalid','short'),'short password rejected');
 account_check(!ofp_account_login($db,'Pilot','wrong')&&!ofp_account_login($db,'Nobody','fixture-password-123'),'bad credentials rejected');
 $a=ofp_account_login($db,'PILOT','fixture-password-123');account_check($a['id']===$id&&$a['password_hash']!=='fixture-password-123','password hash and case-insensitive login');
 $token=ofp_account_session($db,$id);account_check(ofp_account_from_token($db,$token)['id']===$id,'session authenticates');
 account_check($db->query('SELECT token_hash FROM account_sessions')->fetchColumn()===hash('sha256',$token),'only session hash stored');
 $sid=str_repeat('02',16);$nonce='123456789abcdef0';$verifier=hash('sha256',hex2bin($a['player_key']).hex2bin($sid),true);$kid=substr(hash('sha256',$verifier),0,32);
 account_check(ofp_cloud_server('test',$secret)&&!ofp_cloud_server('test',str_repeat('0',64)),'registered server credential checked');
 account_reject(fn()=>ofp_cloud_ticket($db,$a,'test',$sid,$nonce,str_repeat('0',32)),'wrong profile key rejected');
 $ticket=ofp_cloud_ticket($db,$a,'test',$sid,$nonce,$kid);
 account_check(!ofp_cloud_verify($db,$ticket,'other',$sid,$nonce,$kid),'wrong server cannot redeem');
 account_check(!ofp_cloud_verify($db,$ticket,'test',$sid,'0000000000000000',$kid),'wrong nonce cannot redeem');
 account_check(ofp_cloud_verify($db,$ticket,'test',$sid,$nonce,$kid)['id']===$id,'matching ticket accepted');
 account_check(!ofp_cloud_verify($db,$ticket,'test',$sid,$nonce,$kid),'ticket single use');
 $ticket=ofp_cloud_ticket($db,$a,'test',$sid,$nonce,$kid);$db->exec('UPDATE account_tickets SET expires=1');
 account_check(!ofp_cloud_verify($db,$ticket,'test',$sid,$nonce,$kid),'expired ticket rejected');
 $d=ofp_device_start($db);account_check(!ofp_device_poll($db,$d['device']),'unapproved PIN stays pending');
 account_check(ofp_device_approve($db,substr($d['pin'],0,4).'-'.substr($d['pin'],4),$id),'PIN approves');
 account_check(!ofp_device_approve($db,$d['pin'],$id),'PIN cannot be approved twice');
 account_check(ofp_device_poll($db,$d['device'])['id']===$id&&!ofp_device_poll($db,$d['device']),'device secret single use');
 $d=ofp_device_start($db);$db->exec('UPDATE account_devices SET expires=1');account_check(!ofp_device_approve($db,$d['pin'],$id),'expired PIN rejected');
 account_check(!ofp_smtp_ready(),'SMTP absent disables recovery');ofp_account_reset_request($db,'pilot@example.invalid');
 account_check((int)$db->query('SELECT COUNT(*) FROM account_resets')->fetchColumn()===0,'disabled SMTP issues no reset');
 $reset=bin2hex(random_bytes(32));$db->prepare('INSERT INTO account_resets VALUES(?,?,?)')->execute([hash('sha256',$reset),$id,time()+60]);
 $d=ofp_device_start($db);ofp_device_approve($db,$d['pin'],$id);
 account_check(ofp_account_reset($db,$reset,'new-fixture-password-123'),'reset accepted');
 account_check(!ofp_account_from_token($db,$token)&&!ofp_device_poll($db,$d['device']),'reset revokes sessions and pending devices');
 account_check(!ofp_account_reset($db,$reset,'other-fixture-password'),'reset token single use');
 account_check(ofp_account_login($db,'Pilot','new-fixture-password-123')&&!ofp_account_login($db,'Pilot','fixture-password-123'),'only new password works');
 account_check(ofp_account_rate($db,'fixture',2,60)&&ofp_account_rate($db,'fixture',2,60)&&!ofp_account_rate($db,'fixture',2,60),'rate limit enforced');
 $_SERVER=['HTTPS'=>'off','REMOTE_ADDR'=>'203.0.113.2','HTTP_X_FORWARDED_PROTO'=>'https'];account_check(!ofp_account_https(),'untrusted TLS header ignored');
 $_SERVER['REMOTE_ADDR']='127.0.0.1';account_check(ofp_account_https(),'trusted proxy accepted');
 ofp_reset_data($db);foreach(ofp_reset_tables() as $t)account_check((int)$db->query("SELECT COUNT(*) FROM $t")->fetchColumn()===0,'reset empties '.$t);
 account_check(ofp_schema_version($db)===15,'reset preserves schema');
 // Configuration is read once per PHP request: use fresh processes for policy variants.
 $policy=<<<'PHP'
require $argv[1].'/inc/accounts.php';
$expected=(int)$argv[2];
if(ofp_account_config()['password_min_length']!==$expected)exit(1);
try{ofp_account_password('test');$accepted=true;}catch(InvalidArgumentException $e){$accepted=false;}
if($accepted!==($expected===4))exit(2);
try{ofp_account_password(str_repeat('é',37));exit(3);}catch(InvalidArgumentException $e){}
if(ofp_account_config()['require_email_verification']){
 $db=ofp_db();$id=bin2hex(random_bytes(16));$key=bin2hex(random_bytes(32));
 $db->prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?,?,?)')->execute([$id,'Pending','pending','pending@example.invalid',password_hash('pending-password-123',PASSWORD_DEFAULT),$key,time(),time(),0]);
 try{ofp_account_login($db,'Pending','pending-password-123');exit(4);}catch(InvalidArgumentException $e){}
 try{ofp_account_session($db,$id);exit(5);}catch(InvalidArgumentException $e){}
 $session=bin2hex(random_bytes(32));$db->prepare('INSERT INTO account_sessions VALUES(?,?,?)')->execute([hash('sha256',$session),$id,time()+60]);
 if(ofp_account_from_token($db,$session))exit(6);
 $d=ofp_device_start($db);if(ofp_device_approve($db,$d['pin'],$id))exit(7);
 $db->prepare('UPDATE account_devices SET account_id=? WHERE pin=?')->execute([$id,$d['pin']]);
 if(ofp_device_poll($db,$d['device']))exit(8);
 $sid=str_repeat('02',16);$nonce='123456789abcdef0';$kid=substr(hash('sha256',hash('sha256',hex2bin($key).hex2bin($sid),true)),0,32);
 try{ofp_cloud_ticket($db,['id'=>$id],'test',$sid,$nonce,$kid);exit(9);}catch(InvalidArgumentException $e){}
 $ticket=bin2hex(random_bytes(32));$db->prepare('INSERT INTO account_tickets VALUES(?,?,?,?,?,?,?)')->execute([hash('sha256',$ticket),$id,'test',$sid,$nonce,$kid,time()+60]);
 if(ofp_cloud_verify($db,$ticket,'test',$sid,$nonce,$kid))exit(10);
 $token=bin2hex(random_bytes(32));$db->prepare('INSERT INTO account_verifications VALUES(?,?,?)')->execute([hash('sha256',$token),$id,time()-1]);
 if(ofp_account_activate($db,$token))exit(11);
 $db->exec('UPDATE account_verifications SET expires='.(time()+60));
 if(!ofp_account_activate($db,$token)||ofp_account_activate($db,$token)||!ofp_account_login($db,'Pending','pending-password-123'))exit(12);
 try{ofp_account_register($db,'NoMail','nomail@example.invalid','pending-password-123');exit(13);}catch(InvalidArgumentException $e){}
 if((int)$db->query("SELECT COUNT(*) FROM accounts WHERE username='NoMail'")->fetchColumn())exit(14);
}
PHP;
 foreach ([[4,false,4],[1,false,4],[100,false,72],[12,true,12]] as [$min,$verification,$expected]) {
     $config['accounts']['password_min_length']=$min;$config['accounts']['require_email_verification']=$verification;
     file_put_contents($fixture.'/config.inc.php',"<?php return ".var_export($config,true).';');
     $command=escapeshellarg(PHP_BINARY).' -r '.escapeshellarg($policy).' '.escapeshellarg($fixture).' '.$expected;
     passthru($command,$status);account_check($status===0,'password and activation policy variant '.$min.' returned '.$status);
 }
 ofp_reset_data($db);
 echo "accounts: migrations 13/14→15, password policy, pending-account restrictions, activation, sessions, tickets, PIN pairing, recovery and full reset passed\n";
} finally {
 foreach(glob($fixture.'/inc/*') as $f)unlink($f);rmdir($fixture.'/inc');unlink($fixture.'/config.inc.php');rmdir($fixture);
}
