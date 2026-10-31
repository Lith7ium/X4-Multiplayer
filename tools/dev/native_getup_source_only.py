"""Isolated diagnostic source imports; never consumes or writes bytecode caches.

Entrypoints load this file with read+compile before local dependency imports.
Ordinary lane-wrapper invocations do not install this policy.
"""
import hashlib,importlib.machinery,sys
from pathlib import Path

compiled={}
bound=None
bound_roots=[]
failure=None

def remember(path,data):
    global failure
    if failure is not None:raise ImportError(failure)
    path=str(Path(path).resolve());digest=hashlib.sha256(data).hexdigest()
    if bound is not None and any(Path(path).is_relative_to(root) for root in bound_roots) and bound.get(path)!=digest:
        raise ImportError('source outside bound diagnostic inventory: '+path)
    if path in compiled and compiled[path]!=digest:
        failure='diagnostic source changed between executions: '+path
        raise ImportError(failure)
    compiled[path]=digest

def record_script(path):remember(path,Path(path).read_bytes())

class SourceLoader(importlib.machinery.SourceFileLoader):
    def get_code(self,fullname):
        data=Path(self.path).read_bytes();remember(self.path,data)
        return compile(data,self.path,'exec',dont_inherit=True)

class SourceFinder:
    @staticmethod
    def find_spec(fullname,path=None,target=None):
        spec=importlib.machinery.PathFinder.find_spec(fullname,path,target)
        if spec is not None:
            if isinstance(spec.loader,importlib.machinery.SourcelessFileLoader):raise ImportError('sourceless diagnostic bytecode refused: '+fullname)
            if isinstance(spec.loader,importlib.machinery.SourceFileLoader):spec.loader=SourceLoader(fullname,spec.origin)
        return spec
    @staticmethod
    def invalidate_caches():importlib.machinery.PathFinder.invalidate_caches()

def install():
    sys.dont_write_bytecode=True
    if not any(isinstance(finder,SourceFinder) for finder in sys.meta_path):
        at=sys.meta_path.index(importlib.machinery.PathFinder);sys.meta_path[at]=SourceFinder()

def execute(module,path,expected):
    data=Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:raise ImportError('source changed before diagnostic execution')
    remember(path,data);exec(compile(data,str(path),'exec',dont_inherit=True),module.__dict__)

def bind(trees):
    global bound,bound_roots
    expected={str((Path(tree['path'])/rel).resolve()):row['sha256'] for tree in trees for rel,row in tree['files'].items()}
    if bound is not None and bound!=expected:raise ValueError('diagnostic execution inventory cannot be rebound')
    bound=expected;bound_roots=[Path(tree['path']).resolve() for tree in trees]

def verify(trees):
    if failure is not None:raise ValueError(failure)
    expected={str((Path(tree['path'])/rel).resolve()):row['sha256'] for tree in trees for rel,row in tree['files'].items()}
    roots=[Path(tree['path']).resolve() for tree in trees]
    for tree in trees:
        for rel in tree['files']:
            parts=Path(rel).parts
            if not rel.endswith('.py'):continue
            name='.'.join(parts[:-1] if parts[-1]=='__init__.py' else (*parts[:-1],parts[-1][:-3]))
            module=sys.modules.get(name)
            if module is not None and Path(getattr(module,'__file__','')).resolve()!=(Path(tree['path'])/rel).resolve():
                raise ValueError('cached diagnostic module origin differs: '+name)
    for name,module in tuple(sys.modules.items()):
        file=getattr(module,'__file__',None)
        if not isinstance(file,str):continue
        path=Path(file).resolve()
        if any(path.is_relative_to(root) for root in roots):
            if path.suffix!='.py' or str(path) not in compiled:raise ValueError('unrecorded diagnostic source module: '+name)
    for path,digest in compiled.items():
        if any(Path(path).is_relative_to(root) for root in roots) and expected.get(path)!=digest:
            raise ValueError('executed diagnostic source differs from receipt: '+path)
