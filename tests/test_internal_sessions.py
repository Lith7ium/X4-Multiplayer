"""Internal Windows/Linux role and invitation boundaries."""
import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from contextlib import nullcontext
from unittest.mock import patch
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('internal_launcher',ROOT/'tools/distribution/session_launcher.py')
launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)

class InternalSessions(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.host=self.package('host','Windows11');self.guest=self.package('guest','Linux')
    def package(self,name,platform):
        p=self.root/name;(p/'extension/x4_lan_lab/ui').mkdir(parents=True)
        (p/'package.json').write_text(json.dumps(dict(channel='internal',version=launcher.VERSION,platform=platform,slot=None,steam_build='23660954')))
        return p
    def configure(self):
        with patch.object(launcher,'ROOT',self.host):launcher.configure('host','192.0.2.109')
        with patch.object(launcher,'ROOT',self.guest):launcher.configure('guest',invite=self.host/'JOIN-SESSION.json')
        return json.loads((self.host/'JOIN-SESSION.json').read_text())
    def test_windows_host_linux_guest_share_world_protocol_and_ship_spec(self):
        invite=self.configure()
        for folder,slot,address in ((self.host,'p1','127.0.0.1'),(self.guest,'p2','192.0.2.109')):
            with patch.object(launcher,'ROOT',folder):meta,cfg=launcher.configured()
            self.assertEqual((meta['slot'],cfg['host'],cfg['port']),(slot,address,45841))
            self.assertEqual(cfg['token'],invite['token']);self.assertTrue(cfg['world_sync']);self.assertTrue(cfg['fire2'])
            lua=(folder/'extension/x4_lan_lab/ui/config.lua').read_text()
            for text in ('world_sync = true','fire2 = true','ship_weapons = 6','ship_ter_s_xperimental_01_a_macro','test_profile = "45841"', 'coop_docks = true', 'pilot_health = true', 'shot_claims = true', 'shot_visual = true', 'shot_capture = true', 'projectile_visuals = true', 'shot_undo = false', 'npc_combat = true', 'faction_missions = true', 'npc_motion_diagnostics = 1'):
                self.assertIn(text,lua)
    def test_dedicated_authority_and_two_guests_have_explicit_ships(self):
        guest2 = self.package('guest2', 'Windows11')
        for folder, role in ((self.host, 'host'), (self.guest, 'guest'), (guest2, 'guest2')):
            meta = json.loads((folder / 'package.json').read_text())
            meta.update(dedicated=True, role=role)
            (folder / 'package.json').write_text(json.dumps(meta))
        with patch.object(launcher, 'ROOT', self.host):
            launcher.configure('host', '192.0.2.109')
        for folder in (self.guest, guest2):
            with patch.object(launcher, 'ROOT', folder):
                launcher.configure('guest', invite=self.host / 'JOIN-SESSION.json')
        for folder, slot, ship, authority in ((self.host, 'p1', 1, True), (self.guest, 'p2', 2, False), (guest2, 'p3', 1, False)):
            with patch.object(launcher, 'ROOT', folder):
                meta, session = launcher.configured()
            self.assertTrue(session['dedicated'])
            lua = (folder / 'extension/x4_lan_lab/ui/config.lua').read_text()
            self.assertIn(f'slot = "{slot}"', lua)
            self.assertIn(f'local_ship = {ship}', lua)
            self.assertIn('dedicated_session = true', lua)
            self.assertIn(f'dedicated = {str(authority).lower()}', lua)
        self.assertIn('dedicated_control_path', (self.host / 'extension/x4_lan_lab/ui/config.lua').read_text())
        (self.host / 'installation.json').write_text(json.dumps(dict(game='/game', profile='/profile/45841')))
        backend = SimpleNamespace(WorldServer=lambda *_: nullcontext(), prepare_play=lambda *_: None)
        with patch.object(launcher, 'ROOT', self.host), patch.object(launcher, 'backend_module', return_value=backend), \
                patch.object(launcher.subprocess, 'run') as run:
            launcher.server(console=True)
        argv = run.call_args.args[0]
        self.assertEqual(argv[-1], 'run')
        config = json.loads((self.host / '.local/dedicated/config.json').read_text())
        self.assertEqual((config['game'], config['profile'], config['package']), ('/game/X4', '/profile/45841', str(self.host)))

    def test_four_player_dedicated_session_declares_players_profiles_and_ships(self):
        clients = [(self.guest, 'guest'), (self.package('guest2', 'Windows11'), 'guest2'),
                   (self.package('guest3', 'Windows11'), 'guest3')]
        for folder, role, profile in ((self.host, 'host', '45841'), (clients[0][0], 'guest', '45841'),
                                      (clients[1][0], 'guest2', '45842'), (clients[2][0], 'guest3', '45843')):
            meta = json.loads((folder / 'package.json').read_text())
            meta.update(dedicated=True, role=role, max_players=4, test_profile=profile)
            (folder / 'package.json').write_text(json.dumps(meta))
        with patch.object(launcher, 'ROOT', self.host):
            launcher.configure('host', '192.0.2.109')
        invite = json.loads((self.host / 'JOIN-SESSION.json').read_text())
        self.assertEqual(invite['max_players'], 4)
        self.assertIn('-np1-p4-', invite['build'])
        for folder, _ in clients:
            with patch.object(launcher, 'ROOT', folder):
                launcher.configure('guest', invite=self.host / 'JOIN-SESSION.json')
        expected = ((self.host, 'p1', 1, '45841'), (clients[0][0], 'p2', 2, '45841'),
                    (clients[1][0], 'p3', 1, '45842'), (clients[2][0], 'p4', 3, '45843'))
        for folder, slot, ship, profile in expected:
            with patch.object(launcher, 'ROOT', folder):
                meta, session = launcher.configured()
            lua = (folder / 'extension/x4_lan_lab/ui/config.lua').read_text()
            for text in (f'slot = "{slot}"', f'local_ship = {ship}', 'max_players = 4', f'test_profile = "{profile}"',
                         f'build = "{invite["build"]}"'):
                self.assertIn(text, lua)
        # A player-4 package cannot join a three-player session; a mismatched build is refused.
        late = self.package('late', 'Windows11')
        meta = json.loads((late / 'package.json').read_text())
        meta.update(dedicated=True, role='guest3', test_profile='45843')
        (late / 'package.json').write_text(json.dumps(meta))
        three = dict(invite, max_players=3, build=launcher.build_label(3))
        (self.root / 'three.json').write_text(json.dumps(three))
        with patch.object(launcher, 'ROOT', late), self.assertRaises(RuntimeError):
            launcher.configure('guest', invite=self.root / 'three.json')
        for bad in (dict(invite, build=launcher.build_label(3)), dict(invite, max_players=9),
                    dict(invite, build=launcher.BUILD)):
            with self.assertRaises(ValueError):
                launcher.validate_invite(bad)
        self.assertEqual([launcher.role_slot(r) for r in ('host', 'guest', 'guest2', 'guest7')], ['p1', 'p2', 'p3', 'p8'])
        with self.assertRaises(RuntimeError):
            launcher.role_slot('guest8')

    def test_python_relay_refuses_dedicated_and_n_player_sessions(self):
        from x4lan.host import require_two_players
        require_two_players({'coop': True})
        for config in ({'dedicated': True}, {'max_players': 3}):
            with self.assertRaises(ValueError):
                require_two_players(config)

    def test_separate_test_groups_get_distinct_keys_and_world_ids(self):
        a=self.configure();other=self.package('other','Windows11')
        with patch.object(launcher,'ROOT',other):launcher.configure('host','192.0.2.202')
        b=json.loads((other/'JOIN-SESSION.json').read_text())
        self.assertNotEqual(a['token'],b['token']);self.assertNotEqual(a['bubble'],b['bubble'])
    def test_seed_admission_preserves_both_roles_and_enables_candidate_gameplay(self):
        from distribution.world_launcher import install_play_config
        from lupa.luajit21 import LuaRuntime
        invite = self.configure()
        for folder, slot in ((self.host, 'p1'), (self.guest, 'p2')):
            template = folder / 'extension/x4_lan_lab/ui/config.lua'
            before = template.read_bytes()
            target = self.root / ('installed-' + slot)
            (target / 'ui').mkdir(parents=True)
            install_play_config(folder, target, {'extensions': [{'id': 'ego_dlc_timelines', 'version': '900'}]})
            lua = LuaRuntime()
            lua.execute((target / 'ui/config.lua').read_text())
            cfg = lua.globals().X4LAN_CONFIG
            self.assertEqual((cfg.slot, cfg.token, cfg.seed_id), (slot, invite['token'], invite['bubble']))
            self.assertTrue(cfg.seed_verified and cfg.npc_combat and cfg.faction_missions)
            self.assertTrue(cfg.shot_capture and cfg.shot_claims and cfg.pilot_health)
            self.assertEqual(cfg.seed_content, 'ego_dlc_timelines:900')
            self.assertEqual(template.read_bytes(), before)
    def test_public_old_incomplete_and_unsafe_invites_are_rejected(self):
        a=self.configure()
        for key,value in [('channel','public'),('port',45842),('version','7.1.1'),('fire2',False),('world_sync',1),('world_mode','host'),('server','127.0.0.1'),('extra',True)]:
            with self.subTest(key=key),self.assertRaises(ValueError):launcher.validate_invite({**a,key:value})
        with self.assertRaises(ValueError):launcher.validate_invite({k:v for k,v in a.items() if k!='world_sync'})
    def test_configure_cannot_replace_installed_or_existing_session(self):
        self.configure();before=(self.host/'session.json').read_bytes()
        with patch.object(launcher,'ROOT',self.host),self.assertRaisesRegex(RuntimeError,'already has a session'):launcher.configure('host','192.0.2.202')
        self.assertEqual(before,(self.host/'session.json').read_bytes())
        other=self.package('installed','Linux');(other/'installation.json').write_text('{}')
        with patch.object(launcher,'ROOT',other),self.assertRaisesRegex(RuntimeError,'Remove the installed'):launcher.configure('host','192.0.2.109')
    def test_windows_backend_gets_loopback_only_for_host(self):
        self.configure()
        for folder,address in ((self.host,'127.0.0.1'),(self.guest,'192.0.2.109')):
            with patch.object(launcher,'ROOT',folder),patch.object(launcher,'ensure_closed'),patch.object(launcher.sys,'platform','win32'),patch.object(launcher.subprocess,'run') as run:
                launcher.platform_action('install','C:\\Games\\X4')
                argv=run.call_args.args[0];self.assertEqual(argv[argv.index('-ServerIP')+1],address)
                self.assertIn(str(folder/'setup_windows.ps1'),argv)
    def test_only_host_starts_world_relay_and_session_is_locked(self):
        self.configure()
        with patch.object(launcher,'ROOT',self.guest),self.assertRaisesRegex(RuntimeError,'Only the HOST'):launcher.server(console=True)
        with patch.object(launcher,'ROOT',self.host),patch('x4lan.monitor.probe',return_value=False),patch('x4lan.monitor.run') as run,patch.object(launcher,'backend_module'):
            launcher.server(console=True);cfg=run.call_args.args[0];self.assertTrue(cfg['world_sync']);self.assertTrue(cfg['fire2']);self.assertEqual(cfg['port'],45841)
            with launcher.session_lock():
                with self.assertRaisesRegex(RuntimeError,'running'):launcher.server(console=True)
    def test_desktop_server_opens_terminal_without_starting_in_background(self):
        with patch.object(launcher,'ROOT',self.host),patch.object(launcher.sys,'platform','linux'),patch.object(launcher.sys.stdout,'isatty',return_value=False),patch.object(launcher.shutil,'which',return_value='/usr/bin/konsole'),patch.object(launcher.subprocess,'Popen') as start,patch.object(launcher,'configured') as configured:
            launcher.server()
            argv=start.call_args.args[0]
            self.assertEqual(argv,['/usr/bin/konsole','--separate','-e','bash',str(self.host/'03-START-SERVER-HOST.sh'),'--console'])
            self.assertEqual(start.call_args.kwargs['cwd'],self.host)
            for name in ('stdin','stdout','stderr'):
                self.assertEqual(start.call_args.kwargs[name],launcher.subprocess.DEVNULL)
            configured.assert_not_called()
    def test_desktop_without_terminal_fails_instead_of_starting_hidden_server(self):
        with patch.object(launcher.sys,'platform','linux'),patch.object(launcher.sys.stdout,'isatty',return_value=False),patch.object(launcher.shutil,'which',return_value=None),patch('x4lan.monitor.run') as run:
            with self.assertRaisesRegex(RuntimeError,'No supported terminal'):launcher.server()
            run.assert_not_called()
    def test_second_open_attaches_while_server_holds_setup_lock(self):
        invite=self.configure();logs=self.host/'logs';logs.mkdir()
        status={k:invite[k] for k in ('build','bubble','port')};status['running']=True
        (logs/'server-status.json').write_text(json.dumps(status))
        with patch.object(launcher,'ROOT',self.host),patch('x4lan.monitor.probe',return_value=True),patch('x4lan.monitor.run') as run,patch('x4lan.monitor.attach') as attach:
            with launcher.session_lock():launcher.server(console=True)
            attach.assert_called_once_with(logs/'server-status.json');run.assert_not_called()
            for key,value in [('running',False),('build','old'),('bubble','other'),('port',45842)]:
                (logs/'server-status.json').write_text(json.dumps({**status,key:value}))
                with self.subTest(key=key),self.assertRaisesRegex(RuntimeError,'no matching live status'):launcher.server(console=True)
            attach.assert_called_once()
    def test_wrong_owned_installation_never_starts_or_removes(self):
        self.configure();game=self.root/'game';target=game/'extensions/x4_lan_lab';target.mkdir(parents=True)
        (target/'.x4lan-package-id').write_text('other-session')
        meta=json.loads((self.host/'package.json').read_text())
        (self.host/'installation.json').write_text(json.dumps(dict(game=str(game),profile=str(self.root/'45841'),package_id=meta['package_id'])))
        with patch.object(launcher,'ROOT',self.host),patch.object(launcher,'ensure_closed'),patch.object(launcher,'backend_module') as backend:
            for action in ('start','uninstall'):
                with self.assertRaisesRegex(RuntimeError,'another package'):launcher.platform_action(action)
            backend.assert_not_called()
    def test_diagnostics_redact_secret_and_leave_invitation_private(self):
        invite=self.configure();logs=self.host/'logs';logs.mkdir();(logs/'x4-debug.txt').write_text(invite['token'])
        with patch.object(launcher,'ROOT',self.host):launcher.diagnostics()
        self.assertNotIn(invite['token'],(logs/'x4-debug.txt').read_text());self.assertFalse((logs/'JOIN-SESSION.json').exists())
    def test_setup_retries_failed_settings_import_without_reinstalling_or_reimporting_success(self):
        self.configure();(self.host/'installation.json').write_text('{}')
        args=SimpleNamespace(server_only=False,game=None,settings_profile=None,keep_test_settings=False)
        backend=SimpleNamespace(owned_installation=lambda *_: ({'game':str(self.root/'game')},None))
        with patch.object(launcher,'ROOT',self.host),patch.object(launcher,'ensure_closed'),patch.object(launcher,'backend_module',return_value=backend),patch.object(launcher.sys.stdin,'isatty',return_value=False),patch.object(launcher,'platform_action') as install,patch.object(launcher,'import_settings') as settings:
            settings.side_effect=RuntimeError('invalid source input XML')
            with self.assertRaisesRegex(RuntimeError,'invalid source'):launcher.setup(args)
            settings.side_effect=lambda *_:(self.host/'settings-import.json').write_text('{}')
            launcher.setup(args);launcher.setup(args)
            self.assertEqual(settings.call_count,2);install.assert_not_called()
    def test_server_only_setup_does_not_import_player_preferences(self):
        self.configure()
        with patch.object(launcher,'ROOT',self.host),patch.object(launcher,'import_settings') as settings,patch.object(launcher,'platform_action') as install:
            launcher.setup(SimpleNamespace(server_only=True,game=None))
            settings.assert_not_called();install.assert_not_called()

    def test_windows_invitation_dialog_returns_path_as_data(self):
        path = 'C:\\Users\\Test Name\\Downloads\\JOIN-SESSION.json'
        with patch.object(launcher.sys, 'platform', 'win32'), patch.object(launcher.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout=path+'\n', stderr='')) as run:
            self.assertEqual(launcher.choose_invitation(), path)
            argv = run.call_args.args[0]
            self.assertEqual(argv[:4], ['powershell.exe', '-NoProfile', '-STA', '-Command'])
            self.assertNotIn(path, argv[-1])
            self.assertFalse(run.call_args.kwargs.get('shell', False))

    def test_cancelled_invitation_does_not_prompt_or_configure(self):
        with patch.object(launcher.sys, 'platform', 'win32'), patch.object(launcher.subprocess, 'run', return_value=SimpleNamespace(returncode=2, stdout='', stderr='')), patch('builtins.input') as ask:
            with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                launcher.choose_invitation()
            ask.assert_not_called()

    def test_unavailable_dialog_falls_back_to_quoted_terminal_path(self):
        with patch.object(launcher.sys, 'platform', 'win32'), patch.object(launcher.subprocess, 'run', side_effect=FileNotFoundError), patch('builtins.input', return_value='"C:/Folder With Spaces/JOIN-SESSION.json"'):
            self.assertEqual(launcher.choose_invitation(), 'C:/Folder With Spaces/JOIN-SESSION.json')


