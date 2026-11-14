#!/usr/bin/env python3
"""End-to-end account HTTP checks against an isolated website and disposable database."""
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import socket
import socketserver
import subprocess
import tempfile
import threading
import time
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
dsn = os.environ.get('OFP_ACCOUNT_TEST_DSN', '')
if not re.fullmatch(r'mysql:.*dbname=openfp_account_test(?:;.*)?', dsn):
    raise SystemExit('Set OFP_ACCOUNT_TEST_DSN to the disposable openfp_account_test database.')

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

with tempfile.TemporaryDirectory(prefix='openfp-account-http-') as directory:
    fixture = Path(directory)
    for source in (ROOT / 'public-website').rglob('*.php'):
        if source.name == 'config.inc.php':
            continue
        target = fixture / source.relative_to(ROOT / 'public-website')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
    secret = os.urandom(32).hex()
    config = {'db': {'dsn': dsn, 'user': os.environ.get('OFP_ACCOUNT_TEST_USER', 'root'),
                     'password': os.environ.get('OFP_ACCOUNT_TEST_PASSWORD', ''), 'create_tables': True},
              'accounts': {'require_email_verification': False, 'servers': {'fixture': hashlib.sha256(secret.encode()).hexdigest()}},
              'smtp': {}, 'listing': {'probe': False, 'trusted_proxies': []}}
    def write_config():
        payload = base64.b64encode(json.dumps(config).encode()).decode()
        replacement = fixture / 'config.next.php'
        replacement.write_text("<?php return json_decode(base64_decode('" + payload + "'), true);")
        replacement.replace(fixture / 'config.inc.php')
    write_config()
    # Requests deliberately exercise rate limits; a previous run must not affect this run.
    subprocess.run(['php', '-r', 'require $argv[1]."/inc/accounts.php"; ofp_db()->exec("DELETE FROM account_limits");', str(fixture)], check=True)
    # Simulated TLS exists only in this temporary test router. Production has no such switch.
    (fixture / 'router.php').write_text("<?php $_SERVER['HTTPS']=isset($_SERVER['HTTP_X_OFP_FIXTURE_INSECURE'])?'off':'on'; return false;")
    port = free_port()
    env = dict(os.environ, OFP_RELEASE_OFFLINE='1')
    log = open(fixture / 'php.log', 'w+')
    process = subprocess.Popen(['php', '-d', 'opcache.enable=0', '-S', f'127.0.0.1:{port}', str(fixture / 'router.php')], cwd=fixture, env=env, stdout=log, stderr=log)
    try:
        for _ in range(50):
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1): break
            except OSError: time.sleep(.05)
        fixture_ip = '127.0.0.1'
        def request(path, values=None, cookie='', insecure=False, method=None):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
            headers = {'Cookie': cookie, 'X-Forwarded-For': fixture_ip}
            if insecure: headers['X-OFP-Fixture-Insecure'] = '1'
            if values is not None: headers['Content-Type'] = 'application/x-www-form-urlencoded'
            connection.request(method or ('POST' if values is not None else 'GET'), path,
                               urllib.parse.urlencode(values) if values is not None else None, headers)
            response = connection.getresponse()
            result = response.status, dict(response.getheaders()), response.read().decode()
            connection.close()
            return result
        def api(values, expected=200):
            status, headers, body = request('/account/api/', values)
            assert status == expected, (status, body)
            assert headers['Cache-Control'] == 'no-store'
            return dict(line.split('=', 1) for line in body.splitlines() if '=' in line)
        def csrf(body): return re.search(r'name="csrf" value="([0-9a-f]+)"', body).group(1)
        def session(headers): return headers['Set-Cookie'].split(';', 1)[0]
        assert request('/account/api/', {'action': 'login'}, insecure=True)[0] == 426
        assert request('/account/api/')[0] == 405
        status, headers, body = request('/account/status.php')
        assert status == 200 and json.loads(body) == {'profile': None}
        assert 'no-store' in headers['Cache-Control'] and 'Set-Cookie' not in headers
        assert headers['Content-Type'].startswith('application/json')
        assert request('/account/status.php', {})[0] == 405
        assert request('/api/', {'action': 'announce', 'port': 29870})[0] == 403
        name = 'Http' + os.urandom(4).hex()
        password = 'http-fixture-password-123'
        account = api({'action': 'register', 'username': name, 'email': name+'@example.invalid', 'password': password})
        assert len(account['session']) == len(account['key']) == 64
        api({'action': 'login', 'username': name, 'password': 'incorrect'}, 401)
        device = api({'action': 'device_start'})
        assert device['url'] == 'https://openfp.retrocs.org/account/pin'
        assert api({'action': 'device_poll', 'device': device['device']}, 202)['status'] == 'pending'
        status, headers, body = request('/account/pin/')
        cookie = session(headers)
        assert all(flag in headers['Set-Cookie'].lower() for flag in ['secure', 'httponly', 'samesite=lax'])
        assert 'Password recovery is disabled.' in body
        bad = request('/account/pin/', {'action': 'login', 'csrf': 'bad', 'pin': device['pin'], 'username': name, 'password': password}, cookie)[2]
        assert 'Reload the page' in bad
        status, headers, body = request('/account/pin/', {'action': 'login', 'csrf': csrf(body), 'pin': device['pin'], 'username': name, 'password': password}, cookie)
        assert 'Apple TV connected' in body
        assert session(headers) != cookie  # successful login rotates the browser session
        cookie = session(headers)
        status, headers, identity = request('/account/status.php', cookie=cookie)
        assert status == 200 and json.loads(identity) == {'profile': {'username': name, 'avatar': 0}}
        assert 'no-store' in headers['Cache-Control']
        assert json.loads(request('/account/status.php', cookie=cookie, insecure=True)[2]) == {'profile': None}
        paired = api({'action': 'device_poll', 'device': device['device']})
        assert paired['id'] == account['id'] and paired['key'] == account['key']
        api({'action': 'device_poll', 'device': device['device']}, 202)
        assert 'was already used' in request('/account/pin/', {'action': 'pair', 'csrf': csrf(body), 'pin': device['pin']}, cookie)[2]
        sid = '02' * 16
        verifier = hashlib.sha256(bytes.fromhex(account['key']) + bytes.fromhex(sid)).digest()
        kid = hashlib.sha256(verifier).hexdigest()[:32]
        binding = {'server': 'fixture', 'server_id': sid, 'nonce': '123456789abcdef0', 'key_id': kid}
        ticket = api(dict(binding, action='ticket', session=paired['session']))['ticket']
        api(dict(binding, action='verify', secret='0'*64, ticket=ticket), 403)
        assert api(dict(binding, action='verify', secret=secret, ticket=ticket))['id'] == account['id']
        api(dict(binding, action='verify', secret=secret, ticket=ticket), 403)
        # Browser and game settings use the same account, protected by CSRF/session credentials.
        assert api({'action':'profile','session':paired['session']})['preferences'] == '0'
        api({'action':'profile_save','session':'0'*64,'favorite_tank':3,'avatar':9,'steering':1},401)
        api({'action':'profile_save','session':paired['session'],'favorite_tank':4,'avatar':9,'steering':1},400)
        api({'action':'profile_save','session':paired['session'],'favorite_tank':3,'avatar':9,'steering':1})
        remote = api({'action':'profile','session':paired['session']})
        assert remote['favorite_tank']=='3' and remote['avatar']=='9' and remote['steering']=='1'
        _, _, settings = request('/account/',cookie=cookie)
        assert all(label in settings for label in ['Player settings','Favorite tank','Avatar','Changes save automatically.','Delete account','tank-picker','Previous tank','Rescue capacity'])
        assert '<h3>Steering</h3>' not in settings and 'name="steering"' not in settings
        assert 'data-account-link' in settings
        assert json.loads(request('/account/status.php', cookie=cookie)[2]) == {'profile': {'username': name, 'avatar': 9}}
        _, _, denied = request('/account/',{'action':'profile_save','csrf':'bad','favorite_tank':0,'avatar':0,'steering':0},cookie)
        assert 'Reload the page' in denied
        status, headers, _ = request('/account/',{'action':'profile_save','csrf':csrf(settings),'favorite_tank':2,'avatar':5,'steering':0},cookie)
        assert status==303 and headers['Location']=='/account/'
        remote=api({'action':'login','username':name,'password':password})
        assert remote['favorite_tank']=='2' and remote['avatar']=='5' and remote['steering']=='1'
        # Autosaves return JSON, preserve client steering and do not consume the login limiter.
        status, _, saved = request('/account/', {'action':'profile_save','format':'json','csrf':csrf(settings),'favorite_tank':1,'avatar':2,'username':'Hacked'},cookie)
        assert status==200 and json.loads(saved)=={'status':'saved'}
        assert json.loads(request('/account/status.php', cookie=cookie)[2]) == {'profile': {'username': name, 'avatar': 2}}
        status, _, denied = request('/account/', {'action':'profile_save','format':'json','csrf':'bad','favorite_tank':0,'avatar':0},cookie)
        assert status==403 and 'error' in json.loads(denied)
        status, _, denied = request('/account/', {'action':'profile_save','format':'json','csrf':csrf(settings),'favorite_tank':9,'avatar':0},cookie)
        assert status==400 and 'error' in json.loads(denied)
        for i in range(22):
            status, _, saved = request('/account/', {'action':'profile_save','format':'json','csrf':csrf(settings),'favorite_tank':2,'avatar':5},cookie)
            assert status==200, saved
        unchanged=api({'action':'profile','session':paired['session']})
        assert unchanged['username']==name and unchanged['steering']=='1'
        api({'action':'profile_save','session':paired['session'],'favorite_tank':2,'avatar':5,'steering':0,'client':'macos'})
        api({'action':'profile_save','session':paired['session'],'favorite_tank':2,'avatar':5,'steering':1,'client':'amigaos'})
        assert api({'action':'profile','session':paired['session'],'client':'macos'})['steering']=='0'
        assert api({'action':'login','username':name,'password':password,'client':'amigaos'})['steering']=='1'
        assert api({'action':'profile','session':paired['session'],'client':'windows'})['steering_saved']=='0'
        api({'action':'profile','session':paired['session'],'client':'unknown'},400)
        api({'action': 'logout', 'session': paired['session']})
        api(dict(binding, action='ticket', session=paired['session']), 401)
        delivered = []
        class SMTP(socketserver.StreamRequestHandler):
            def handle(self):
                self.wfile.write(b'220 fixture SMTP\r\n')
                while line := self.rfile.readline():
                    command = line.split(b' ', 1)[0].strip().upper()
                    if command == b'EHLO': self.wfile.write(b'250 fixture\r\n')
                    elif command in [b'MAIL', b'RCPT']: self.wfile.write(b'250 ok\r\n')
                    elif command == b'DATA':
                        self.wfile.write(b'354 message\r\n')
                        data = b''
                        while (part := self.rfile.readline()) not in [b'.\r\n', b'']:
                            data += part
                        delivered.append(data.decode())
                        self.wfile.write(b'250 accepted\r\n')
                    elif command == b'QUIT': break
                    else: self.wfile.write(b'500 unsupported\r\n')
        with socketserver.TCPServer(('127.0.0.1', 0), SMTP) as smtp:
            smtp.timeout = 10
            mail_thread = threading.Thread(target=smtp.serve_forever, daemon=True)
            mail_thread.start()
            config['smtp'] = {'host': '127.0.0.1', 'port': smtp.server_address[1], 'encryption': 'none',
                              'from': 'fixture@example.invalid', 'ehlo': 'localhost'}
            write_config()
            _, headers, body = request('/account/?mode=recover', cookie=cookie)
            assert 'Send recovery link' in body, body
            _, _, body = request('/account/?mode=recover', {'action': 'recover', 'csrf': csrf(body), 'email': name+'@example.invalid'}, cookie)
            assert len(delivered) == 1
            assert 'a recovery link will be sent' in body
            reset = re.search(r'/account/\?reset=([0-9a-f]{64})', delivered[0]).group(1)
            _, _, body = request('/account/?reset='+reset, cookie=cookie)
            _, _, body = request('/account/', {'action': 'reset', 'csrf': csrf(body), 'reset_token': reset, 'password': 'new-http-fixture-password'}, cookie)
            assert 'Password changed' in body
            assert json.loads(request('/account/status.php', cookie=cookie)[2]) == {'profile': None}
            api({'action': 'login', 'username': name, 'password': password}, 401)
            api({'action': 'login', 'username': name, 'password': 'new-http-fixture-password'})
            api(dict(binding, action='ticket', session=account['session']), 401)
            # New accounts need email activation by default. The form must show server-side
            # validation errors instead of silently failing browser minlength validation.
            config['accounts'].pop('require_email_verification')
            config['listing']['trusted_proxies'] = ['127.0.0.1']
            fixture_ip = '203.0.113.10'
            write_config()
            _, headers, body = request('/account/?mode=register')
            browser = session(headers)
            assert 'novalidate' in body and 'minlength="12"' in body and 'At least 12 characters' in body
            short_name = 'Short' + os.urandom(3).hex()
            short_email = short_name+'@example.invalid'
            _, _, body = request('/account/?mode=register', {'action':'register', 'csrf':csrf(body),
                                    'username':short_name, 'email':short_email, 'password':'test'}, browser)
            assert 'role="alert"' in body and 'Use at least 12 characters' in body
            assert f'value="{short_name}"' in body and f'value="{short_email}"' in body
            assert 'value="test"' not in body
            config['accounts']['password_min_length'] = 4
            write_config()
            _, _, body = request('/account/?mode=register', {'action':'register', 'csrf':csrf(body),
                                    'username':short_name, 'email':short_email, 'password':'test'}, browser)
            assert 'Account created. Check your email' in body and 'Resend activation email' in body
            assert 'Signed in as' not in body and len(delivered) == 2
            pending_name = 'Wait' + os.urandom(4).hex()
            pending_email = pending_name+'@example.invalid'
            pending = api({'action':'register', 'username':pending_name, 'email':pending_email, 'password':'test'}, 202)
            assert pending['status'] == 'verification_required' and not any(k in pending for k in ['id','key','session'])
            assert len(delivered) == 3
            api({'action':'login', 'username':pending_name, 'password':'test'}, 400)
            _, _, body = request('/account/pin/', cookie=browser)
            _, _, denied = request('/account/pin/', {'action':'login', 'csrf':csrf(body), 'pin':device['pin'],
                                    'username':pending_name, 'password':'test'}, browser)
            assert 'Activate your account using the email link' in denied and 'Apple TV connected' not in denied
            activation = re.search(r'/account/\?verify=([0-9a-f]{64})', delivered[-1]).group(1)
            # Insecure and HEAD requests must not consume a valid emailed token.
            _, _, bad = request('/account/?verify='+activation, cookie=browser, insecure=True)
            assert 'Use HTTPS to confirm your email' in bad and 'Signed in as' not in bad
            assert request('/account/?verify='+activation, cookie=browser, method='HEAD')[0] == 200
            api({'action':'login', 'username':pending_name, 'password':'test'}, 400)
            def database(code, *args):
                return subprocess.check_output(['php','-r','require $argv[1]."/inc/accounts.php"; $db=ofp_db(); '+code,
                                                str(fixture), *args], env=env).decode()
            assert database('$q=$db->prepare("SELECT v.token_hash FROM account_verifications v JOIN accounts a ON a.id=v.account_id WHERE a.username=?"); $q->execute([$argv[2]]); echo $q->fetchColumn();', pending_name) == hashlib.sha256(activation.encode()).hexdigest()
            database('$q=$db->prepare("UPDATE account_verifications v JOIN accounts a ON a.id=v.account_id SET v.expires=1 WHERE a.username=?"); $q->execute([$argv[2]]);', pending_name)
            _, _, expired = request('/account/?verify='+activation, cookie=browser)
            assert 'activation link has expired' in expired, re.findall(r'<p[^>]*role="alert"[^>]*>(.*?)</p>', expired)
            assert 'Signed in as' not in expired
            _, _, body = request('/account/?mode=activation', cookie=browser)
            _, _, body = request('/account/?mode=activation', {'action':'resend','csrf':csrf(body),'email':pending_email}, browser)
            assert len(delivered) == 3  # one email per account per minute
            database('$q=$db->prepare("SELECT id FROM accounts WHERE username=?"); $q->execute([$argv[2]]); $id=$q->fetchColumn(); $db->prepare("UPDATE account_limits SET started=1 WHERE bucket=?")->execute([hash("sha256","activation:".$id)]);', pending_name)
            _, _, body = request('/account/?mode=activation', {'action':'resend','csrf':csrf(body),'email':pending_email}, browser)
            assert len(delivered) == 4
            replacement = re.search(r'/account/\?verify=([0-9a-f]{64})', delivered[-1]).group(1)
            assert replacement != activation
            _, _, bad = request('/account/?verify='+activation, cookie=browser)
            assert 'activation link has expired' in bad
            assert 'Signed in as' not in bad
            _, _, bad = request('/account/', {'action':'activate','csrf':'wrong','verification_token':replacement}, browser)
            assert 'Reload the page' in bad
            assert 'Signed in as' not in bad
            old_csrf = csrf(body)
            status, headers, _ = request('/account/?verify='+replacement, cookie=browser)
            assert status == 303 and headers['Location'] == '/account/'
            signed_browser = session(headers)
            assert signed_browser != browser
            assert all(flag in headers['Set-Cookie'].lower() for flag in ['secure', 'httponly', 'samesite=lax'])
            _, _, body = request('/account/', cookie=signed_browser)
            assert 'Email confirmed. Your account is active and you are signed in.' in body
            assert 'Your cloud profile' in body and f'Signed in as <strong>{pending_name}</strong>' in body
            assert 'verification_token' not in body and 'Permanently delete account' in body
            assert body.count('name="password"') == 1  # deletion confirmation, never a sign-in form
            assert csrf(body) != old_csrf
            _, _, body = request('/account/', cookie=signed_browser)
            assert f'Signed in as <strong>{pending_name}</strong>' in body  # session persists on refresh
            _, headers, anonymous = request('/account/', cookie=browser)
            assert 'Signed in as' not in anonymous  # previous session cannot inherit the login
            browser = session(headers)
            active = api({'action':'login','username':pending_name,'password':'test'})
            assert len(active['session']) == 64
            _, _, body = request('/account/?verify='+replacement, cookie=browser)
            assert 'activation link has expired' in body
            assert 'Signed in as' not in body  # a used token cannot create another authenticated session
            # Browser account controls: new email needs proof, username stays fixed, password revokes access.
            fixture_ip='203.0.113.12'
            manage_name='Edit'+os.urandom(3).hex()
            managed=api({'action':'register','username':manage_name,'email':manage_name+'@example.invalid','password':'test'},202)
            activate=re.search(r'/account/\?verify=([0-9a-f]{64})',delivered[-1]).group(1)
            # Verification signs in to the account from the link, even if another account is open.
            status, mh, _=request('/account/?verify='+activate,cookie=signed_browser)
            assert status==303 and mh['Location']=='/account/'
            mc=session(mh)
            assert mc!=signed_browser
            _, _, mb=request('/account/',cookie=mc)
            active_managed=api({'action':'login','username':manage_name,'password':'test'})
            second_browser=request('/account/')
            sc=session(second_browser[1])
            _, sh, _=request('/account/',{'action':'login','csrf':csrf(second_browser[2]),'username':manage_name,'password':'test'},sc)
            sc=session(sh)
            new_email=manage_name+'-new@example.invalid'
            changes={'action':'change_email','csrf':csrf(mb),'current_password':'test','new_email':new_email,'username':'RenameAttempt'}
            status, _, bad=request('/account/',dict(changes,csrf='bad'),mc)
            assert 'Reload the page' in bad
            status, _, bad=request('/account/',dict(changes,current_password='wrong'),mc)
            assert 'Enter your current password' in bad
            before=len(delivered)
            status, _, _=request('/account/',changes,mc)
            assert status==303 and len(delivered)==before+1
            email_link=re.search(r'/account/\?confirm_email=([0-9a-f]{64})',delivered[-1]).group(1)
            assert database('$q=$db->prepare("SELECT email FROM accounts WHERE username=?");$q->execute([$argv[2]]);echo $q->fetchColumn();',manage_name)==manage_name.lower()+'@example.invalid'
            assert database('$q=$db->prepare("SELECT token_hash FROM account_email_changes WHERE account_id=?");$q->execute([$argv[2]]);echo $q->fetchColumn();',active_managed['id'])==hashlib.sha256(email_link.encode()).hexdigest()
            # Opening the email-change link confirms and signs in without an existing browser session.
            assert request('/account/?confirm_email='+email_link,method='HEAD')[0]==200
            _, _, bad=request('/account/?confirm_email='+email_link,insecure=True)
            assert 'Use HTTPS to confirm your email' in bad
            assert database('$q=$db->prepare("SELECT email FROM accounts WHERE username=?");$q->execute([$argv[2]]);echo $q->fetchColumn();',manage_name)==manage_name.lower()+'@example.invalid'
            status, mh, _=request('/account/?confirm_email='+email_link)
            assert status==303 and mh['Location']=='/account/'
            mc=session(mh)
            _, _, mb=request('/account/',cookie=mc)
            assert 'Your new email address is confirmed and you are signed in.' in mb
            assert f'Signed in as <strong>{manage_name}</strong>' in mb
            assert database('$q=$db->prepare("SELECT email FROM accounts WHERE username=?");$q->execute([$argv[2]]);echo $q->fetchColumn();',manage_name)==new_email.lower()
            _, _, bad=request('/account/?confirm_email='+email_link)
            assert 'link has expired or was already used' in bad and 'Signed in as' not in bad
            assert api({'action':'profile','session':active_managed['session']})['username']==manage_name
            # Permit another request, cancel it, and prove its token cannot change the email.
            database('$db->exec("DELETE FROM account_limits");')
            _, _, mb=request('/account/',cookie=mc)
            request('/account/',dict(changes,csrf=csrf(mb),new_email=manage_name+'-cancel@example.invalid'),mc)
            cancelled=re.search(r'/account/\?confirm_email=([0-9a-f]{64})',delivered[-1]).group(1)
            request('/account/',{'action':'cancel_email','csrf':csrf(mb)},mc)
            _, _, bad=request('/account/?confirm_email='+cancelled,cookie=mc)
            assert 'link has expired' in bad
            password_change={'action':'change_password','csrf':csrf(mb),'current_password':'test','new_password':'replacement-pass','confirm_password':'other'}
            assert 'do not match' in request('/account/',password_change,mc)[2]
            password_change['confirm_password']='replacement-pass'
            assert 'current password' in request('/account/',dict(password_change,current_password='wrong'),mc)[2]
            status, mh, _=request('/account/',password_change,mc)
            assert status==303 and session(mh)!=mc
            mc=session(mh)
            _, _, mb=request('/account/',cookie=mc)
            assert 'Password changed. You are still signed in here' in mb
            assert f'Signed in as <strong>{manage_name}</strong>' in mb
            assert 'Signed in as' not in request('/account/',cookie=sc)[2]
            api({'action':'profile','session':active_managed['session']},401)
            api({'action':'login','username':manage_name,'password':'test'},401)
            assert api({'action':'login','username':manage_name,'password':'replacement-pass'})['id']==active_managed['id']
            # Email edits cannot silently skip confirmation when registration activation is disabled.
            config['accounts']['require_email_verification']=False
            write_config()
            database('$db->exec("DELETE FROM account_limits");')
            request('/account/',dict(changes,csrf=csrf(mb),current_password='replacement-pass',new_email=manage_name+'-off@example.invalid'),mc)
            _, _, mb=request('/account/',cookie=mc)
            assert 'Waiting for confirmation' in mb
            assert database('$q=$db->prepare("SELECT email FROM accounts WHERE username=?");$q->execute([$argv[2]]);echo $q->fetchColumn();',manage_name)==new_email.lower()
            config['accounts'].pop('require_email_verification')
            write_config()
            # No SMTP bypass: enabling activation without mail blocks registration clearly.
            config['smtp'] = {}
            write_config()
            unavailable = api({'action':'register','username':'NoMail'+os.urandom(2).hex(),
                               'email':'nomail'+os.urandom(3).hex()+'@example.invalid','password':'test'}, 400)
            assert 'email activation needs the site mail service' in unavailable['error'], unavailable
            # Disabling activation permits immediate sign-in, including accounts still pending.
            config['accounts']['require_email_verification'] = False
            fixture_ip = '203.0.113.11'
            write_config()
            api({'action':'login','username':short_name,'password':'test'})
            immediate = api({'action':'register','username':'Now'+os.urandom(3).hex(),
                             'email':'now'+os.urandom(3).hex()+'@example.invalid','password':'test'})
            assert len(immediate['session']) == 64
            # Deletion is explicit, requires the current password, and revokes browser/client access.
            delete_name = 'Gone'+os.urandom(3).hex()
            deleted = api({'action':'register','username':delete_name,'email':delete_name+'@example.invalid','password':'test'})
            announcement={'action':'announce','cloud_server':'fixture','cloud_secret':secret,'port':29870,
                          'name':'HTTP test server','players':1,'max_players':2,'people':delete_name,
                          'leaders':delete_name+'\t1200\t1\t1\t90'}
            assert request('/api/',announcement)[0]==200
            replay='OFPREPLAY 2\nplayer=0 bot=0 name='+delete_name+'\n'
            report=dict(announcement,action='match',seconds=30,world='classic',goal='flag',winner='camp',
                        players='\t'.join([delete_name,'camp','1','4','90','1200','0','0','1','0',deleted['id']]),replay=replay)
            status, _, reply=request('/api/',report)
            assert status==200,reply
            match_id=int(reply.split()[-1])
            assert request('/api/',dict(announcement,action='live',replay=replay))[0]==200
            _, headers, body = request('/account/')
            delete_browser=session(headers)
            _, headers, body = request('/account/',{'action':'login','csrf':csrf(body),'username':delete_name,'password':'test'},delete_browser)
            delete_browser=session(headers)
            assert 'Signed in as' in body
            _, _, denied = request('/account/',{'action':'delete_account','csrf':csrf(body),'password':'test'},delete_browser)
            assert 'Confirm that you want' in denied
            _, _, denied = request('/account/',{'action':'delete_account','csrf':'bad','password':'test','confirm_delete':'yes'},delete_browser)
            assert 'Reload the page' in denied
            _, _, denied = request('/account/',{'action':'delete_account','csrf':csrf(body),'password':'wrong','confirm_delete':'yes'},delete_browser)
            assert 'current password' in denied
            assert api({'action':'profile','session':deleted['session']})['username']==delete_name
            status, headers, _ = request('/account/',{'action':'delete_account','csrf':csrf(body),'password':'test','confirm_delete':'yes'},delete_browser)
            assert status==303 and headers['Location']=='/account/' and session(headers)!=delete_browser
            _, _, body = request('/account/',cookie=session(headers))
            assert 'personal details have been deleted' in body and 'Signed in as' not in body
            api({'action':'profile','session':deleted['session']},401)
            api({'action':'login','username':delete_name,'password':'test'},401)
            # An older browser cookie also loses access; it cannot save or restore the account.
            assert 'Signed in as' not in request('/account/',cookie=delete_browser)[2]
            _, _, scrubbed=request('/api/?action=replay&id='+str(match_id))
            assert delete_name not in scrubbed and 'DELETED_PLAYER' in scrubbed
            # Delayed reports stay anonymous, even if someone registers the old username again.
            api({'action':'register','username':delete_name,'email':'replacement'+os.urandom(3).hex()+'@example.invalid','password':'test'})
            status, _, reply=request('/api/',report)
            assert status==200,reply
            late_id=int(reply.split()[-1])
            _, _, scrubbed=request('/api/?action=replay&id='+str(late_id))
            assert delete_name not in scrubbed and 'DELETED_PLAYER' in scrubbed
            _, _, matches=request('/api/?action=matches')
            late=next(m for m in json.loads(matches)['matches'] if m['id']==late_id)
            assert late['players'][0]['name']=='Deleted player' and late['players'][0]['won']==1
            assert late['players'][0]['score']==90 and late['players'][0]['deleted']==1
            smtp.shutdown()
            mail_thread.join(timeout=10)
        print('account HTTP: autosave, immutable usernames, confirmed email replacement/cancellation, password change and session revocation; visible validation, configurable passwords, activation email/expiry/resend, one-click verification and browser sign-in, pending-account denial, optional activation, PIN pairing, tickets, password recovery, shared profile settings and confirmed account deletion passed')
    finally:
        process.terminate()
        process.wait(timeout=10)
        log.seek(0)
        errors = log.read()
        log.close()
        if 'Fatal error' in errors or 'PHP Warning' in errors:
            raise AssertionError(errors)
