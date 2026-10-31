"""Build/test the standalone original C arithmetic port; no game or agent.

All raw binary32 outputs are compared with the frozen Python arithmetic contract.
Run check_portable_rotation.py separately for the independent Decimal/Fraction
oracle. A supplied private census benchmarks actual saved angle texts; only its
hash, counts and timings appear in the report, never its rows or saved IDs.
"""
import argparse
import ctypes
from concurrent.futures import ThreadPoolExecutor
from decimal import Decimal, localcontext
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import subprocess
import threading
import time

import check_portable_rotation as independent
import portable_rotation_reference as reference

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/native/engine'
U32=ctypes.c_uint32
SENTINEL=0xdeadbeef


class Attribute(ctypes.Structure):
    _fields_=[('text',ctypes.c_char_p),('length',ctypes.c_size_t),('present',ctypes.c_uint)]


def bind(path):
    lib=ctypes.CDLL(str(path.resolve()))
    lib.x4rot_decimal.argtypes=[ctypes.c_char_p,ctypes.c_size_t,ctypes.POINTER(U32)]
    lib.x4rot_add.argtypes=lib.x4rot_multiply.argtypes=[U32,U32,ctypes.POINTER(U32)]
    lib.x4rot_sincos.argtypes=[U32,ctypes.POINTER(U32)]
    lib.x4rot_basis.argtypes=[ctypes.POINTER(U32),ctypes.POINTER(U32)]
    lib.x4rot_offset.argtypes=[ctypes.c_uint,ctypes.POINTER(Attribute),ctypes.POINTER(U32)]
    lib.x4rot_half_pi_words.argtypes=[ctypes.POINTER(U32)]
    lib.x4rot_half_pi_words.restype=None
    lib.x4rot_test_offset_batch.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(Attribute),ctypes.c_size_t,ctypes.POINTER(ctypes.c_int),ctypes.POINTER(U32)]
    return lib


def binary_call(lib,name,words,length):
    out=(U32*length)(*[SENTINEL]*length)
    result=getattr(lib,name)(*words,out)
    if result:
        assert list(out)==[SENTINEL]*length,(name,'partial failure output')
    return result,list(out)


def text_rows(census):
    if census is None:
        return [],None
    data=json.loads(census.read_text())
    assert data['snapshot_sha256']=='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
    assert len(data['rows'])==10008 and not data['duplicate_xml_attributes']
    rows=[]
    for row in data['rows']:
        for bank in ('read','write'):
            values=row['banks'][bank];assert len(values)==1
            item=values[0]
            if item['kind']=='missing_orientation':
                rows.append((0,[None]*3))
            else:
                assert item['kind']=='euler' and item['rotation_count']==1 and not item['quaternion_count']
                assert not item['unknown_children'] and not item['invalid_attributes']
                angles=item['rotations'][0];assert set(angles)<=set(('pitch','yaw','roll'))
                rows.append((1,[angles.get(name) for name in ('pitch','yaw','roll')]))
    assert sum(kind==1 for kind,_ in rows)==20010 and sum(kind==0 for kind,_ in rows)==6
    return rows,hashlib.sha256(census.read_bytes()).hexdigest()


def batch_inputs(rows):
    attrs=(Attribute*(len(rows)*3))()
    kinds=(ctypes.c_uint*len(rows))()
    retained=[]
    for i,(kind,values) in enumerate(rows):
        kinds[i]=kind
        for j,value in enumerate(values):
            encoded=None if value is None else value.encode('ascii')
            retained.append(encoded)
            attrs[3*i+j]=Attribute(encoded,0 if encoded is None else len(encoded),int(encoded is not None))
    return kinds,attrs,retained


def batch_expected(rows):
    result=[]
    for kind,text in rows:
        if kind==0: result.append((1,[SENTINEL]*9))
        else: result.append((0,[value for vector in reference.text_basis(*text) for value in vector]))
    return result


def batch_run(lib,rows,expected,environment):
    kinds,attrs,retained=batch_inputs(rows)
    statuses=(ctypes.c_int*len(rows))()
    out=(U32*(len(rows)*9))(*([SENTINEL]*(len(rows)*9)))
    started=time.perf_counter()
    status=lib.x4rot_test_offset_batch(environment,kinds,attrs,len(rows),statuses,out)
    elapsed=time.perf_counter()-started
    assert status==0,('rounding/MXCSR restore',status)
    for i,(want_status,want_words) in enumerate(expected):
        assert statuses[i]==want_status and list(out[i*9:(i+1)*9])==want_words,('batch',environment,i,statuses[i])
    return elapsed


