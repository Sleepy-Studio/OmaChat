#!/usr/bin/env python3
"""Real isolated TLS server + null-audio desktop daemon + Android instrumentation.
Only creates disposable accounts. Never opens installed desktop configuration.
Requires a running emulator (10.0.2.2), SDK and built CMake targets.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import sqlite3
import shlex
import subprocess
import sys
import tempfile
import time
import re
from opus_reference import verify_android_packet

ROOT = Path(__file__).resolve().parents[2]

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def wait(check, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = check()
        if result:
            return result
        time.sleep(.1)
    raise RuntimeError('Fixture timed out')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--desktop-binary', type=Path, help='Optional legacy desktop daemon for interoperability verification')
    parser.add_argument('--test-class', help='Run a focused instrumentation class/method against the disposable fixture')
    parser.add_argument('--crypto-only', action='store_true', help='Run only the encrypted slice against a new disposable fixture')
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--port', type=int, help='Unused local TCP port; default is ephemeral')
    parser.add_argument('--serial', help='Exact adb device serial; required when several devices are attached')
    parser.add_argument('--host', default='10.0.2.2', help='Phone-reachable IP of this computer (LAN), or emulator host alias')
    parser.add_argument('--bind', default='127.0.0.1', help='Local address on which the disposable server listens')
    args = parser.parse_args()
    adb = ['adb'] + (['-s', args.serial] if args.serial else [])
    desktop_host = 'localhost' if args.bind == '127.0.0.1' else args.bind
    build = args.build.resolve()
    children = []
    with tempfile.TemporaryDirectory(prefix='oc-android-', dir='/tmp') as temp:
        path = Path(temp)
        port, media = args.port or free_port(), free_port()
        sock = path / 'desktop.sock'
        cert, key = path / 'cert.pem', path / 'key.pem'
        subprocess.run([str(build/'server/omachat-server'), 'generate-cert', '--cert', str(cert), '--key', str(key), '--name', 'localhost', '--name', args.host], check=True, capture_output=True)
        config = path / 'server.toml'
        config.write_text(f'''[server]\nname = "Android integration"\nbind = "{args.bind}"\nport = {port}\nregistration_open = true\n[media]\nudp_port = {media}\n[tls]\ncertificate = "{cert}"\nprivate_key = "{key}"\n[database]\npath = "{path / 'server.db'}"\n[files]\npath = "{path / 'files'}"\n''')
        logs = []
        def spawn(command, env=None):
            log = open(path/f'{len(children)}.log', 'w')
            logs.append(log)
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env)
            children.append(child)
            return child
        def ctl(*command, stdin=None):
            result = subprocess.run([str(build/'cli/omachatctl'), '--socket', str(sock), '--json', *command], input=stdin, text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError(result.stderr or result.stdout)
            return json.loads(result.stdout)
        try:
            spawn([str(build/'server/omachat-server'), '--config', str(config)])
            env = dict(os.environ, XDG_CONFIG_HOME=str(path/'config'), XDG_DATA_HOME=str(path/'data'), XDG_CACHE_HOME=str(path/'cache'))
            spawn([str(args.desktop_binary or build/'daemon/omachatd'), '--socket', str(sock), '--database', str(path/'desktop.db'), '--config', str(path/'desktop.toml'), '--memory-credentials', '--null-audio', '--no-notifications'], env)
            wait(sock.exists)
            try:
                ctl('account', 'register', f'{desktop_host}:{port}', 'androidfixture', '--password-stdin', stdin='fixture-password-123\n')
            except RuntimeError:
                pass  # first trust challenge; independently verify below
            status = wait(lambda: ctl('status') if ctl('status').get('error', {}).get('fingerprint') else None)
            fingerprint = status['error']['fingerprint']
            ctl('trust', fingerprint)
            ctl('account', 'register', f'{desktop_host}:{port}', 'androidfixture', '--password-stdin', stdin='fixture-password-123\n')
            wait(lambda: ctl('status').get('state') == 'connected')
            ctl('server', 'create', 'Android integration')
            ctl('message', 'send', 'general', 'desktop-to-android')
            # Direct instrumentation preserves the installed APK and its data.
            subprocess.run([str(ROOT/'android/gradlew'), '-p', str(ROOT/'android'),
                            ':app:assembleDebug', ':app:assembleDebugAndroidTest'], check=True)
            subprocess.run(adb + ['install', '-r', str(ROOT/'android/app/build/outputs/apk/debug/app-debug.apk')], check=True)
            subprocess.run(adb + ['install', '-r', str(ROOT/'android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk')], check=True)
            extra_args = []
            def instrument(classes):
                command = adb + ['shell', 'am', 'instrument', '-w', '-r',
                    '-e', 'class', classes,
                    '-e', 'serverPort', str(port), '-e', 'serverHost', args.host,
                    '-e', 'serverFingerprint', fingerprint.removeprefix('SHA256:'),
                    *extra_args, 'org.omachat.android.debug.test/androidx.test.runner.AndroidJUnitRunner']
                # adb shell joins argv without quoting: safety numbers contain spaces.
                shell_index = command.index('shell')
                command = command[:shell_index + 1] + [shlex.join(command[shell_index + 1:])]
                result = subprocess.run(command, check=True, text=True, capture_output=True, timeout=180)
                print(result.stdout)
                packets = re.findall(r'androidOpus=([A-Za-z0-9+/=]+)', result.stdout)
                if 'org.omachat.android.VoiceAudioTest' in classes.split(',') and not packets:
                    raise RuntimeError('Android Opus interoperability packet was not reported')
                for encoded in packets:
                    verify_android_packet(encoded)
                    print('PASS: host desktop libopus decoded Android 440 Hz frame')
                if 'OK (' not in result.stdout or 'FAILURES!!!' in result.stdout:
                    raise RuntimeError('Android instrumentation did not pass')
            if args.test_class:
                instrument(args.test_class)
                print('PASS: focused instrumentation against disposable TLS fixture')
                return
            if not args.crypto_only:
                instrument('org.omachat.android.ServerIntegrationTest,org.omachat.android.RoomPersistenceTest,org.omachat.android.MessageCacheTest,org.omachat.android.TransferTest,org.omachat.android.LoginSessionTest,org.omachat.android.MainThreadLifecycleTest,org.omachat.android.HistoricalSenderTest')
                instrument('org.omachat.android.ProcessDeathTest#seedBeforeProcessDeath')
                subprocess.run(adb + ['shell', 'am', 'force-stop', 'org.omachat.android.debug'], check=True)
                instrument('org.omachat.android.ProcessDeathTest#recoverAfterProcessDeath')
                history = ctl('message', 'history', 'general', '--limit', '100')
                if 'android-to-desktop' not in json.dumps(history) and history.get('has_more'):
                    messages = history['messages']
                    oldest = min(messages, key=lambda message: (int(message.get('timestamp', 0)), int(message['id'])))
                    history = ctl('message', 'history', 'general', '--limit', '100', '--before', oldest['id'])
                if 'android-to-desktop' not in json.dumps(history):
                    raise RuntimeError('Desktop did not observe the Android message')
            invite = ctl('invite', 'create', 'Android integration')['uri'].rsplit('/', 1)[-1]
            extra_args = ['-e', 'fixtureInvite', invite]
            instrument('org.omachat.android.EncryptedConversationTest#nativeCryptoRejectsTamperingWrongContextAndBounds')
            instrument('org.omachat.android.EncryptedConversationTest#prepareIndependentAndroidDevice')
            # Wait until the desktop's real directory observes Android's own published key.
            safety = wait(lambda: (value if value.get('devices') == 1 else None)
                          if (value := ctl('e2e', 'safety', 'androidcrypto')) else None)
            encrypted = ctl('dm', 'androidcrypto', 'desktop encrypted secret')
            channel_id = encrypted['channel_id']
            desktop_file = path / 'desktop-private.bin'
            desktop_file.write_bytes(bytes((index * 13) % 256 for index in range(200000)))
            ctl('message', 'send', str(channel_id), 'desktop encrypted file', '--attach', str(desktop_file))
            group = ctl('group', 'create', 'androidcrypto', 'cryptogroup', '--name', 'Encrypted fixture group')
            group_id = group['id']
            ctl('message', 'send', str(group_id), 'desktop encrypted group')
            extra_args += ['-e', 'desktopSafety', safety['number']]
            instrument('org.omachat.android.EncryptedConversationTest#decryptDesktopAndSendEncryptedReplyAndEdit')
            private_history = ctl('message', 'history', str(channel_id), '--limit', '100')
            if 'android encrypted edited' not in json.dumps(private_history):
                raise RuntimeError('Desktop could not decrypt Android encrypted edit')
            with sqlite3.connect(path / 'server.db') as database:
                stored = database.execute('SELECT content, encrypted FROM messages WHERE channel_id=?', (channel_id,)).fetchall()
                if len(stored) != 4 or any(content or not ciphertext for content, ciphertext in stored):
                    raise RuntimeError('Private bodies were stored in plaintext or missing')
            file_message = next(message for message in private_history['messages'] if message.get('content') == 'android encrypted file')
            received_path = path / 'received-private.bin'
            ctl('attachment', 'get', file_message['attachments'][0]['id'], '--output', str(received_path))
            if received_path.read_bytes() != bytes((index * 7) % 256 for index in range(131072)):
                raise RuntimeError('Desktop encrypted file bytes differ')
            if 'android encrypted group' not in json.dumps(ctl('message', 'history', str(group_id))):
                raise RuntimeError('Desktop did not decrypt the Android group message')
            subprocess.run(adb + ['shell', 'am', 'force-stop', 'org.omachat.android.debug'], check=True)
            instrument('org.omachat.android.EncryptedConversationTest#encryptedHistorySurvivesForceStop')
            print('PASS: independent-device encrypted text/reply/edit/files/groups, safety numbers, key-change blocking and force-stop recovery' + ('' if args.crypto_only else '; community/reliability/persistence regression phases'))
        finally:
            for child in reversed(children):
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait()
            for log in logs:
                log.close()
            for log in sorted(path.glob('*.log')):
                # Fixture-only logs; no production users or credentials.
                if any(child.returncode not in (0, -15) for child in children):
                    print(log.read_text()[-3000:])

    if not args.test_class and not args.crypto_only:
        # Registration is deliberately rate-limited per peer IP. Voice adds two
        # accounts; use a fresh disposable server rather than weakening that guard
        # or making the existing crypto/persistence fixture timing-dependent.
        subprocess.run([sys.executable, str(Path(__file__).resolve()), *sys.argv[1:],
                        '--test-class', 'org.omachat.android.VoiceMediaTest,org.omachat.android.VoiceAudioTest'], check=True)
        # Application ownership tests use several explicit logins and sign-outs.
        # A third fresh fixture keeps the normal authentication rate limit intact.
        subprocess.run([sys.executable, str(Path(__file__).resolve()), *sys.argv[1:],
                        '--test-class', 'org.omachat.android.VoiceOwnershipTest'], check=True)
        print('PASS: complete Android workflow, including isolated voice transport and application ownership fixtures')

if __name__ == '__main__':
    main()