class InternalPackageRuntime(unittest.TestCase):
    def test_both_built_servers_import_and_construct_session_outside_checkout(self):
        from distribution import build_packages
        with tempfile.TemporaryDirectory() as d:
            for platform in ('Linux','Windows11'):
                folder=Path(d)/platform;folder.mkdir();build_packages.copy_server_runtime(folder)
                with self.subTest(platform=folder.name):
                    code="import sys;sys.path.insert(0,sys.argv[1]);from x4lan.monitor import run;from x4lan.universe import UniverseSession;s=UniverseSession('a'*32,'world1-package-check','bubble');print(type(s).__name__)"
                    result=subprocess.run([sys.executable,'-I','-B','-c',code,str(folder)],cwd=d,text=True,capture_output=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(result.stdout.strip(),'UniverseSession')

    def test_dedicated_roles_give_every_client_slot_a_windows_and_a_linux_package(self):
        from distribution import build_packages
        roles = build_packages.dedicated_roles(8)
        self.assertEqual(roles[0], ('Linux', 'host', '45841'))
        clients = roles[1:]
        self.assertEqual(len(clients), 14)
        for k, role in enumerate(['guest'] + ['guest%d' % k for k in range(2, 8)], start=2):
            profile = '45841' if k == 2 else str(45839 + k)
            self.assertEqual([r for r in clients if r[1] == role],
                             [('Windows11', role, profile), ('Linux', role, profile)])
            self.assertEqual(launcher.role_slot(role), 'p%d' % k)
        names = [build_packages.package_name('7.6.0', *r[:2]) for r in roles]
        self.assertEqual(len(set(names)), len(names))
        self.assertEqual(names[:5], ['X4-LAN-7.6.0-Linux', 'X4-LAN-7.6.0-Windows11', 'X4-LAN-7.6.0-Linux-Client',
                                     'X4-LAN-7.6.0-Windows11-Guest2', 'X4-LAN-7.6.0-Linux-Client2'])
        self.assertEqual(names[-1], 'X4-LAN-7.6.0-Linux-Client7')

    def test_built_dedicated_set_has_matched_linux_clients_without_session_secrets(self):
        import shutil, tarfile
        from distribution import build_packages
        with tempfile.TemporaryDirectory() as temp:
            # Fixture reports and Rust outputs only; this is no release validation.
            source = Path(temp) / 'source'; source.mkdir()
            for name in ('VERSION.txt', 'docs/VERSIONING.txt', 'docs/DEDICATED-SERVER.md', 'src/extension', 'src/x4lan'):
                if (ROOT / name).is_dir():
                    shutil.copytree(ROOT / name, source / name, ignore=shutil.ignore_patterns('__pycache__'))
                else:
                    (source / name).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / name, source / name)
            release = build_packages.version()
            (source / 'reports').mkdir()
            for name, suffix in (('automated', 'tests'), ('release', 'checks')):
                (source / 'reports' / f'{name}-{release}-{suffix}.txt').write_text('Unit-test fixture only.\n')
            rust = source / 'src/rust/target/release'; rust.mkdir(parents=True)
            (rust / 'libx4lan_net.so').write_bytes(b'\x7fELF rust x4lan-net fixture')
            (rust / 'x4lan-dedicated').write_bytes(b'\x7fELF supervisor fixture')
            out = Path(temp) / 'set'
            diet = Path(temp) / 'framediet-fixture.dll'; diet.write_bytes(b'frame diet module fixture')
            with patch.object(build_packages, 'SOURCE', source), patch.object(build_packages.subprocess, 'run') as cargo,                     patch.object(build_packages, 'build_framediet', return_value=diet):
                folders = build_packages.build(out, dedicated=True, max_players=3)
            self.assertEqual(cargo.call_count, 2)  # One Rust build for the whole set.
            for name in ('Windows11', 'Windows11-Guest2'):  # Windows clients carry the frame-diet/RNG-split module
                native = out / f'X4-LAN-{release}-{name}' / 'extension/x4_lan_lab/native'
                self.assertEqual((native / 'x4lan_framediet_64.dll').read_bytes(), diet.read_bytes())
            self.assertEqual([f.name.split(release + '-')[1] for f in folders],
                             ['Linux', 'Windows11', 'Linux-Client', 'Windows11-Guest2', 'Linux-Client2'])
            metas = {f.name: json.loads((f / 'package.json').read_text()) for f in folders}
            self.assertEqual(len({m['pair_id'] for m in metas.values()}), 1)
            for meta in metas.values():
                self.assertEqual(meta['roles'], ['host', 'guest', 'guest2'])
                self.assertEqual(meta['max_players'], 3)
            listed = [line.split('  ', 1)[1] for line in (out / 'PACKAGES-SHA256.txt').read_text().splitlines()]
            self.assertEqual(sorted(listed), sorted(f.name + ('.zip' if 'Windows' in f.name else '.tar.xz') for f in folders))
            from distribution.world_launcher import content_digest
            digests = {content_digest(f / 'extension/x4_lan_lab') for f in folders}
            self.assertEqual(len(digests), 1)  # Same mod hash: any client package joins the same world.
            for name, role, profile in (('Linux-Client', 'guest', '45841'), ('Linux-Client2', 'guest2', '45842')):
                folder = out / f'X4-LAN-{release}-{name}'
                meta = metas[folder.name]
                self.assertEqual((meta['platform'], meta['role'], meta['test_profile'], meta['dedicated']),
                                 ('Linux', role, profile, True))
                self.assertIsNone(meta['slot']); self.assertIsNone(meta['package_id'])
                native = folder / 'extension/x4_lan_lab/native'
                self.assertEqual(sorted(p.name for p in native.iterdir()), ['x4lan_net.so'])
                self.assertEqual((native / 'x4lan_net.so').read_bytes(), (rust / 'libx4lan_net.so').read_bytes())
                self.assertFalse((folder / 'x4lan-dedicated').exists())
                self.assertTrue((folder / 'setup_linux.py').is_file())
                self.assertFalse((folder / 'setup_windows.ps1').exists())
                self.assertTrue((folder / 'DEDICATED-SERVER.md').is_file())
                for title in ('01-INSTALL', '04-TEST-CONNECTION', '05-START-X4', '09-REMOVE-MOD'):
                    self.assertTrue((folder / (title + '.sh')).is_file())
                cfg = (folder / 'extension/x4_lan_lab/ui/config.lua').read_text()
                self.assertIn('enabled = false', cfg); self.assertNotIn('token', cfg)
                with tarfile.open(out / (folder.name + '.tar.xz')) as tar:
                    members = {m.name[len(folder.name) + 1:] for m in tar.getmembers()}
                for secret in ('session.json', 'JOIN-SESSION.json', 'installation.json'):
                    self.assertNotIn(secret, members)
                self.assertIn('extension/x4_lan_lab/native/x4lan_net.so', members)
                self.assertIn('setup_linux.py', members)
            server = out / f'X4-LAN-{release}-Linux'
            self.assertTrue((server / 'x4lan-dedicated').is_file())
            self.assertEqual((server / 'extension/x4_lan_lab/native/x4lan_net.so').read_bytes(),
                             (rust / 'libx4lan_net.so').read_bytes())
            windows = out / f'X4-LAN-{release}-Windows11-Guest2/extension/x4_lan_lab/native'
            self.assertEqual(sorted(p.name for p in windows.iterdir()), ['x4lan_framediet_64.dll', 'x4lan_net_64.dll'])

    def test_linux_client_package_configures_as_client_only(self):
        from distribution import build_packages
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folders = {}
            for platform, role, profile in build_packages.dedicated_roles(3):
                folder = root / build_packages.package_name(launcher.VERSION, platform, role)
                (folder / 'extension/x4_lan_lab/ui').mkdir(parents=True)
                (folder / 'package.json').write_text(json.dumps(dict(
                    channel='internal', version=launcher.VERSION, platform=platform, slot=None, package_id=None,
                    steam_build='23660954', dedicated=True, role=role, max_players=3, test_profile=profile)))
                folders[folder.name.split(launcher.VERSION + '-')[1]] = folder
            client = folders['Linux-Client2']
            with patch.object(launcher, 'ROOT', client), self.assertRaisesRegex(RuntimeError, 'dedicated client'):
                launcher.configure('host', '192.0.2.109')
            self.assertFalse((client / 'session.json').exists())
            self.assertEqual(launcher.dedicated_setup_role(json.loads((client / 'package.json').read_text())), 'guest')
            self.assertEqual(launcher.dedicated_setup_role(json.loads((folders['Linux'] / 'package.json').read_text())), 'host')
            with patch.object(launcher, 'ROOT', folders['Linux']):
                launcher.configure('host', '192.0.2.109')
            with patch.object(launcher, 'ROOT', client):
                launcher.configure('guest', invite=folders['Linux'] / 'JOIN-SESSION.json')
                meta, session = launcher.configured()
            self.assertEqual((meta['slot'], session['host']), ('p3', '192.0.2.109'))
            lua = (client / 'extension/x4_lan_lab/ui/config.lua').read_text()
            for text in ('slot = "p3"', 'test_profile = "45842"', 'dedicated = false', 'max_players = 3'):
                self.assertIn(text, lua)
            self.assertNotIn('dedicated_control_path', lua)
            self.assertFalse((client / 'JOIN-SESSION.json').exists())
