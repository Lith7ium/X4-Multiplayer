"""Compare manager enqueue inputs separately from provenance coverage."""
import argparse,json
from pathlib import Path
from native_world_trace import evidence as motion_evidence
from native_world_queue import evidence,compare

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left',type=Path);parser.add_argument('right',type=Path);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();errors=[];samples=[];coverage=[]
    for path in (args.left,args.right):
        text=path.read_text(encoding='utf-8',errors='replace');motion,_=motion_evidence(text,errors,path.name)
        events,summary=evidence(text,errors,path.name,motion);samples.append(events);coverage.append(summary)
    result={'scope':'selected imported recipients; enqueue before ownership transfer, typed dispatch before native destruction, exact nested append; native load indicators are pre-admission diagnostics, addresses/lifetimes local only',
            'errors':errors,'coverage':coverage,'provenance_errors':[e for c in coverage for e in c['provenance_errors']],
            'differences':compare(*samples) if not errors else []}
    args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'errors':errors,'provenance_errors':len(result['provenance_errors']),'differences':len(result['differences']),'out':str(args.out)}))
    return bool(errors or result['provenance_errors'])

if __name__=='__main__':raise SystemExit(main())
