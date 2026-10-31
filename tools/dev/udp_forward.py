"""Loopback UDP forwarder for a remote lockstep relay.

Local clients (the lane X4 processes) keep talking to 127.0.0.1:<port> only; each
local peer gets its own upstream socket to the remote relay, so the relay still sees
one source address per client. Datagrams are forwarded unchanged in both directions.
Stops after --idle seconds without traffic once at least one peer has connected.
"""
import argparse
import selectors
import socket
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--listen-port', type=int, required=True)
    p.add_argument('--remote', required=True, help='relay host (dotted IPv4)')
    p.add_argument('--remote-port', type=int, required=True)
    p.add_argument('--idle', type=float, default=120.0)
    args = p.parse_args()
    socket.inet_aton(args.remote)
    local = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local.bind(('127.0.0.1', args.listen_port))
    sel = selectors.DefaultSelector()
    sel.register(local, selectors.EVENT_READ, None)
    upstream = {}                 # local peer address -> upstream socket
    last = None
    counts = {'up': 0, 'down': 0}
    print(f'UDP_FORWARD READY 127.0.0.1:{args.listen_port} -> {args.remote}:{args.remote_port}', flush=True)
    while True:
        for key, _ in sel.select(timeout=1.0):
            sock, peer = key.fileobj, key.data
            try:
                data, address = sock.recvfrom(65536)
            except (ConnectionResetError, OSError):
                continue          # ICMP unreachable on Windows; keep serving
            last = time.monotonic()
            if peer is None:      # from a local client: forward upstream
                out = upstream.get(address)
                if out is None:
                    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    out.connect((args.remote, args.remote_port))
                    sel.register(out, selectors.EVENT_READ, address)
                    upstream[address] = out
                    print(f'UDP_FORWARD PEER {address[0]}:{address[1]} via {out.getsockname()[1]}', flush=True)
                out.send(data)
                counts['up'] += 1
            else:                 # from the relay: back to its local client
                local.sendto(data, peer)
                counts['down'] += 1
        if last is not None and time.monotonic() - last > args.idle:
            print(f'UDP_FORWARD DONE peers={len(upstream)} up={counts["up"]} down={counts["down"]}', flush=True)
            return


if __name__ == '__main__':
    main()
