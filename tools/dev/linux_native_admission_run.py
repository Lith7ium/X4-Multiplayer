"""Bounded, isolated Linux X4 clock-probe launcher; never manages deployed services.

Run as root in the explicitly staged private test directory. The game runs as
x4server, with read-only host files, a fresh profile and only DLC/autostart mods.
Existing same-user Steam is required and is never launched, stopped or copied.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import shutil
import signal
import socket
import subprocess
import time

EXE_SHA='113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
AGENT_SHA='39372938fcac09c3061262c8b50682f655fe9eb1025416238e2319673c9ed25b'
PROBE_SHA='2fe32df09e35454d70e97a7ecdfa787a2f5293dc99029e36d5121e24581ddef7'
OBSERVER_AGENT_SHA='dba2adf1025e0247c0bdffd8802b5d0a3ab85f3c237b778a30b860f14494aebf'
OBSERVER_PROBE_SHA='bd01f6e548226be48e6950348183acaa9ef64c3939375b990105a5f68d386428'
PORTABLE_SOURCE_SHA='34e63d30be33d2d6e23701cf7ed7e0daedc8f8ddf8c00e6a2d55d861630e5794'
OBSERVER_ADAPTER_SHA='729da3ad73c807bfd2e5c5f0279e3b4b4f0bbb0e91d0476b0498cd5223b839b5'
OBSERVER_BRIDGE_SHA='ee841df1e9a9291ec47a584c4d31d97c2a0319f31faa4a92b17bf77cd6ecb87c'
PORTABLE_CONTRACT_SHA='f66b841dbef811d245eb584a0586401ef80361847ad17d941dfeb3cd5dc0592b'


def observer_bundle(root,game):
    """Validate the reviewed fixture, complete stock bytes and dependency code."""
    source=root/'bin/native_portable.py'
    if sha(source)!=PORTABLE_SOURCE_SHA:raise RuntimeError('portable dependency changed')
    from native_portable import canonical_json,validate_contract
    bundle=root/'portable-baseline-001';contract=json.loads((bundle/'contract.json').read_text())
    if validate_contract(contract)!=PORTABLE_CONTRACT_SHA:raise RuntimeError('portable fixture contract changed')
    if sha(bundle/'save_009.xml.gz')!=contract['snapshot_sha256']:raise RuntimeError('owned snapshot bytes differ')
    if sha(root/'bin/native_portable_linux.lua')!=OBSERVER_ADAPTER_SHA:raise RuntimeError('observer adapter changed')
    if sha(root/'bin/x4mp_load_probe.so')!=OBSERVER_BRIDGE_SHA:raise RuntimeError('observer Lua bridge changed or not reviewed')
    for name,expected in contract['fixture_files'].items():
        if sha(bundle/'fixture'/name)!=expected:raise RuntimeError('common fixture bytes differ')
    stock=json.loads((bundle/'stock.json').read_text());claimed=stock.pop('manifest_sha256')
    if hashlib.sha256(canonical_json(stock)).hexdigest()!=claimed or claimed!=contract['stock_sha256'] or stock.get('payloads_hashed') is not True:
        raise RuntimeError('unverified stock content manifest')
    if len(stock['files'])!=127 or len(stock['dlc'])!=7:raise RuntimeError('stock coverage differs')
    stamps={}
    for name,row in stock['files'].items():
        path=game/name
        if not path.resolve().is_relative_to(game):raise RuntimeError('stock path outside game')
        before=path.stat()
        if before.st_size!=row['bytes'] or sha(path)!=row['sha256']:raise RuntimeError('stock payload differs')
        after=path.stat()
        stamp=(before.st_ino,before.st_size,before.st_mtime_ns)
        if stamp!=(after.st_ino,after.st_size,after.st_mtime_ns):raise RuntimeError('stock changed while hashing')
        stamps[name]=stamp
    return bundle,contract,stamps


def stage_observer_fixture(case,profile,root,bundle,contract):
    """Only the new private case is written; no autostart or gameplay trigger."""
    boot=case/'extensions/x4_lan_lab';shutil.copytree(bundle/'fixture',boot)
    shutil.copyfile(root/'bin/native_portable_linux.lua',boot/'ui/native_portable_linux.lua')
    (boot/'native').mkdir()
    shutil.copyfile(root/'bin/x4mp_load_probe.so',boot/'native/x4mp_load_probe.so')
    if sha(boot/'native/x4mp_load_probe.so')!=sha(root/'bin/x4mp_load_probe.so'):
        raise RuntimeError('observer Lua bridge staging changed bytes')
    shutil.copyfile(bundle/'save_009.xml.gz',profile/'save/save_009.xml.gz')
    if sha(profile/'save/save_009.xml.gz')!=contract['snapshot_sha256']:raise RuntimeError('snapshot staging changed bytes')
    (boot/'content.xml').write_text('<?xml version="1.0"?><content id="x4_lan_lab" name="Owned portable snapshot probe" version="11" save="false"><dependency version="900" /></content>\n')
    (boot/'ui.xml').write_text('<?xml version="1.0"?><addon name="x4_lan_lab"><environment type="menus"><dependency name="ego_detailmonitor" /><file name="ui/settings.lua" /><file name="ui/native_portable_linux.lua" /><file name="ui/native_portable_loader.lua" /></environment></addon>\n')
    values={'enabled':True,'mode':'snapshot-import-observer-v1','profile':'45841','run':case.name,
            'save_path':str(profile/'save'),'snapshot_sha256':contract['snapshot_sha256'],
            'contract_sha256':PORTABLE_CONTRACT_SHA,'schema_sha256':contract['schema_sha256']}
    (boot/'ui/settings.lua').write_text('X4MP_PORTABLE_CONFIG={'+','.join(
        key+'='+('true' if value is True else json.dumps(value)) for key,value in values.items())+'}\n')
    return boot


def observer_environment(env,token,ready_file):
    result={key:value for key,value in env.items() if not key.startswith(('X4MP_ADMISSION','X4MP_SCHED'))}
    result.update(X4MP_LOAD_OBSERVER='snapshot-import-observer-v1',X4MP_LOAD_TOKEN=token,
                  X4MP_LOAD_READY_FILE=str(ready_file),X4MP_AGENT_SETTLE_MS='2000')
    return result


def summarize_probe(result,observer):
    probe=result.get('probe',{})
    result['loaded_world_verified']=probe.get('loaded_snapshot_verified' if observer else 'world_ready_before_first_report') is True
    if observer:
        # A valid LOADED marker is useful failed-run evidence. It is not the
        # complete import/restoration acceptance represented by verified above.
        result['loaded_world_marker_observed']=isinstance(probe.get('markers',{}).get('LOADED'),dict)
    if not result['loaded_world_verified']:
        result['error']=probe.get('error') or 'required owned loaded-world evidence missing'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def memory_available_bytes():
    """Best-effort memory telemetry; it never determines native admission."""
    try:
        for line in Path('/proc/meminfo').read_text().splitlines():
            if line.startswith('MemAvailable:'):
                value = int(line.split()[1]) * 1024
                return value if value >= 0 else None
    except (OSError, ValueError, IndexError):
        pass
    return None


def start_ticks(pid):return int(Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[19])


def processes():
    result={}
    for path in Path('/proc').glob('[0-9]*'):
        try:
            stat=(path/'stat').read_text().rsplit(')',1)[1].split()
            result[int(path.name)]={'parent':int(stat[1]),'start':int(stat[19]),
                'uid':path.stat().st_uid,'name':(path/'comm').read_text().strip()}
        except (OSError,ValueError):pass
    return result


def descendants(root,table):
    owned={root}
    while True:
        added={pid for pid,row in table.items() if row['parent'] in owned}-owned
        if not added:return owned
        owned.update(added)


def atomic_json(path,value):
    temp=path.with_suffix('.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)


def bounded_text(path):
    try:
        with path.open('rb') as stream:return stream.read(65536).decode(errors='replace')
    except FileNotFoundError:return ''


def steam_identity(userhome,uid):
    pid=int((userhome/'.steam/steam.pid').read_text().strip())
    row=processes().get(pid)
    if not row or row['uid']!=uid or row['name']!='steam':raise RuntimeError('owned existing Steam client unavailable')
    return {'pid':pid,'start':row['start'],'uid':uid}


def stop_group(proc,identity):
    if proc is None or proc.poll() is not None:return
    if start_ticks(proc.pid)!=identity or os.getpgid(proc.pid)!=proc.pid:raise RuntimeError('owned process group identity changed')
    os.killpg(proc.pid,signal.SIGTERM)
    try:proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        if start_ticks(proc.pid)!=identity:raise RuntimeError('process changed before forced cleanup')
        os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=8)


def stop_pidfd(fd):
    """Terminate only the pinned process, independently of launcher-parent life."""
    poll=select.poll();poll.register(fd,select.POLLIN)
    if not poll.poll(0):
        try:signal.pidfd_send_signal(fd,signal.SIGTERM)
        except ProcessLookupError:pass
    if not poll.poll(2000):
        try:signal.pidfd_send_signal(fd,signal.SIGKILL)
        except ProcessLookupError:pass
    if not poll.poll(5000):raise RuntimeError('owned PIDFD did not report process exit')


def namespace_command(case,game,dlc_binds,lock,env,program):
    """Build the same namespace for native launches and nongame preflight.

    Resolve the real game directory before shadowing its extension directory.
    DLC source binds must precede that shadow and remain outside both views.
    """
    game=game.resolve(strict=True)
    command=['runuser','-u','x4server','--','bwrap','--ro-bind','/','/','--proc','/proc','--dev-bind','/dev','/dev',
             '--bind',str(case),str(case),'--ro-bind',str(game),str(case/'game')]
    for source,view in dlc_binds:command+=['--ro-bind',str(source),str(view)]
    command+=['--ro-bind',str(case/'extensions'),str(case/'game/extensions'),
             '--ro-bind',str(case/'extensions'),str(game/'extensions'),'--bind',str(lock),'/tmp/EGOSOFT_X4.lock',
             '--die-with-parent','--chdir',str(case/'game'),'--clearenv']
    for key,value in env.items():command+=['--setenv',key,value]
    return command+['--',*program]


def main():
    import pwd
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--scenario',choices=('hold-noop','nonzero','timeout','disconnect','snapshot-observer'),required=True)
    args=parser.parse_args()
    if os.geteuid()!=0:raise RuntimeError('root required only for process evidence and owned cleanup')
    user=pwd.getpwnam('x4server');userhome=Path(user.pw_dir)
    root=args.root.resolve(strict=True)
    if root.parent!=userhome or not root.name.startswith('x4mp-linux-admission-') or not (root/'.x4mp-admission-test-root').is_file():
        raise RuntimeError('not a separately owned admission test root')
    observer=args.scenario=='snapshot-observer'
    game=(userhome/'x4/game').resolve(strict=True)
    agent=root/('bin/libx4mp_load_observer.so' if observer else 'bin/libx4mp_agent.so')
    probe=root/('bin/linux_load_observer_probe.py' if observer else 'bin/linux_admission_probe.py')
    agent_sha=OBSERVER_AGENT_SHA if observer else AGENT_SHA;probe_sha=OBSERVER_PROBE_SHA if observer else PROBE_SHA
    if sha(game/'X4')!=EXE_SHA or sha(agent)!=agent_sha or sha(probe)!=probe_sha:raise RuntimeError('pinned staged artifact mismatch')
    if any(row['name']=='X4' for row in processes().values()):raise RuntimeError('another Linux X4 is running')
    available=memory_available_bytes()
    steam=steam_identity(userhome,user.pw_uid)
    bundle=contract=None;stock_stamps={}
    if observer:bundle,contract,stock_stamps=observer_bundle(root,game)
    case=root/'cases'/(args.scenario+'-'+secrets.token_hex(4));case.mkdir(parents=True,exist_ok=False)
    profile=case/'home/.config/EgoSoft/X4/45841'
    for path in (profile/'save',case/'tmp',case/'cache',case/'data',case/'runtime',case/'game',case/'extensions',case/'home/.steam'):
        path.mkdir(parents=True,exist_ok=True)
    (case/'runtime').chmod(0o700)
    (profile/'config.xml').write_text('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode><fullscreen>false</fullscreen><borderless>false</borderless><res_width>800</res_width><res_height>600</res_height><antialiasing>0</antialiasing><vsync>false</vsync></root>\n')
    for name in ('root','steam.pid','sdk32','sdk64'):
        source=userhome/'.steam'/name
        if source.exists():(case/'home/.steam'/name).symlink_to(source)
    (case/'home/.steam/steam').symlink_to('root')
    dlc_binds=[]
    for extension in (game/'extensions').glob('ego_dlc_*'):
        if extension.is_dir():
            view=case/'dlc-source'/extension.name;view.mkdir(parents=True)
            dlc_binds.append((extension.resolve(strict=True),view))
            (case/'extensions'/extension.name).symlink_to(view)
    if observer:boot=stage_observer_fixture(case,profile,root,bundle,contract)
    else:
        boot=case/'extensions/x4lane_autostart';shutil.copytree(root/'autostart',boot)
    extension_ids=sorted(path.name for path in (case/'extensions').iterdir())
    if any(not (name.startswith('ego_dlc_') or name==('x4_lan_lab' if observer else 'x4lane_autostart')) for name in extension_ids):
        raise RuntimeError('unexpected extension in isolated view')
    (profile/'content.xml').write_text('<?xml version="1.0"?><content>'+''.join(
        '<extension id="'+name+'" enabled="true" />' for name in extension_ids)+'</content>\n')
    if not observer:
        (boot/'ui/settings.lua').write_text('X4LANE_AUTOSTART={enabled=true,lane="linux-probe",run="'+case.name+'",gamestart="custom_creative",start="menu",menu_delay_s=0.25,menu_deadline_s=120,quit_after_s=600,ready_gametime_s=2,alive_s=5,mute=true}\n')
    lock=case/'x4.lock';lock.touch()
    for path in [case,*case.rglob('*')]:
        if not path.is_symlink():os.chown(path,user.pw_uid,user.pw_gid)
    case.chmod(0o700)
    display=next((n for n in range(101,120) if not Path(f'/tmp/.X{n}-lock').exists() and not Path(f'/tmp/.X11-unix/X{n}').exists()),None)
    if display is None:raise RuntimeError('no unused private Xvfb display')
    auth=case/'xauthority';auth.touch(mode=0o600);os.chown(auth,user.pw_uid,user.pw_gid)
    subprocess.run(['runuser','-u','x4server','--','xauth','-f',str(auth),'add',f':{display}','.',secrets.token_hex(16)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reserve:
        reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
    token=secrets.token_hex(16);claim=case/'claim.json';agentlog=case/'agent.log';disarm=case/'disarm';ready_file=case/'ready-request'
    env={'PATH':'/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin','HOME':str(case/'home'),
         'USER':'x4server','LOGNAME':'x4server','DISPLAY':f':{display}','XAUTHORITY':str(auth),
         'XDG_CONFIG_HOME':str(case/'home/.config'),'XDG_CACHE_HOME':str(case/'cache'),
         'XDG_DATA_HOME':str(case/'data'),'XDG_RUNTIME_DIR':str(case/'runtime'),'TMPDIR':str(case/'tmp'),
         'SteamAppId':'392160','SteamGameId':'392160','LD_LIBRARY_PATH':str(case/'game/lib'),
         'VK_ICD_FILENAMES':'/usr/share/vulkan/icd.d/lvp_icd.json','VK_DRIVER_FILES':'/usr/share/vulkan/icd.d/lvp_icd.json',
         'SDL_AUDIODRIVER':'dummy','ALSOFT_DRIVERS':'null','LD_PRELOAD':str(agent),
         'X4MP_AGENT':'1','X4MP_AGENT_LOG':str(agentlog),'X4MP_AGENT_CADENCE_MS':'100','X4MP_AGENT_SETTLE_MS':'10000',
         'X4MP_AGENT_DISARM_FILE':str(disarm),'X4MP_SCHED':'fixed60','X4MP_ADMISSION':'clock-noop-v1',
         'X4MP_ADMISSION_READY_FILE':str(ready_file),
         'X4MP_ADMISSION_PORT':str(port),'X4MP_ADMISSION_SLOT':'1','X4MP_ADMISSION_TOKEN':token,
         'X4MP_ADMISSION_TIMEOUT_MS':'5000','X4MP_ADMISSION_TICKS':'12'}
    if observer:
        env=observer_environment(env,token,ready_file)
    command=namespace_command(case,game,dlc_binds,lock,env,[str(case/'game/X4'),'-personalfolderid','45841','-skipintro','-debug','scripts','-logfile','native-probe.log','-nocputhrottle','-noworkshopsync','-seed:276944566'])
    xvfb=gameproc=coordinator=None;gamepidfd=xvfbpidfd=None;gamepid=None;starts={};logs=[]
    result={'scenario':args.scenario,'case':str(case),'steam_before':steam,'native_linux_game':True,
            'memory_available_before_bytes':available,
            'synthetic_coordinator':not observer,'read_only_load_observer':observer,'cross_platform_gameplay':False,'agent_sha256':agent_sha,
            'probe_sha256':probe_sha,'launcher_sha256':sha(Path(__file__)),'game_sha256':EXE_SHA}
    if observer:result.update(contract_sha256=PORTABLE_CONTRACT_SHA,stock_files_verified=len(stock_stamps),simulation_admission=False)
    cancelled=False
    def cancel(_number,_frame):
        nonlocal cancelled
        cancelled=True
    for number in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):signal.signal(number,cancel)
    def check_cancel():
        if cancelled:raise RuntimeError('owned native probe cancelled')
    def launch(label,cmd,process_env=None,target_user=False):
        log=(case/(label+'.log')).open('wb');logs.append(log)
        user_args={'user':user.pw_uid,'group':user.pw_gid,'extra_groups':[]} if target_user else {}
        parent_pid=os.getpid()
        def parent_death():
            if ctypes.CDLL(None).prctl(1,signal.SIGKILL)!=0 or os.getppid()!=parent_pid:os._exit(125)
        proc=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True,env=process_env,preexec_fn=parent_death,**user_args)
        starts[proc.pid]=start_ticks(proc.pid);return proc
    try:
        if any(row['name']=='X4' for row in processes().values()) or steam_identity(userhome,user.pw_uid)!=steam:
            raise RuntimeError('native admission or Steam changed during preflight')
        xvfb=launch('xvfb',['/usr/bin/Xvfb',f':{display}','-screen','0','800x600x24','-nolisten','tcp','-noreset','-auth',str(auth)],target_user=True)
        xvfbpidfd=os.pidfd_open(xvfb.pid)
        if Path(f'/proc/{xvfb.pid}/exe').resolve()!=Path('/usr/bin/Xvfb').resolve() or processes()[xvfb.pid]['uid']!=user.pw_uid:
            raise RuntimeError('private Xvfb executable or UID differs')
        result['private_xvfb_identity']={'pid':xvfb.pid,'start_ticks':starts[xvfb.pid],'uid':user.pw_uid}
        deadline=time.monotonic()+10
        while not Path(f'/tmp/.X11-unix/X{display}').exists():
            check_cancel()
            if xvfb.poll() is not None or time.monotonic()>deadline:raise RuntimeError('private Xvfb failed')
            time.sleep(.1)
        penv=os.environ.copy();penv['X4MP_LOAD_TOKEN' if observer else 'X4MP_ADMISSION_TOKEN']=token
        probe_command=['python3',str(probe),'--claim',str(claim),'--agent-so',str(agent),'--agent-log',str(agentlog),'--world-log',str(profile/'native-probe.log'),'--ready-file',str(ready_file),'--run-id',case.name,'--report',str(case/'probe.json'),'--startup-timeout','900' if observer else '600']
        probe_command+=['--contract',str(bundle/'contract.json'),'--disarm-file',str(disarm)] if observer else ['--port',str(port),'--scenario',args.scenario]
        coordinator=launch('coordinator',probe_command,penv)
        deadline=time.monotonic()+10
        while 'READY' not in bounded_text(case/'coordinator.log'):
            check_cancel()
            if coordinator.poll() is not None or time.monotonic()>deadline:raise RuntimeError('synthetic coordinator failed before readiness')
            time.sleep(.05)
        gameproc=launch('game',command)
        deadline=time.monotonic()+45
        while gamepid is None:
            check_cancel()
            table=processes();owned=descendants(gameproc.pid,table)
            matches=[pid for pid in owned if table.get(pid,{}).get('name')=='X4' and table[pid]['uid']==user.pw_uid]
            if len(matches)>1:raise RuntimeError('ambiguous owned native process')
            if matches:
                pid=matches[0]
                if str(agent) in Path(f'/proc/{pid}/maps').read_text():
                    candidate_fd=os.pidfd_open(pid)
                    if start_ticks(pid)!=table[pid]['start']:
                        os.close(candidate_fd)
                        raise RuntimeError('owned native identity changed before PIDFD claim')
                    gamepid=pid;gamepidfd=candidate_fd
                    result['native_pid']=pid;result['native_start_ticks']=table[pid]['start']
                    views={}
                    for view in (case/'game/extensions',game/'extensions'):
                        inside=Path(f'/proc/{pid}/root')/str(view).lstrip('/')
                        views[str(view)]=sorted(path.name for path in inside.iterdir())
                        if views[str(view)]!=extension_ids:raise RuntimeError('native extension view differs from private allowlist')
                    result['extension_views']=views
                    result['native_executable_path']=os.readlink(f'/proc/{pid}/exe')
                    result['native_working_directory']=os.readlink(f'/proc/{pid}/cwd')
                    atomic_json(claim,{'pid':pid,'start_ticks':table[pid]['start'],'uid':user.pw_uid})
                    break
            if gameproc.poll() is not None or time.monotonic()>deadline:raise RuntimeError('exact agent did not map in owned native process')
            time.sleep(.02)
        print('OWNED_NATIVE_READY '+json.dumps({'case':str(case),'pid':gamepid,'scenario':args.scenario}),flush=True)
        deadline=time.monotonic()+(940 if observer else 640)
        while coordinator.poll() is None:
            check_cancel()
            if time.monotonic()>deadline:raise RuntimeError('bounded native probe timeout')
            if gameproc.poll() is not None:raise RuntimeError('native process exited during probe')
            if steam_identity(userhome,user.pw_uid)!=steam:raise RuntimeError('existing Steam identity changed')
            time.sleep(.25)
        result['probe_exit_code']=coordinator.returncode
        if (case/'probe.json').exists():result['probe']=json.loads((case/'probe.json').read_text())
        summarize_probe(result,observer)
        if observer:
            result['stock_stamps_unchanged']=all((stat.st_ino,stat.st_size,stat.st_mtime_ns)==stamp for name,stamp in stock_stamps.items() for stat in [(game/name).stat()])
            if not result['stock_stamps_unchanged']:raise RuntimeError('stock files changed during owned load')
    except Exception as error:
        result['error']=str(error)
    finally:
        disarm.touch()
        time.sleep(1)
        for fd,label in ((gamepidfd,'native'),(xvfbpidfd,'xvfb')):
            if fd is not None:
                try:stop_pidfd(fd);result[label+'_pidfd_exited']=True
                except Exception as error:result.setdefault('cleanup_errors',[]).append(str(error))
                finally:os.close(fd)
        for proc in (gameproc,coordinator,xvfb):
            if proc is not None:
                try:stop_group(proc,starts[proc.pid])
                except Exception as error:result.setdefault('cleanup_errors',[]).append(str(error))
        for log in logs:log.close()
        try:result['steam_after']=steam_identity(userhome,user.pw_uid)
        except Exception as error:result['steam_after']={'error':str(error)}
        result['steam_identity_unchanged']=result['steam_after']==steam
        final_processes=processes()
        result['owned_native_exited']=gamepid is None or gamepid not in final_processes or final_processes[gamepid]['start']!=result['native_start_ticks']
        result['coordinator_exited']=coordinator is None or coordinator.poll() is not None
        result['private_xvfb_exited']=xvfb is None or result.get('xvfb_pidfd_exited') is True
        atomic_json(case/'launch-result.json',result)
        print('NATIVE_PROBE_FINISHED '+json.dumps({'case':str(case),'probe_exit_code':result.get('probe_exit_code'),'error':result.get('error'),'owned_native_exited':result['owned_native_exited'],'steam_identity_unchanged':result['steam_identity_unchanged']}),flush=True)
    raise SystemExit(0 if result.get('probe_exit_code')==0 and result.get('loaded_world_verified') and not result.get('error') and not result.get('cleanup_errors') and result['owned_native_exited'] and result['private_xvfb_exited'] and result['steam_identity_unchanged'] else 1)


if __name__=='__main__':main()
