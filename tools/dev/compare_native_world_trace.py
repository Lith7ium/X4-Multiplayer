"""Compare watched native motion calls without matching process/thread addresses."""
import argparse
import json
from pathlib import Path
from native_world_trace import evidence,compare


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left',type=Path);parser.add_argument('right',type=Path)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();errors=[];traces=[];coverage=[]
    for path in (args.left,args.right):
        with path.open(encoding='utf-8',errors='replace') as source:
            trace,summary=evidence(source,errors,path.name)
        traces.append(trace);coverage.append(summary)
    result={'scope':'read-only imported-world motion calls; raw blocks include local pointers/padding',
            'errors':errors,'coverage':coverage,'differences':compare(*traces) if not errors else []}
    args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'errors':errors,'differences':len(result['differences']),'out':str(args.out)}))
    return bool(errors)


if __name__=='__main__':raise SystemExit(main())