def verify(lib,samples,workload,workload_expected):
    digest=hashlib.sha256();counts={}
    def record(name,inputs,result):
        digest.update(json.dumps([name,inputs,result],separators=(',',':')).encode()+b'\n')
        counts[name]=counts.get(name,0)+1
    pi=(U32*13)();lib.x4rot_half_pi_words(pi)
    assert sum(value<<(32*i) for i,value in enumerate(pi))==reference.half_pi()
    for word in independent.corpus(samples):
        result,words=binary_call(lib,'x4rot_sincos',[word],2)
        assert result==0 and tuple(words)==reference.sincos(word),('sincos',hex(word),result,[hex(x) for x in words])
        record('sincos',[word],words)
    for word in (0x7f800000,0xff800000,0x7fc00000):
        assert binary_call(lib,'x4rot_sincos',[word],2)[0]==-1
    selected=(0,0x80000000,0x3f000000,0x3f490fdb,0x3fc90fdb,0xbfc90fdb,0x7f7fffff)
    with localcontext() as context:
        context.prec=180
        pi=independent.decimal_pi()
        independently_expected={(p,y,r):[value for vector in independent.reference_basis(p,y,r,pi) for value in vector]
                                for p in selected for y in selected for r in selected}
    for pitch in selected:
        for yaw in selected:
            for roll in selected:
                inputs=(U32*3)(pitch,yaw,roll)
                result,words=binary_call(lib,'x4rot_basis',[inputs],9)
                expected=[value for vector in reference.basis(pitch,yaw,roll) for value in vector]
                assert result==0 and words==expected==independently_expected[pitch,yaw,roll],('basis',pitch,yaw,roll,result)
                record('basis',[pitch,yaw,roll],words)
    counts['independent_basis_coefficients']=343*9
    rng=random.Random(0x433332524e45)
    scalar=[(rng.randrange(0x7f800000)|rng.randrange(2)<<31,rng.randrange(0x7f800000)|rng.randrange(2)<<31) for _ in range(samples)]
    scalar += [(1,0x3f000000),(3,0x3f000000),(0x80000001,0x3f000000),(0x80000000,0x80000000),(0x3f800000,0x33800000),(0x3f800001,0x33800000),(0x7f7fffff,0x40000000)]
    for name,oracle in (('add',reference.add),('multiply',reference.multiply)):
        for a,b in scalar:
            try: expected=(0,[oracle(a,b)])
            except ValueError: expected=(-2,[SENTINEL])
            actual=binary_call(lib,'x4rot_'+name,[a,b],1)
            assert actual==expected,(name,hex(a),hex(b),actual,expected)
            record(name,[a,b],actual)
    text=['0','-0','90','-90','.5','-.5','0.8875748217105865478515625','340282346638528859811704183484516925440',
          '-0.'+'0'*200+'1','9'*256,'0'*256,'1.'+'0'*254,'','+90',' 90','90;','1e3','nan','inf','0'*257,'1\x00']
    text += [format(independent.decimal_word(word),'f') for word in independent.corpus(0)[::7]]
    for value in text:
        encoded=value.encode('ascii')
        try: expected=reference.decimal_to_binary32(value);good=True
        except ValueError: good=False
        result,words=binary_call(lib,'x4rot_decimal',[encoded,len(encoded)],1)
        assert (result==0 and words==[expected]) if good else (result in (-1,-2) and words==[SENTINEL]),('decimal',value,result)
        record('decimal',[value],[result,words])
    with localcontext() as context:
        context.prec=256
        rng=random.Random(0x444543494d414c)
        lower_words={rng.randrange(0x7f7fffff) for _ in range(1024)}
        lower_words.update(exponent<<23 for exponent in range(255))
        epsilon=Decimal(1).scaleb(-200)
        for low in sorted(lower_words):
            midpoint=(independent.decimal_word(low)+independent.decimal_word(low+1))/2
            for delta,target in ((-epsilon,low),(Decimal(0),low+(low&1)),(epsilon,low+1)):
                for negative in (False,True):
                    value=('-' if negative else '')+format(midpoint+delta,'f');encoded=value.encode('ascii')
                    expected=target|(0x80000000 if negative else 0)
                    result,words=binary_call(lib,'x4rot_decimal',[encoded,len(encoded)],1)
                    assert result==0 and words==[expected],('decimal midpoint',value,result,words)
                    record('decimal_midpoint',[value],words)
    bad=(Attribute*3)(Attribute(None,0,0),Attribute(b'90',2,1),Attribute(b'bad',3,1))
    assert binary_call(lib,'x4rot_offset',[1,bad],9)[0]==-1
    bad[2]=Attribute(b'0',1,0)
    assert binary_call(lib,'x4rot_offset',[1,bad],9)[0]==-1
    assert binary_call(lib,'x4rot_offset',[2,bad],9)[0]==-1
    env_rows=[(0,[None]*3),(1,[None]*3),(1,['','','']),(1,['-0','-0','-0']),
              (1,[None,'90',None]),(1,[None,'-90',None]),(1,['0.8875748217105865478515625','0.1','-0.2']),
              (1,['340282346638528859811704183484516925440','-340282346638528859811704183484516925440','0.'+'0'*44+'1'])]
    env_expected=batch_expected(env_rows)
    env_times=[batch_run(lib,env_rows,env_expected,environment) for environment in range(8)]
    barrier=threading.Barrier(8,timeout=15)
    def concurrent(environment):
        barrier.wait()
        for _ in range(8):batch_run(lib,env_rows,env_expected,environment)
        return environment
    with ThreadPoolExecutor(max_workers=8) as pool:
        assert sorted(pool.map(concurrent,range(8)))==list(range(8))
    counts['concurrent_environment_batches']=64
    workload_times=[]
    if workload:
        # Actual saved-text workload, once per MXCSR/rounding combination.
        workload_times=[batch_run(lib,workload,workload_expected,environment) for environment in range(8)]
        counts['saved_euler_banks']=sum(kind==1 for kind,_ in workload)
        counts['absent_orientation_retained']=sum(kind==0 for kind,_ in workload)
        for (kind,text),expected in zip(workload,workload_expected): record('workload',[kind,text],expected)
    return dict(counts=counts,output_sha256=digest.hexdigest(),environment_variants=8,
                near_extreme_batch_seconds=env_times,saved_workload_batch_seconds=workload_times)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    compiler=parser.add_mutually_exclusive_group()
    compiler.add_argument('--zig',type=Path);compiler.add_argument('--cc',default=None)
    parser.add_argument('--samples',type=int,default=4096)
    parser.add_argument('--census',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--compare',type=Path)
    args=parser.parse_args()
    if not 0<=args.samples<=100000:parser.error('bounded sample count required')
    if args.zig:
        version=subprocess.check_output([str(args.zig),'version'],text=True).strip()
        if version!='0.15.2':parser.error('expected established Zig0.15.2')
        command=[str(args.zig),'cc','-target','x86_64-windows-gnu' if os.name=='nt' else 'x86_64-linux-gnu']
    else:
        exe=shutil.which(args.cc or 'cc')
        if not exe:parser.error('C compiler not found')
        version=subprocess.check_output([exe,'--version'],text=True).splitlines()[0];command=[exe]
    directory=ROOT/'.local/portable-rotation/c-build'/platform.system().lower();directory.mkdir(parents=True,exist_ok=True)
    workload,census_sha=text_rows(args.census);expected=batch_expected(workload)
    results={}
    for name,flags in (('debug',['-O0']),('optimized',['-O2']),('fast',['-O3','-ffast-math'])):
        obj=directory/(name+'.o');binary=directory/(name+('.dll' if os.name=='nt' else '.so'))
        common=['-std=c11','-Wall','-Wextra','-Werror']+([] if os.name=='nt' else ['-fPIC'])
        subprocess.run(command+common+flags+['-c',str(SOURCE/'portable_rotation.c'),'-o',str(obj)],check=True)
        subprocess.run(command+common+[flags[0],'-shared',str(obj),str(SOURCE/'test_portable_rotation_env.c'),'-o',str(binary)]+([] if os.name=='nt' else ['-lm']),check=True)
        results[name]=verify(bind(binary),args.samples,workload,expected)
    assert len({item['output_sha256'] for item in results.values()})==1
    files=[SOURCE/'portable_rotation.h',SOURCE/'portable_rotation.c',SOURCE/'test_portable_rotation_env.c',Path(__file__),Path(reference.__file__),Path(independent.__file__)]
    report=dict(scope='standalone nongame C arithmetic, sampled correctness and owned test-process environment only',platform=platform.platform(),compiler=version,
                census_sha256=census_sha,results=results,sources={path.name:hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest() for path in files})
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    if args.compare:
        other=json.loads(args.compare.read_text());assert other['sources']==report['sources'] and other['census_sha256']==census_sha
        for name in results:
            for key in ('counts','output_sha256','environment_variants'):assert other['results'][name][key]==results[name][key],('cross-host',name,key)


if __name__=='__main__':main()
