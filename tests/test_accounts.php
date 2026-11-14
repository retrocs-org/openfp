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
 'accounts'=>['require_email_verification'=>false,'servers'=>['test'=>hash('sha256',$secret)]], 'ladders'=>[['slug'=>'fixture','name'=>'Fixture','servers'=>['game.example.invalid:29870'],'teams'=>['Iron Guard'=>['Pilot']]]], 'smtp'=>[], 'listing'=>['trusted_proxies'=>['127.0.0.1/32']]];
file_put_contents($fixture.'/config.inc.php',"<?php return ".var_export($config,true).';');
foreach (['accounts','smtp','listing','migrations','map-catalog','reset-plan'] as $f) copy(__DIR__.'/../public-website/inc/'.$f.'.php',$fixture.'/inc/'.$f.'.php');
require $fixture.'/inc/accounts.php';require $fixture.'/inc/smtp.php';require $fixture.'/inc/reset-plan.php';
function account_check($v,string $m): void {if(!$v)throw new RuntimeException($m);}
function account_reject(callable $f,string $m): void {try{$f();}catch(InvalidArgumentException $e){return;}throw new RuntimeException($m);}
try {
 $db=ofp_db();account_check(ofp_schema_version($db)===17,'migration reaches 17');account_check(ofp_migrate($db)===[],'migration is idempotent');
 // Exercise upgrading an existing version 13 schema, as well as fresh installation.
 foreach (array_filter(ofp_reset_tables(),fn($t)=>str_starts_with($t,'account_')||in_array($t,['accounts','deleted_accounts'],true)) as $t) $db->exec("DROP TABLE $t");
 $db->exec('ALTER TABLE servers DROP COLUMN cloud_server');$db->exec("UPDATE ofp_meta SET v='13' WHERE k='schema_version'");
 account_check(count(ofp_migrate($db))===4&&ofp_schema_version($db)===17,'version 13 upgrades to 17');
 // A pre-activation account remains active when version 14 is upgraded.
 $legacy=bin2hex(random_bytes(16));
 $db->exec('DROP TABLE account_verifications');$db->exec('ALTER TABLE accounts DROP COLUMN email_verified');
 $db->exec("UPDATE ofp_meta SET v='14' WHERE k='schema_version'");
 $db->prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?,?)')->execute([$legacy,'Legacy','legacy','legacy@example.invalid',password_hash('legacy-fixture-password',PASSWORD_DEFAULT),bin2hex(random_bytes(32)),time(),time()]);
 account_check(count(ofp_migrate($db))===3&&ofp_schema_version($db)===17,'version 14 upgrades to 17');
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
 // Cloud preferences persist independently of device key bindings.
 account_check(ofp_account_preferences($db,$id)===['preferences'=>0],'new account has no remote override');
 ofp_account_save_preferences($db,$id,['favorite_tank'=>'3','avatar'=>'9','steering'=>'1']);
 account_check(ofp_account_preferences($db,$id)===['preferences'=>1,'favorite_tank'=>3,'avatar'=>9,'steering'=>1],'preferences round trip');
 foreach (['favorite_tank'=>4,'avatar'=>10,'steering'=>2] as $field=>$bad)
     account_reject(fn()=>ofp_account_save_preferences($db,$id,array_replace(['favorite_tank'=>3,'avatar'=>9,'steering'=>1],[$field=>$bad])),'invalid '.$field.' rejected');
 // Each platform keeps its own steering; shared appearance remains shared.
 ofp_account_save_preferences($db,$id,['favorite_tank'=>2,'avatar'=>5,'steering'=>0],'macos');
 ofp_account_save_preferences($db,$id,['favorite_tank'=>2,'avatar'=>5,'steering'=>1],'amigaos');
 account_check(ofp_account_preferences($db,$id,'macos')['steering']===0,'Mac steering stays separate');
 account_check(ofp_account_preferences($db,$id,'amigaos')['steering']===1,'Amiga steering stays separate');
 account_check(ofp_account_preferences($db,$id,'windows')['steering_saved']===0,'new platform keeps its local default');
 ofp_account_save_preferences($db,$id,['favorite_tank'=>3,'avatar'=>9]);
 account_check(ofp_account_preferences($db,$id,'amigaos')['steering']===1 && ofp_account_preferences($db,$id,'macos')['steering']===0,'browser edits preserve all client steering');
 account_reject(fn()=>ofp_account_preferences($db,$id,'unknown'),'unknown platform rejected');
 $other=ofp_account_register($db,'Wingman','wing@example.invalid','wing-fixture-password');
 $players=[['name'=>'Pilot','account_id'=>$id,'side'=>'camp','won'=>1,'kills'=>4,'score'=>90,'change'=>12,'bot'=>0],
           ['name'=>'Wingman','account_id'=>$other,'side'=>'fortress','won'=>0,'kills'=>2,'score'=>40,'change'=>-12,'bot'=>0]];
 $map="OFPMAP 1\nauthor=Pilot\ncontact=pilot@example.invalid\nname=Fixture\ntiles\n0\n";
 $replay="OFPREPLAY 1\nseed=1 biome=0 time=0 garages=1 versus=1 ai=1 players=2 tanks=2\nplayer=0 cls=1 team=0 steer=0 bot=0 name=Pilot\nplayer=1 cls=1 team=1 steer=0 bot=0 name=Wingman\nmap=".strlen($map)."\n".$map."ticks\n30 0000\ncheck 30 12345678\nend\n";
 $mid=ofp_store_match(['host'=>'game.example.invalid','port'=>29870,'server_name'=>'Fixture','seconds'=>60,'mode'=>'versus','world'=>'classic','map'=>'Fixture','winner'=>'camp','players'=>$players],$replay);
 $db->prepare('INSERT INTO account_results VALUES(?,?,?,?,?)')->execute([$mid,$id,1,4,90]);
 $before=ofp_ladder_table(ofp_ladders()[0]);
 $db->prepare('INSERT INTO live VALUES(?,?,?,?)')->execute(['game.example.invalid',29870,$replay,time()]);
 $db->prepare('INSERT INTO daily(day,name,ticks,replay,at,ip) VALUES(?,?,?,?,?,?)')->execute([20261114,'Pilot',30,$replay,time(),'203.0.113.4']);
 $db->prepare('INSERT INTO daily_queue(day,name,ticks,replay,at,ip) VALUES(?,?,?,?,?,?)')->execute([20261114,'Wingman',30,$replay,time(),'203.0.113.5']);
 $db->prepare('INSERT INTO leaders(host,port,name,updated) VALUES(?,?,?,?)')->execute(['game.example.invalid',29870,'Pilot',time()]);
 $db->prepare('INSERT INTO gallery(name,author,hash,text,size,uploaded,ip) VALUES(?,?,?,?,?,?,?)')->execute(['Fixture','Pilot',sha1($map),$map,strlen($map),time(),'203.0.113.4']);
 $token=ofp_account_session($db,$id);$active=ofp_account_login($db,'Pilot','new-fixture-password-123');
 $ticket=ofp_cloud_ticket($db,$active,'test',$sid,$nonce,$kid);$device=ofp_device_start($db);ofp_device_approve($db,$device['pin'],$id);
 foreach(['account_resets','account_verifications'] as $table)$db->prepare("INSERT INTO $table VALUES(?,?,?)")->execute([bin2hex(random_bytes(32)),$id,time()+60]);
 account_reject(fn()=>ofp_account_delete($db,$id,'wrong'),'deletion requires password');
 account_check(ofp_account_from_token($db,$token)['id']===$id,'failed deletion leaves account intact');
 ofp_account_delete($db,$id,'new-fixture-password-123');
 account_check(!ofp_account_login($db,'Pilot','new-fixture-password-123')&&!ofp_account_from_token($db,$token),'deleted account cannot authenticate');
 account_check(!ofp_cloud_verify($db,$ticket,'test',$sid,$nonce,$kid)&&!ofp_device_poll($db,$device['device']),'deletion revokes tickets and pairings');
 foreach(['accounts'=>'id','account_client_preferences'=>'account_id','account_sessions'=>'account_id','account_preferences'=>'account_id','account_tickets'=>'account_id','account_devices'=>'account_id','account_resets'=>'account_id','account_verifications'=>'account_id'] as $table=>$field){
     $q=$db->prepare("SELECT COUNT(*) FROM $table WHERE $field = ?");$q->execute([$id]);account_check(!(int)$q->fetchColumn(),'deletion removes '.$table);
 }
 $after=ofp_ladder_table(ofp_ladders()[0]);
 account_check($before['teams'][0]['points']===$after['teams'][0]['points']&&$before['teams'][0]['won']===$after['teams'][0]['won'],'deletion retains team results');
 account_check($after['teams'][0]['members']===['Deleted player'],'team members anonymized');
 account_check(!str_contains(json_encode($after),'Pilot'),'ladder identity removed');
 $saved=json_decode($db->query('SELECT players FROM matches WHERE id='.$mid)->fetchColumn(),true);
 account_check($saved[0]['name']==='Deleted player'&&$saved[0]['won']===1&&$saved[0]['score']===90&&$saved[0]['kills']===4&&$saved[1]===$players[1],'only deleted participant identity changes');
 account_check((int)$db->query('SELECT score FROM account_results WHERE match_id='.$mid)->fetchColumn()===90,'account scores retained');
 $anonymous=$db->query('SELECT replay FROM replays WHERE match_id='.$mid)->fetchColumn();
 account_check(str_contains($anonymous,'name=DELETED_PLAYER')&&str_contains($anonymous,'name=Wingman')&&!str_contains($anonymous,'Pilot')&&!str_contains($anonymous,'pilot@example.invalid'),'hosted replay identity and contact removed');
 account_check(substr($anonymous,strpos($anonymous,"ticks\n"))===substr($replay,strpos($replay,"ticks\n")),'replay inputs and checksums unchanged');
 preg_match('/^map=(\d+)\n/m',$anonymous,$len,PREG_OFFSET_CAPTURE);$offset=$len[0][1]+strlen($len[0][0]);
 account_check(substr($anonymous,$offset+(int)$len[1][0],6)==="ticks\n",'embedded map length updated');
 foreach(['live','daily_queue'] as $table)account_check(!str_contains($db->query("SELECT replay FROM $table LIMIT 1")->fetchColumn(),'Pilot'),'hosted '.$table.' scrubbed');
 account_check(!(int)$db->query('SELECT COUNT(*) FROM daily')->fetchColumn()&&!(int)$db->query("SELECT COUNT(*) FROM leaders WHERE name='Pilot'")->fetchColumn(),'personal daily and leaderboard entries removed');
 $shared=$db->query('SELECT author,text,ip FROM gallery LIMIT 1')->fetch(PDO::FETCH_ASSOC);
 account_check($shared['author']===''&&$shared['ip']===''&&!str_contains($shared['text'],'Pilot')&&!str_contains($shared['text'],'pilot@example.invalid'),'map attribution and address erased');
 $tombstone=$db->query("SELECT * FROM deleted_accounts WHERE id='$id'")->fetch(PDO::FETCH_ASSOC);
 account_check($tombstone&&count($tombstone)===2&&!str_contains(json_encode($tombstone),'Pilot')&&json_decode($tombstone['ladder_teams'],true)===['fixture'=>'Iron Guard'],'late reports retain only anonymous team association');
 $late=ofp_public_replay_names($db,str_replace('bot=0 name=Wingman','bot=3 name=Bot_Three',$replay));
 account_check(str_contains($late,'name=DELETED_PLAYER')&&str_contains($late,'name=Bot_Three'),'late replay names anonymized and bots retained');
 account_check(ofp_public_people($db,'Pilot,Wingman')==='Wingman','deleted names cannot reappear in listing');
 account_reject(fn()=>ofp_account_delete($db,$id,'new-fixture-password-123'),'deletion cannot run twice');
 ofp_reset_data($db);foreach(ofp_reset_tables() as $t)account_check((int)$db->query("SELECT COUNT(*) FROM $t")->fetchColumn()===0,'reset empties '.$t);
 account_check(ofp_schema_version($db)===17,'reset preserves schema');
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
 $activated=ofp_account_activate($db,$token);
 if(!$activated||$activated['id']!==$id||$activated['username']!=='Pending'||!password_verify('pending-password-123',$activated['password_hash'])||ofp_account_activate($db,$token)||!ofp_account_login($db,'Pending','pending-password-123'))exit(12);
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
 echo "accounts: migrations 13/14→17, password policy, pending-account restrictions, activation, sessions, tickets, PIN pairing, recovery, preferences, account deletion, anonymous results/replays and full reset passed\n";
} finally {
 foreach(glob($fixture.'/inc/*') as $f)unlink($f);rmdir($fixture.'/inc');unlink($fixture.'/config.inc.php');rmdir($fixture);
}
