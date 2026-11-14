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
              'accounts': {'servers': {'fixture': hashlib.sha256(secret.encode()).hexdigest()}},
              'smtp': {}, 'listing': {'probe': False, 'trusted_proxies': []}}
    payload = base64.b64encode(json.dumps(config).encode()).decode()
    (fixture / 'config.inc.php').write_text("<?php return json_decode(base64_decode('" + payload + "'), true);")
    # Simulated TLS exists only in this temporary test router. Production has no such switch.
    (fixture / 'router.php').write_text("<?php $_SERVER['HTTPS']=isset($_SERVER['HTTP_X_OFP_FIXTURE_INSECURE'])?'off':'on'; return false;")
    port = free_port()
    env = dict(os.environ, OFP_RELEASE_OFFLINE='1')
    log = open(fixture / 'php.log', 'w+')
    process = subprocess.Popen(['php', '-S', f'127.0.0.1:{port}', str(fixture / 'router.php')], cwd=fixture, env=env, stdout=log, stderr=log)
    try:
        for _ in range(50):
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1): break
            except OSError: time.sleep(.05)
        def request(path, values=None, cookie='', insecure=False):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
            headers = {'Cookie': cookie}
            if insecure: headers['X-OFP-Fixture-Insecure'] = '1'
            if values is not None: headers['Content-Type'] = 'application/x-www-form-urlencoded'
            connection.request('POST' if values is not None else 'GET', path,
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
            mail_thread = threading.Thread(target=smtp.handle_request)
            mail_thread.start()
            config['smtp'] = {'host': '127.0.0.1', 'port': smtp.server_address[1], 'encryption': 'none',
                              'from': 'fixture@example.invalid', 'ehlo': 'localhost'}
            payload = base64.b64encode(json.dumps(config).encode()).decode()
            (fixture / 'config.inc.php').write_text("<?php return json_decode(base64_decode('" + payload + "'), true);")
            _, headers, body = request('/account/?mode=recover', cookie=cookie)
            assert 'Send recovery link' in body
            _, _, body = request('/account/?mode=recover', {'action': 'recover', 'csrf': csrf(body), 'email': name+'@example.invalid'}, cookie)
            mail_thread.join(timeout=10)
            assert not mail_thread.is_alive() and len(delivered) == 1
            assert 'a recovery link will be sent' in body
            reset = re.search(r'/account/\?reset=([0-9a-f]{64})', delivered[0]).group(1)
            _, _, body = request('/account/?reset='+reset, cookie=cookie)
            _, _, body = request('/account/', {'action': 'reset', 'csrf': csrf(body), 'reset_token': reset, 'password': 'new-http-fixture-password'}, cookie)
            assert 'Password changed' in body
            api({'action': 'login', 'username': name, 'password': password}, 401)
            api({'action': 'login', 'username': name, 'password': 'new-http-fixture-password'})
            api(dict(binding, action='ticket', session=account['session']), 401)
        print('account HTTP: HTTPS, credentials, CSRF, secure browser sessions, PIN approval, tickets, logout and SMTP password recovery passed')
    finally:
        process.terminate()
        process.wait(timeout=10)
        log.seek(0)
        errors = log.read()
        log.close()
        if 'Fatal error' in errors or 'PHP Warning' in errors:
            raise AssertionError(errors)
