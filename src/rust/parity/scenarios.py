"""Generated replay scenarios for relay_parity.py.

handwritten(): named protocol cases (handshake, authentication, malformed and oversize
packets, lease expiry/resume boundaries, reordering, each session layer's flows).
randomized(n, seed): grammar-based traffic for every packet kind at every session level,
mostly well-formed and stateful (increasing sequences, real baselines/snapshots) with
seeded mutations: reordering, duplicates, corrupted/missing/extra fields, exotic numbers,
wrong leases/addresses, non-ASCII and oversize datagrams, and clock jumps across the
lease, freshness and staging timeouts.
numbers(seed): CPython float() grammar and '.9g'/'%.17g'/'.3f' formatting cases.

Leases are deterministic in both drivers: the n-th lease a session grants is f"{n:032x}".
"""
import random
import struct
import zlib

TOKEN = "a" * 32
BUBBLE = "bubble"
P1, P2, P3 = "127.0.0.1:10001", "127.0.0.1:10002", "127.0.0.1:10003"
P1B = "127.0.0.1:20001"          # same IP as P1, different port
OTHER_IP = "10.0.0.7:10001"
SECTOR = "cluster_01_sector001_macro"
SECTOR2 = "cluster_02_sector001_macro"
LEVELS = ["base", "bubble", "economy", "coop", "dock", "universe"]


def lease(n):
    return f"{n:032x}"


def nonce(n):
    return f"{n:x}" * 32 if n < 16 else f"{n:032x}"


class Scenario:
    def __init__(self, name, level, build=None):
        self.name, self.level = name, level
        self.build = build or ("world1-test" if level == "universe" else "test")
        self.commands = [{"op": "new", "level": level, "token": TOKEN, "build": self.build, "bubble": BUBBLE}]

    def recv(self, t, addr, data):
        if isinstance(data, str):
            data = data.encode("latin-1")
        self.commands.append({"op": "recv", "t": t, "addr": addr, "hex": data.hex()})
        return self

    def send(self, t, addr, *fields):
        return self.recv(t, addr, "|".join(str(f) for f in fields))

    def snap(self, t):
        self.commands.append({"op": "snap", "t": t})
        return self

    def expire(self, t):
        self.commands.append({"op": "expire", "t": t})
        return self

    def hello(self, t, slot="p1", addr=P1, n=1, sector=SECTOR, token=TOKEN, build=None, bubble=BUBBLE):
        return self.send(t, addr, "HELLO", 1, token, build or self.build, sector, bubble, slot, nonce(n))

    def dict(self):
        return {"name": self.name, "level": self.level, "commands": self.commands}


def world(hull1=100, hull2=100, extra=None):
    values = [hull1, 50, 3, hull2, 50, 4, 80, 80, 90, 90, 7, 1, 0, 1, 0, 12, 13]
    if extra:
        values = extra
    return values


def econ_payload():
    rows = ["M,200000,10,20,30"]
    for i, ware in enumerate(("energycells", "water", "food_rations")):
        rows.append(f"W,{ware},{10 + i},5,6,100,200,10,12,500,11,13,600")
    return ";".join(rows)


def mission_payload(state="O", done=0, total=5):
    text = lambda s: s.encode().hex()
    return f"quota,{state},{done},{total},1000,argon,{text('Title')},{text('Deliver ' * 3)},{text('Objective')}"


def ship_row(i, x=1.5):
    return f"{i},ship,ship_arg_s_fighter_01_a_macro,argon,{x},2,3,0.1,0.2,0.3,none,0"


# ---------------------------------------------------------------- hand-written


def handwritten():
    out = []

    def case(name, level="base"):
        s = Scenario(name, level)
        out.append(s)
        return s

    for level in LEVELS:
        s = case(f"{level}: two players exchange poses and ticks", level)
        s.hello(0).hello(0.01, "p2", P2, 2)
        for i in range(1, 6):
            s.send(i * .05, P1, "STATE", 1, lease(1), i, 10 + i, -20.5, 1e-3, .1, -.2, 3.1)
            s.send(i * .05, P2, "STATE", 1, lease(2), i, -1, 2, 3, 0, 0, 0)
            s.snap(i * .05 + .01)
        s.snap(1.0)

        s = case(f"{level}: authentication and envelope failures", level)
        for raw in (b"", b"\xff", b"HELLO", b"HELLO|2|x", b"PING|1|" + TOKEN.encode() + b"|" + b"1" * 32,
                    b"PING|1|" + b"b" * 32 + b"|" + b"1" * 32, b"PING|1|" + TOKEN.encode() + b"|" + b"G" * 32):
            s.recv(0, P3, raw)
        s.hello(0, token="b" * 32).hello(0, build="other").hello(0, bubble="else").hello(0, "p3")
        s.hello(0, sector="bad sector").hello(0, sector="x" * 97).hello(0, n=99)
        s.send(0, P1, "HELLO", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", "1" * 31)
        s.send(0, P1, "HELLO", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", "1" * 32, "extra")
        s.hello(0.1).snap(0.2)
        s.hello(0.2, "p2", P1, 2)                 # one endpoint cannot hold both slots
        s.hello(0.3, "p2", P2, 2, sector=SECTOR2)  # sector is fixed by the first player
        s.hello(0.4, "p1", P2, 1)                 # slot owned by another address
        s.hello(0.5, "p1", P1, 3)                 # different nonce for a live slot
        s.hello(0.6)                              # lost WELCOME: same lease again
        s.snap(0.7)

        s = case(f"{level}: oversize, non-ASCII and field-count edges", level)
        s.hello(0)
        base = f"STATE|1|{lease(1)}|1|1|2|3|0|0|0".encode()
        s.recv(.1, P1, base + b" " * (768 - len(base)))            # exactly 768 with junk float
        s.recv(.1, P1, base + b"0" * (768 - len(base)))            # 768, valid number
        s.recv(.2, P1, f"STATE|1|{lease(1)}|2|1|2|3|0|0|0".encode() + b"0" * (769 - len(base)))
        s.recv(.3, P1, f"STATE|1|{lease(1)}|3|1|2|3|0|0|\xe9".encode("latin-1"))
        s.send(.4, P1, "STATE", 1, lease(1), 4, 1, 2, 3, 0, 0)
        s.send(.5, P1, "STATE", 1, lease(1), 5, 1, 2, 3, 0, 0, 0, 0)
        s.send(.6, P1, "IDLE", 1, lease(1))
        s.send(.7, P1, "IDLE", 1, lease(1), 6, 7)
        for t in (.8, .9):
            s.snap(t)

        s = case(f"{level}: python float grammar in poses", level)
        s.hello(0).hello(0, "p2", P2, 2)
        seq = 0
        for value in (" 1", "1 ", "\t2", "1_0", "1__0", "_1", ".5", "5.", "1e3", "1E+3", "1e", "inf", "-inf", "nan",
                      "Infinity", "1e309", "-0", "0x10", "+.5", "1e-400", "50000", "50000.0000001", "6.283185307179586",
                      "6.2831853071795865", "\x0b3", "\x1c3", "1_000.5_5", "1e1_0", "--1", "+-1"):
            seq += 1
            s.send(seq * .01, P1, "STATE", 1, lease(1), seq, value if seq % 2 else 1, 2, 3, 0, 0, value if seq % 2 == 0 else 0)
            s.snap(seq * .01 + .001)

        s = case(f"{level}: sequence ordering and idle", level)
        s.hello(0).hello(0, "p2", P2, 2)
        for seq in (5, 3, 5, 6, 2147483647, 2147483648, "0007", "+8", " 9", "", "-1"):
            s.send(.1, P1, "STATE", 1, lease(1), seq, seq if isinstance(seq, int) and seq < 100 else 1, 0, 0, 0, 0, 0)
            s.snap(.11)
        s.send(.2, P1, "IDLE", 1, lease(1), 2147483647).snap(.21)
        s.send(.3, P1, "STATE", 1, lease(1), 7, 1, 1, 1, 0, 0, 0).snap(.31)

        s = case(f"{level}: lease expiry boundary, retired nonce and resume", level)
        s.hello(0).hello(0, "p2", P2, 2).send(0.5, P1, "STATE", 1, lease(1), 1, 1, 2, 3, 0, 0, 0)
        s.expire(29.999).snap(29.999)
        s.send(29.9995, P2, "IDLE", 1, lease(2), 1)
        s.expire(30.0).snap(30.0)                                   # p1 expired at exactly 30 s
        s.hello(30.1)                                               # retired (slot, nonce)
        s.send(30.2, P1, "RESUME", 1, TOKEN, s.build, SECTOR2, BUBBLE, "p1", nonce(1), lease(1), nonce(5))  # sector
        s.send(30.3, P1, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(4), lease(1), nonce(5))   # nonce
        s.send(30.4, P1, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(9), nonce(5))   # lease
        s.send(30.5, P2, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(5))   # address
        s.send(30.6, P3, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(5))   # ok
        s.send(30.7, P3, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(5))   # lost WELCOME retry
        s.send(30.8, P3, "STATE", 1, lease(1), 2, 4, 5, 6, 0, 0, 0)
        s.send(30.9, P3, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(5))   # no longer
        s.send(31, P1, "STATE", 1, lease(1), 3, 4, 5, 6, 0, 0, 0)  # old address
        s.snap(31.1).expire(61).snap(61)
        s.send(62, P3, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p2", nonce(9), lease(9), nonce(10))     # unknown -> ok?
        s.send(63, P3, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p2", nonce(2), lease(2), nonce(11))
        s.hello(64, "p2", P2, 12).snap(64.1)

        s = case(f"{level}: fresh relay answers RESUME with RESET", level)
        s.send(0, P1, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(2))
        s.send(0, P1, "RESUME", 1, TOKEN, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), "x")
        s.send(0, P1, "RESUME", 1, "b" * 32, s.build, SECTOR, BUBBLE, "p1", nonce(1), lease(1), nonce(2))

    for level in ("bubble", "economy", "coop", "dock", "universe"):
        s = case(f"{level}: world, actions and command lifecycle", level)
        s.hello(0).hello(0, "p2", P2, 2)
        s.send(0.1, P2, "ACTION", 1, TOKEN, "p2", "c" * 32, "TRADE", "energycells")   # no world
        s.send(0.2, P1, "WORLD", 1, lease(1), 1, *world())
        s.send(0.2, P1, "WORLD", 1, lease(1), 1, *world(hull1=50))                     # stale revision
        s.send(0.25, P2, "WORLD", 1, lease(2), 2, *world())                            # guest cannot publish
        s.send(0.3, P1, "WORLD", 1, lease(1), 2, *world(extra=[101] + world()[1:]))   # bound
        s.send(0.3, P1, "WORLD", 1, lease(1), 3, *world(extra=world()[:11] + [2] + world()[12:]))
        s.send(0.3, P1, "WORLD", 1, lease(1), 4, *world(extra=world()[:2] + [3.5] + world()[3:]))
        s.send(0.35, P1, "CREW", 1, lease(1), 1, 1, 1).send(0.35, P2, "CREW", 1, lease(2), 1, 2, 2)
        for i, verb in enumerate(("TRADE", "PICKUP", "TRADE", "SELL")):
            s.send(0.4 + i / 100, P2, "ACTION", 1, TOKEN, "p2", f"{i + 1:x}" * 32, verb, "energycells")
        s.send(0.45, P2, "ACTION", 1, TOKEN, "p2", "1" * 32, "TRADE", "energycells")   # duplicate
        s.send(0.45, P1, "ACTION", 1, TOKEN, "p2", "1" * 32, "TRADE", "energycells")   # same IP, other port
        s.send(0.45, OTHER_IP, "ACTION", 1, TOKEN, "p2", "1" * 32, "TRADE", "energycells")
        s.send(0.46, P2, "ACTION", 1, TOKEN, "p2", "9" * 32, "TRADE", "water")
        s.snap(0.5)
        s.send(0.6, P1, "DONE", 1, lease(1), "1" * 32, "OK").send(0.6, P2, "DONE", 1, lease(2), "2" * 32, "OK")
        s.send(0.6, P1, "DONE", 1, lease(1), "2" * 32, "MAYBE").snap(0.7)
        for i in range(12):
            s.send(0.8, P1, "ACTION", 1, TOKEN, "p1", f"{i:02x}" * 16, "PICKUP", "energycells")
        s.snap(0.9).expire(31).snap(31)
        s.send(31, P2, "ACTION", 1, TOKEN, "p2", "1" * 32, "TRADE", "energycells").snap(31.1)

    for level in ("bubble", "economy"):
        s = case(f"{level}: body, walk, fire and suit kill", level)
        s.hello(0).hello(0, "p2", P2, 2)
        s.send(.1, P2, "BODY", 1, lease(2), 1, "suit", 0, 3, 1, 2, 3, 0, 0, 0)
        s.send(.1, P2, "BODY", 1, lease(2), 2, "suit", 0, 0, 1, 2, 3, 0, 0, 0)
        s.send(.1, P2, "BODY", 1, lease(2), 2, "suit", 0, 2, 1, 2, 3, 0, 0, 0)
        s.send(.1, P2, "BODY", 1, lease(2), 3, "foot", 2, 3, 1, 2, 3, 0, 0, 0)
        s.send(.1, P2, "BODY", 1, lease(2), 4, "suit", "01", 3, " 1", "1_0", 3, 0, 0, 0)
        s.send(.1, P2, "BODY", 1, lease(2), 5, "suit", 0, 3, 1, 2, 3, 0, 0, 0)   # cannot revive
        s.send(.2, P2, "WALK", 1, lease(2), 1, "04", 1, 2, 3, 359, 0, -360)
        s.send(.2, P2, "WALK", 1, lease(2), 2, 5, 1, 2, 3, 0, 0, 0)
        s.send(.2, P1, "WALK", 1, lease(1), 1, 0, 10001, 2, 3, 0, 0, 0)
        st = ("station", 0, "cluster_14_sector001_macro", "ABC-123", "station_gen_factory_base_01_macro", "room_a_macro")
        s.send(.2, P2, "WALK2", 1, lease(2), 1, 1, *st, 1.5, 0, -2, 90, 1.4, 5.25, 0)
        s.send(.2, P2, "WALK2", 1, lease(2), 1, 1, *st, 9, 0, -2, 90, 1.4, 5.25, 0)            # duplicate seq
        s.send(.2, P2, "WALK2", 1, lease(2), 2, 1, "ship", 3, "-", "-", "-", "-", 0, 0, 0, 0, 0, 1, 0)  # ship 3
        s.send(.2, P2, "WALK2", 1, lease(2), 2, 1, "none", 0, "-", "-", "-", "-", 0, 0, 0, 0, 0, 1, 0)
        s.send(.2, P2, "WALK2", 1, lease(2), 2, 1, "station", 0, "-", "A", "m", "-", 0, 0, 0, 0, 0, 1, 0)
        s.send(.2, P2, "WALK2", 1, lease(2), 2, 1, *st, " 1", "1_0", "-0", 360, 100, 1e9, 2)
        s.send(.2, P1, "WALK2", 1, lease(1), 1, 0, "none", 0, "-", "-", "-", "-", 0, 0, 0, 0, 0, 0, 0)
        s.send(.2, P1, "WALK2", 1, lease(1), 2, 1, "ship", 2, "-", "-", "-", "x" * 97, 0, 0, 0, 0, 0, 0, 0)
        s.send(.2, P1, "WALK2", 1, lease(1), 3, 1, "ship", 1, "-", "-", "-", "-", "nan", 0, 0, 0, 0, 0, 0)
        s.send(.25, P2, "WALK2", 1, lease(2), 3, 1, *st, 2, 0, -2, 90, 1.4, 5.5, 0)
        s.send(.3, P1, "FIRE", 1, lease(1), 7).send(.3, P1, "FIRE", 1, lease(1), 6)
        s.send(.3, P1, "SUITKILL", 1, lease(1), 3).send(.3, P2, "SUITKILL", 1, lease(2), 3)
        for t in (.21, .26, .31, .45, .5, .7, .9, 1.25, 1.4):
            s.snap(t)

    for level in ("economy", "coop", "dock", "universe"):
        s = case(f"{level}: economy assembly and orders", level)
        s.hello(0).hello(0, "p2", P2, 2)
        payload = econ_payload()
        parts = [payload[i:i + 100] for i in range(0, len(payload), 100)]
        n = len(parts)
        s.send(.1, P1, "ECON", 1, lease(1), 1, 2, n, parts[1])
        s.send(.1, P1, "ECON", 1, lease(1), 1, 2, n, parts[1] + "x")             # conflicting duplicate
        for i, part in enumerate(parts, 1):
            s.send(.1 + i / 1000, P1, "ECON", 1, lease(1), 1, i, n, part)
        s.send(.2, P1, "ECON", 1, lease(1), 2, 1, 1, "M,1,2,3,4")                 # invalid snapshot
        s.send(.2, P1, "ECON", 1, lease(1), 3, 1, 9, "M")                         # total bound
        s.send(.25, P1, "WORLD", 1, lease(1), 1, *world())
        s.send(.25, P1, "CREW", 1, lease(1), 1, 1, 1).send(.25, P2, "CREW", 1, lease(2), 1, 2, 2)
        s.snap(.3)
        lines = "energycells,-5,10;water,3,12"
        kind = "COOPORDER" if level != "economy" else "ORDER"
        for i, (owner, basket) in enumerate(((lease(1), lines), (lease(2), lines), (lease(1), "energycells,0,10"),
                                             (lease(1), "a,1,1;a,1,1"), (lease(1), "energycells,1000000,1000001"))):
            if kind == "ORDER":
                s.send(.4, P2, "ORDER", 1, lease(2), f"{i + 1:x}" * 32, owner, "m1", basket)
            else:
                s.send(.4, P2, "COOPORDER", 1, lease(2), f"{i + 1:x}" * 32, owner, 2, 1, "m1", basket)
        if kind == "COOPORDER":
            s.send(.41, P2, "COOPORDER", 1, lease(2), "1" * 32, lease(1), 2, 1, "m1", lines)   # retry
            s.send(.41, P2, "COOPORDER", 1, lease(2), "1" * 32, lease(1), 1, 1, "m1", lines)   # other ship
            s.send(.41, P2, "COOPORDER", 1, lease(2), "e" * 32, lease(1), 1, 1, "m1", lines)   # not pilot
        else:
            s.send(.41, P2, "ORDER", 1, lease(2), "1" * 32, lease(1), "m1", lines)
            s.send(.41, P2, "ORDER", 1, lease(2), "1" * 32, lease(1), "m2", lines)
        s.send(.42, P2, "DOCKSTATE", 1, lease(2), 3, "m2").send(.42, P2, "DOCKSTATE", 1, lease(2), 2, "m1")
        s.snap(.5).snap(.6)
        s.send(.7, P1, "ORDER_DONE", 1, lease(1), "1" * 32, "OK").send(.7, P1, "ORDER_DONE", 1, lease(1), "1" * 32, "FUNDS")
        s.snap(.8).snap(1.1).expire(31).snap(31.2)

    for level in ("coop", "dock", "universe"):
        s = case(f"{level}: pilot handoff, nav, fire2, health and shots", level)
        s.hello(0).hello(0, "p2", P2, 2)
        s.send(.05, P1, "WORLD", 1, lease(1), 1, *world())
        s.send(.1, P1, "CREW", 1, lease(1), 1, 1, 1).send(.1, P2, "CREW", 1, lease(2), 1, 1, 1)   # denied
        s.send(.1, P2, "CREW", 1, lease(2), 2, 2, 2).send(.1, P2, "CREW", 1, lease(2), 2, 0, 0)
        s.send(.1, P2, "CREW", 1, lease(2), 3, 1, 2).snap(.11)
        for i in range(1, 4):
            t = .1 + i * .05
            nav = [lease(2), 2, 1, i, SECTOR, 10 * i, 0, 0, 0, 0, 0, 0, i]
            s.send(t, P2, "NAV2", 1, *nav, 100 + t, 1, 2, 3, 0, 0, 0)
            s.send(t, P2, "FIRE2", 1, lease(2), 2, 1, i, 100 + t, 1, 2, i, i)
            s.send(t, P1, "NAV", 1, lease(1), 1, 1, i, SECTOR, 1, 2, 3, 0, 0, 0, 0, i)
            s.snap(t + .01)
        s.send(.3, P2, "NAV2", 1, lease(2), 2, 1, 9, SECTOR, 1, 2, 3, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0)  # time regress
        s.send(.3, P2, "FIRE2", 1, lease(2), 2, 1, 9, 101, 0, 2, 5, 5)                              # group 0
        s.snap(.35)
        s.send(.36, P2, "HACK", 1, lease(2), 2, 1).send(.36, P2, "HACK", 1, lease(2), 2, 2)
        s.send(.37, P2, "HEALTH", 1, lease(2), 2, 2, 1, 75.5, 20).send(.37, P2, "HEALTH", 1, lease(2), 2, 2, 1, 70, 20)
        s.send(.38, P2, "HEALTH", 1, lease(2), 2, 2, 2, 101, 20)
        s.send(.39, P1, "SHOT", 1, lease(1), 1, 1, 2, 2, 1, 3, "weapon_gen_s_laser_01_mk1_macro:12.500")
        s.send(.39, P2, "SHOT", 1, lease(2), 2, 2, 1, 1, 1, 2, "weapon_gen_s_laser_01_mk1_macro:12.500")
        s.send(.39, P2, "SHOT", 1, lease(2), 2, 2, 3, 0, 1, 2, "weapon_gen_s_laser_01_mk1_macro:12.500")
        s.send(.40, P2, "SHOT", 1, lease(2), 2, 2, 3, 0, 2, 1, "weapon_gen_s_laser_01_mk1_macro:12.500")
        s.send(.40, P2, "BODY2", 1, lease(2), 1, "ship", 0, 0, SECTOR, 1, 2, 3, 0, 0, 0)
        s.send(.40, P2, "PERF", 1, lease(2), 1, 60, 16.6, 33, "ship", SECTOR)
        s.send(.40, P1, "QUEST", 1, lease(1), 1, 1, 5, 0, 0).send(.41, P1, "QUEST", 1, lease(1), 2, 2, 20, 1, 0)
        s.send(.41, P1, "QUEST", 1, lease(1), 3, 1, 20, 1, 0)
        for t in (.42, .5, .9, 1.2, 1.3, 2.0):
            s.snap(t)
        s.send(2.1, P1, "HSTALE", 1, lease(1), 2, 2).snap(2.2)
        s.send(2.3, P1, "WORLD", 1, lease(1), 2, *world(hull2=1)).snap(2.4)
        s.expire(40).snap(40)

    for level in ("coop", "universe"):
        s = case(f"{level}: projectile births, ends, tombstones and retirement", level)
        s.hello(0).hello(0, "p2", P2, 2)
        s.send(.01, P1, "WORLD", 1, lease(1), 1, *world())
        s.send(.02, P1, "CREW", 1, lease(1), 1, 1, 1)
        s.send(.03, P1, "NAV2", 1, lease(1), 1, 1, 1, SECTOR, 0, 0, 0, 0, 0, 0, 0, 0, 10.0, 0, 0, 0, 0, 0, 0)
        gen = 2  # health generation after the crew change (checked by HGEN in the snapshot)
        s.snap(.04)
        for i in range(1, 8):
            s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, i, 10.0 + i / 100, SECTOR, 1 + i % 3, 1, 2, 3, 400, 0, 0, 0.1, 0.2, 0.3, 1.5)
        s.send(.05, P1, "BEND", 1, lease(1), 1, 1, gen, 3, 10.05).send(.05, P1, "BEND", 1, lease(1), 1, 1, gen, 50, 10.05)
        s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, 50, 10.05, SECTOR, 1, 1, 2, 3, 400, 0, 0, 0, 0, 0, 1)    # tombstoned
        s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, 2000, 10.05, SECTOR, 1, 1, 2, 3, 400, 0, 0, 0, 0, 0, 1)  # window
        s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, 9, 12, SECTOR, 1, 1, 2, 3, 400, 0, 0, 0, 0, 0, 1)        # future
        s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, 9, 10, SECTOR, 1, 1, 2, 3, 400, 0, 0, 0, 0, 0, "1.0e0")
        s.send(.05, P1, "BOLT", 1, lease(1), 1, 1, gen, 10, 10, SECTOR, 1, 1, 2, 3, 400, 0, 0, 0, 0, 0, " 1")
        for t in (.06, .08, .10, .15, .2, .25, .5, .8, 1.0):
            s.snap(t)
            s.send(t, P1, "NAV2", 1, lease(1), 1, 1, int(t * 100) + 1, SECTOR, 0, 0, 0, 0, 0, 0, 0, 0, 10.0 + t, 0, 0, 0, 0, 0, 0)

    for level in ("coop", "universe"):
        s = case(f"{level}: mission journal assembly", level)
        s.hello(0).hello(0, "p2", P2, 2)
        for ident, (state, done) in enumerate((("O", 0), ("A", 2), ("C", 5)), 1):
            payload = mission_payload(state, done)
            parts = [payload[i:i + 40] for i in range(0, len(payload), 40)]
            digest = zlib.adler32(payload.encode())
            for i, part in reversed(list(enumerate(parts, 1))):
                s.send(.1 * ident, P1, "MSTATE", 1, lease(1), lease(1), 1, ident, i, len(parts), digest, part)
            s.snap(.1 * ident + .01)
        s.send(.5, P1, "MSTATE", 1, lease(1), lease(1), 2, 1, " 1", "+1", digest, "x")
        s.send(.5, P2, "MREQ", 1, lease(2), lease(1), 1, 3, "ACCEPT").send(.5, P2, "MREQ", 1, lease(2), lease(1), 1, 1, "ABORT")
        for t in (.6, .7, .9, 1.2, 1.5):
            s.snap(t)

    s = case("universe: baseline transfer, acks, interest, motion, cargo, identity and npc", "universe")
    s.hello(0).hello(0, "p2", P2, 2)
    s.send(.01, P1, "WORLD", 1, lease(1), 1, *world())
    s.send(.02, P1, "CREW", 1, lease(1), 1, 1, 1).send(.02, P2, "CREW", 1, lease(2), 1, 2, 2)
    s.send(.03, P2, "UINTEREST", 1, lease(2), 1, SECTOR)
    rows = [ship_row(i) for i in (3, 7, 9)] + ["11,drop,crate_macro,argon,1,2,3,0,0,0,energycells,5"]
    digest = zlib.adler32("\n".join(rows).encode())
    s.send(.04, P1, "UOBJECT", 1, lease(1), lease(1), 1, 1, rows[0])                 # before begin
    s.send(.05, P1, "UBEGIN", 1, lease(1), lease(1), 1, SECTOR, len(rows), digest)
    s.send(.05, P1, "UBEGIN", 1, lease(1), lease(1), 1, SECTOR, len(rows), digest + 1)
    for i, row in enumerate(rows, 1):
        s.send(.06, P1, "UOBJECT", 1, lease(1), lease(1), 1, i, row)
    s.send(.06, P1, "UOBJECT", 1, lease(1), lease(1), 1, 2, rows[0])                 # conflict restarts
    s.send(.07, P1, "UBEGIN", 1, lease(1), lease(1), 1, SECTOR, len(rows), digest)
    for i, row in enumerate(rows, 1):
        s.send(.07, P1, "UOBJECT", 1, lease(1), lease(1), 1, i, row)
    s.send(.08, P1, "UCOMMIT", 1, lease(1), lease(1), 1).send(.08, P1, "UCOMMIT", 1, lease(1), lease(1), "0001")
    s.send(.08, P1, "UPOS", 1, lease(1), lease(1), 1)                                   # malformed UPOS is acked
    for t in (.09, .12, .4, .7):
        s.snap(t)
    s.send(.71, P2, "UACK", 1, lease(2), lease(1), 1, 0)
    s.snap(.72)
    for i in range(1, 6):
        s.send(.72, P2, "UACK", 1, lease(2), lease(1), 1, i)
    s.snap(.73).snap(1.0)
    for i in range(1, 5):
        s.send(1.0 + i / 10, P1, "UMOVE", 1, lease(1), lease(1), 7, i, SECTOR, 50 + i, 1, 2, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0)
        s.send(1.0 + i / 10, P1, "UPOS", 1, lease(1), lease(1), 3, i, SECTOR, 1, 2, 3, 0, 0, 0)
        s.snap(1.0 + i / 10 + .01)
    s.send(1.5, P1, "IDENT", 1, lease(1), lease(1), 1, "N", 7, "ARG-123", "41726773".lower(), 1, 0)
    s.send(1.5, P1, "IDENT", 1, lease(1), lease(1), 2, "P", 1, "PLY-1", "50", 1, 1)
    s.send(1.5, P1, "IDENT", 1, lease(1), lease(1), 3, "N", 8, "ARG-124", "41", 1, 0)
    s.send(1.5, P1, "IDENT", 1, lease(1), lease(1), 4, "F", 4, "MKT-4", "0a", 1, 0)
    s.send(1.5, P1, "CINV", 1, lease(1), lease(1), 1, "energycells", 10, 20)
    s.send(1.5, P2, "CARGOREQ", 1, lease(2), "d" * 32, lease(1), 2, 1, "DROP", 0, "energycells", 5)
    s.send(1.5, P2, "CARGOREQ", 1, lease(2), "d" * 32, lease(1), 2, 1, "DROP", 0, "energycells", 6)
    s.send(1.5, P2, "CARGOREQ", 1, lease(2), "e" * 32, lease(1), 2, 1, "PICKUP", 0, "energycells", 6)
    s.send(1.5, P2, "CARGOREQ", 1, lease(2), "f" * 32, lease(9), 2, 1, "PICKUP", 11, "energycells", 6)
    s.send(1.5, P2, "CARGOREQ", 1, lease(2), "c" * 32, lease(1), 1, 1, "PICKUP", 11, "energycells", 6)
    s.send(1.55, P2, "CDOCK", 1, lease(2), 2, 1, 1, "STORED", 17).send(1.55, P1, "CDOCK", 1, lease(1), 1, 1, 1, "APPROACH", 17)
    s.send(1.6, P1, "NHP", 1, lease(1), lease(1), 7, 1, 1, SECTOR, "ARG-123", "ship_arg_s_fighter_01_a_macro", "argon", 80, 50, 1)
    s.send(1.6, P2, "HACK", 1, lease(2), 2, 2)
    s.send(1.6, P2, "NCLAIM", 1, lease(2), lease(1), 7, 1, 2, 2, 1, 3, "weapon_gen_s_laser_01_mk1_macro:10.000")
    for t in (1.65, 1.7, 1.8, 2.0):
        s.snap(t)
    s.send(2.0, P1, "NACK", 1, lease(1), lease(1), lease(2), 7, 1, 2, 2, 2)
    s.send(2.0, P1, "CARGODONE", 1, lease(1), "d" * 32, "OK", 4).snap(2.1)
    s.send(2.1, P2, "CARGOREQ", 1, lease(2), "d" * 32, lease(1), 2, 1, "DROP", 0, "energycells", 5).snap(2.2)
    s.send(2.3, P2, "UINTEREST", 1, lease(2), 2, SECTOR2).snap(2.4).expire(40).snap(40)

    # XB bundles: only an XBCAP peer (right lease, address and version) receives them; a
    # dense NPC sector fills several bundles per tick, NHP/IDENT forward new sequences only.
    for capable in (("p2",), ("p1", "p2")):
        s = case(f"universe: XB bundles for {'+'.join(capable)}, dense NPC health/identity/motion", "universe")
        s.hello(0).hello(0, "p2", P2, 2)
        s.send(.01, P1, "WORLD", 1, lease(1), 1, *world())
        s.send(.02, P1, "CREW", 1, lease(1), 1, 1, 1).send(.02, P2, "CREW", 1, lease(2), 1, 2, 2)
        s.send(.03, P2, "UINTEREST", 1, lease(2), 1, SECTOR)
        ids = list(range(1, 41))
        rows = [ship_row(i) for i in ids]
        digest = zlib.adler32("\n".join(rows).encode())
        s.send(.04, P1, "UBEGIN", 1, lease(1), lease(1), 1, SECTOR, len(rows), digest)
        for i, row in enumerate(rows, 1):
            s.send(.04, P1, "UOBJECT", 1, lease(1), lease(1), 1, i, row)
        s.send(.04, P1, "UCOMMIT", 1, lease(1), lease(1), 1)
        s.send(.05, P2, "XBCAP", 1, lease(1), 1)          # wrong lease for P2
        s.send(.05, P2, "XBCAP", 1, lease(2), 2)          # unknown version
        s.send(.05, P1, "XBCAP", 1, lease(2), 1)          # wrong address
        s.snap(.06)
        for slot in capable:
            s.send(.07, P1 if slot == "p1" else P2, "XBCAP", 1, lease(int(slot[1])), 1)
        seq = 0
        for step in range(4):
            t = .1 + step * .1
            for i in ids:
                seq += 1
                s.send(t, P1, "NHP", 1, lease(1), lease(1), i, 1, seq, SECTOR, f"ARG-{i}",
                       "ship_arg_s_fighter_01_a_macro", "argon", 100 - step if i % 3 == 0 or step == 0 else 100, 50, 1)
                s.send(t, P1, "IDENT", 1, lease(1), lease(1), seq, "N", i, f"ARG-{i}", "4142", 1, i % 2)
                s.send(t, P1, "UMOVE", 1, lease(1), lease(1), i, seq, SECTOR, 10 + t, 1.25 * i, 2, 3, 0, 0.5, 0,
                       100, 0, 0, 0, 0, 0)
            for k in range(3):
                s.snap(t + .01 + k * .05)
        s.send(.6, P2, "UACK", 1, lease(2), lease(1), 1, 0)
        for t in (.62, .7, .8, 1.0, 1.4, 2.5):
            s.snap(t)

    s = case("universe: build label must start with world1-", "universe")
    s.commands[0]["build"] = "test"
    s = case("base: invalid constructor token", "base")
    s.commands[0]["token"] = "A" * 32
    return [x.dict() for x in out]


# ---------------------------------------------------------------- randomized


MUTATIONS = ["", "-1", "0", "1e309", "nan", "-inf", " 1", "1_0", "0007", "2147483648", "99999999999999999999",
             "x" * 97, "\xe9", "1.5", "+1", "-0", "p3", "m3", "0x1", "|", "1e-400", "inf"]


class Fuzzer:
    def __init__(self, rng, level):
        self.r = rng
        self.level = level
        self.s = Scenario("", level)
        self.t = 0.0
        self.seq = {}
        self.slots = {"p1": (P1, 1), "p2": (P2, 2)}

    def next_seq(self, key):
        r = self.r
        value = self.seq.get(key, 0)
        roll = r.random()
        if roll < .08 and value > 1:
            return value - r.randint(1, min(value, 3))                    # reordered / stale
        if roll < .12:
            return value                                                  # duplicate
        value += 1 if roll < .9 else r.randint(2, 50)
        self.seq[key] = value
        return value

    def lease(self, slot):
        # Usually the slot's likely lease; sometimes another session's lease.
        r = self.r
        if r.random() < .9:
            return lease(1 if slot == "p1" else 2) if r.random() < .8 else lease(r.randint(1, 4))
        return r.choice([lease(9), "f" * 32, "x"])

    def addr(self, slot):
        r = self.r
        if r.random() < .95:
            return self.slots[slot][0]
        return r.choice([P3, P1B, OTHER_IP, P1 if slot == "p2" else P2])

    def num(self, lo, hi, digits=3):
        return round(self.r.uniform(lo, hi), self.r.choice([0, 1, digits, 9]))

    def pose(self, limit=1000):
        return [self.num(-limit, limit) for _ in range(3)] + [self.num(-3.1, 3.1) for _ in range(3)]

    def ship(self):
        return self.r.choice([1, 2, 1, 2, 0, 3])

    def packet(self, slot):
        """One (mostly) well-formed packet for this level's vocabulary."""
        r, L, lvl = self.r, self.lease(slot), self.level
        kinds = ["HELLO", "STATE", "IDLE", "PING", "RESUME", "XBCAP"]
        if lvl != "base":
            kinds += ["WORLD", "ACTION", "DONE", "FIRE", "BODY", "WALK", "WALK2", "SUITKILL"]
        if lvl not in ("base", "bubble"):
            kinds += ["ECON*", "ORDER", "ORDER_DONE", "DOCKSTATE"]
        if lvl in ("coop", "dock", "universe"):
            kinds += ["CREW", "CREW", "NAV", "NAV2", "NAV2", "FIRE2", "HEALTH", "HACK", "HACK", "HSTALE", "SHOT", "BODY2",
                      "PERF", "COOPORDER", "QUEST", "BOLT", "BOLT", "BEND", "MSTATE*", "MREQ"]
        if lvl in ("coop", "dock", "universe"):
            kinds += ["FLOW_HEALTH", "FLOW_BOLT"]
        if lvl in ("dock", "universe"):
            kinds += ["CDOCK", "CDOCK"]
        if lvl == "universe":
            kinds += ["BASELINE*", "UBEGIN", "UOBJECT", "UCOMMIT", "UACK", "UINTEREST", "UPOS", "UMOVE", "UMOVE",
                      "CARGOREQ", "CARGODONE", "CINV", "IDENT", "NHP", "NCLAIM", "NACK",
                      "FLOW_DELIVERY", "FLOW_DELIVERY", "FLOW_CARGO", "FLOW_NPC"]
        kind = r.choice(kinds)
        if kind.startswith("FLOW_"):
            return getattr(self, kind.lower())()
        ship = self.ship()
        epoch = r.choice([1, 1, 2, 3])
        gen = r.choice([1, 2, 3, 4, 5])
        host = lease(1) if r.random() < .9 else lease(2)
        req = f"{r.randint(1, 6):x}" * 32
        if kind == "HELLO":
            n = self.slots[slot][1] if r.random() < .85 else r.randint(1, 9)
            return [["HELLO", 1, TOKEN, self.s.build, SECTOR if r.random() < .95 else SECTOR2, BUBBLE, slot, nonce(n)]]
        if kind == "RESUME":
            n = self.slots[slot][1]
            new = r.randint(10, 14)
            fields = ["RESUME", 1, TOKEN, self.s.build, SECTOR, BUBBLE, slot, nonce(n if r.random() < .7 else new), L, nonce(new)]
            if r.random() < .5:
                self.slots[slot] = (r.choice([self.slots[slot][0], P3, P1B]), new)
                fields[7] = nonce(n)
            return [fields]
        if kind == "PING":
            return [["PING", 1, TOKEN, nonce(r.randint(1, 9))]]
        if kind == "XBCAP":
            return [["XBCAP", 1, L, r.choice(["1", "1", "1", "2", "01"])]]
        if kind == "STATE":
            return [["STATE", 1, L, self.next_seq(("state", slot)), *self.pose(60000)]]
        if kind == "IDLE":
            return [["IDLE", 1, L, self.next_seq(("state", slot))]]
        if kind == "WORLD":
            values = world(hull1=r.choice([100, 50, 1, 0.5, 1.01]), hull2=r.choice([100, 30, 1]))
            return [["WORLD", 1, L, self.next_seq("world"), *values]]
        if kind == "ACTION":
            return [["ACTION", 1, TOKEN, slot, req, r.choice(["TRADE", "PICKUP"]), "energycells"]]
        if kind == "DONE":
            return [["DONE", 1, L, req, r.choice(["OK", "REJECTED", "BUSY", "CANCELLED"])]]
        if kind == "FIRE":
            return [["FIRE", 1, L, self.next_seq(("fire", slot))]]
        if kind == "BODY":
            return [["BODY", 1, L, self.next_seq(("body", slot)), r.choice(["ship", "foot", "suit"]), r.choice([0, 0, 1]),
                     r.choice([0, 1, 2, 3]), *self.pose()]]
        if kind == "WALK":
            return [["WALK", 1, L, self.next_seq(("walk", slot)), r.randint(0, 5), *self.pose(100)]]
        if kind == "WALK2":
            state = r.choice([0, 1, 1, 1, 2])
            kind2 = r.choice(["ship", "station", "none", "ship", "deck"])
            ship = r.choice([0, 1, 2, 3]) if kind2 == "ship" else r.choice([0, 0, 1])
            context = ["-", "-", "-"] if kind2 != "station" or r.random() < .1 else [SECTOR, "ABC-123", "station_macro"]
            return [["WALK2", 1, L, self.next_seq(("walk2", slot)), state, kind2, ship, *context,
                     r.choice(["room_a_macro", "-", "bad room"]), *self.pose(120000)[:3], self.num(-400, 400),
                     self.num(-1, 110), self.t + r.choice([0, 0, 2e9]), r.choice([0, 1, 2])]]
        if kind == "SUITKILL":
            return [["SUITKILL", 1, L, r.choice([0, 1, 2, 3])]]
        if kind == "ECON*":
            payload = econ_payload() if r.random() < .8 else "M,1,2,3"
            size = r.choice([60, 120, 500])
            parts = [payload[i:i + size] for i in range(0, len(payload), size)]
            rev = self.next_seq("econ")
            packets = [["ECON", 1, L, rev, i, len(parts), p] for i, p in enumerate(parts, 1)]
            r.shuffle(packets)
            return packets
        if kind == "ORDER":
            return [["ORDER", 1, L, req, host, r.choice(["m1", "m2"]), r.choice(["energycells,-5,10", "water,3,12;food_rations,1,1"])]]
        if kind == "ORDER_DONE":
            return [["ORDER_DONE", 1, L, req, r.choice(["OK", "STOCK", "UNCERTAIN", "PENDING"])]]
        if kind == "DOCKSTATE":
            return [["DOCKSTATE", 1, L, self.next_seq(("dock", slot)), r.choice(["none", "m1", "m2"])]]
        if kind == "CREW":
            c = r.choice([0, 1, 2])
            return [["CREW", 1, L, self.next_seq(("crew", slot)), c, c if r.random() < .85 else r.choice([0, 1, 2])]]
        if kind in ("NAV", "NAV2"):
            seq = self.next_seq(("nav", ship))
            fields = [kind, 1, L, ship, epoch, seq, SECTOR if r.random() < .9 else SECTOR2, *self.pose(), r.choice([0, 1, 2]), seq]
            if kind == "NAV2":
                fields += [self.t + r.choice([0, 0, -1, 5]), *self.pose(100)]
            return [fields]
        if kind == "FIRE2":
            seq = self.next_seq(("fire2", ship))
            return [["FIRE2", 1, L, ship, epoch, seq, self.t + seq / 1000, r.choice([1, 2, 4, 0, 5]), r.choice([1, 3]), seq, seq // 2]]
        if kind == "HEALTH":
            return [["HEALTH", 1, L, ship, gen, self.next_seq(("health", ship)), self.num(0, 100), self.num(0, 100)]]
        if kind in ("HACK", "HSTALE"):
            return [[kind, 1, L, ship, gen]]
        if kind == "SHOT":
            return [["SHOT", 1, L, ship, gen, r.choice([1, 2, 3, 4, 0]), r.choice([0, 1, 2, 3]), self.next_seq(("shot", ship)),
                     r.randint(0, 9), r.choice(["weapon_gen_s_laser_01_mk1_macro:12.500", "bad"])]]
        if kind == "BODY2":
            return [["BODY2", 1, L, self.next_seq(("body", slot)), r.choice(["ship", "foot", "suit"]), r.choice([0, 1]),
                     r.choice([0, 1, 2]), SECTOR, *self.pose()]]
        if kind == "PERF":
            return [["PERF", 1, L, self.next_seq(("perf", slot)), self.num(0, 120), self.num(0, 50), self.num(0, 100),
                     r.choice(["ship", "paused", "sleep"]), SECTOR]]
        if kind == "COOPORDER":
            return [["COOPORDER", 1, L, req, host, ship, epoch, r.choice(["m1", "m2"]), "energycells,-5,10"]]
        if kind == "QUEST":
            stage = r.choice([1, 2, 3])
            delivered = {1: r.randint(0, 19), 2: 20, 3: 20}[stage]
            return [["QUEST", 1, L, self.next_seq("quest"), stage, delivered, {3: 2}.get(stage, r.randint(0, 1)), int(stage == 3)]]
        if kind in ("BOLT", "BEND"):
            ident = self.next_seq(("bolt", ship))
            t = self.t + r.choice([0, -.5, .05, -2])
            if kind == "BEND":
                return [["BEND", 1, L, ship, epoch, gen, ident, round(t, 4)]]
            return [["BOLT", 1, L, ship, epoch, gen, ident, round(t, 4), SECTOR, r.choice([1, 2, 3]), *self.pose(), 400, 0, 0,
                     r.choice([0.5, 1.5, 3])]]
        if kind == "MSTATE*":
            ident = r.randint(1, 3)
            payload = mission_payload(*r.choice([("O", 0), ("A", 1), ("A", 3), ("C", 5), ("X", 2)]))
            size = r.choice([40, 200, 500])
            parts = [payload[i:i + size] for i in range(0, len(payload), size)]
            digest = zlib.adler32(payload.encode())
            rev = self.next_seq(("mission", ident))
            packets = [["MSTATE", 1, L, host, ident, rev, i, len(parts), digest, p] for i, p in enumerate(parts, 1)]
            r.shuffle(packets)
            return packets
        if kind == "MREQ":
            return [["MREQ", 1, L, host, r.randint(1, 3), r.randint(1, 4), r.choice(["ACCEPT", "ABORT"])]]
        if kind == "CDOCK":
            mode = r.choice(["APPROACH", "EXTERNAL", "STORED", "FREE", "UNKNOWN"])
            return [["CDOCK", 1, L, ship, epoch, self.next_seq(("cdock", ship)), mode,
                     0 if mode in ("FREE", "UNKNOWN") and r.random() < .9 else r.choice([5, 17])]]
        if kind == "BASELINE*":
            rev = self.next_seq("baseline")
            ids = sorted(r.sample(range(1, 40), r.randint(0, 6)))
            rows = [ship_row(i, x=r.randint(1, 9)) for i in ids]
            sector = SECTOR if r.random() < .8 else SECTOR2
            digest = zlib.adler32("\n".join(rows).encode())
            packets = [["UBEGIN", 1, L, host, rev, sector, len(rows), digest]]
            packets += [["UOBJECT", 1, L, host, rev, i, row] for i, row in enumerate(rows, 1)]
            packets += [["UCOMMIT", 1, L, host, rev]]
            if r.random() < .3:
                r.shuffle(packets)
            return packets
        if kind == "UBEGIN":
            return [["UBEGIN", 1, L, host, self.seq.get("baseline", 1), SECTOR, r.randint(0, 3), r.randint(0, 2 ** 32)]]
        if kind == "UOBJECT":
            return [["UOBJECT", 1, L, host, self.seq.get("baseline", 1), r.randint(1, 3), ship_row(r.randint(1, 40))]]
        if kind == "UCOMMIT":
            return [["UCOMMIT", 1, L, host, self.seq.get("baseline", 1)]]
        if kind == "UACK":
            return [["UACK", 1, L, host, self.seq.get("baseline", 1), r.choice([-1, 0, 1, 2, 3, 7])]]
        if kind == "UINTEREST":
            return [["UINTEREST", 1, L, self.next_seq(("interest", slot)), r.choice([SECTOR, SECTOR, SECTOR2])]]
        if kind in ("UPOS", "UMOVE"):
            fields = [kind, 1, L, host, r.randint(1, 40), self.next_seq(("motion",)), SECTOR]
            if kind == "UMOVE":
                fields.append(self.t + r.choice([0, -1]))
            fields += self.pose()
            if kind == "UMOVE":
                fields += self.pose(100)
            return [fields]
        if kind == "CARGOREQ":
            verb = r.choice(["DROP", "PICKUP"])
            return [["CARGOREQ", 1, L, req, host, ship, epoch, verb, 0 if verb == "DROP" else r.choice([0, 11]),
                     "energycells", r.choice([5, 0, 6])]]
        if kind == "CARGODONE":
            return [["CARGODONE", 1, L, req, r.choice(["OK", "CAPACITY", "PENDING"]), r.randint(0, 9)]]
        if kind == "CINV":
            return [["CINV", 1, L, L, self.next_seq("cinv"), r.choice(["energycells", "water", "Bad"]), r.randint(0, 30), 5]]
        if kind == "IDENT":
            scope = r.choice(["P", "F", "N", "X"])
            return [["IDENT", 1, L, L, self.next_seq(("ident", scope)), scope, r.randint(1, 40), "ARG-1",
                     r.choice(["4142", "0a", "e282ac", "c0"]), r.choice([0, 1]), r.choice([0, 1])]]
        if kind == "NHP":
            alive = r.choice([1, 1, 0])
            return [["NHP", 1, L, host, r.randint(1, 40), r.choice([1, 2]), self.next_seq("nhp"), SECTOR, "ARG-1",
                     "ship_arg_s_fighter_01_a_macro", "argon", 0 if not alive else self.num(1, 100), self.num(0, 100), alive]]
        if kind == "NCLAIM":
            return [["NCLAIM", 1, L, host, r.randint(1, 40), r.choice([1, 2]), ship, gen, self.next_seq("nclaim"),
                     r.randint(0, 5), "weapon_gen_s_laser_01_mk1_macro:10.000"]]
        if kind == "NACK":
            return [["NACK", 1, L, host, lease(2), r.randint(1, 40), r.choice([1, 2]), ship, gen, r.randint(0, 5)]]
        raise AssertionError(kind)

    # Cooperative multi-packet flows: (slot, fields) items and "SNAP" ticks. They drive
    # the deeper state machines (baseline delivery, health generations, cargo, NPC
    # claims, projectile forwarding) that independent random packets rarely reach.

    def baseline(self, ids, sector=SECTOR):
        self.seq["baseline"] = self.seq.get("baseline", 0) + 1
        rev = self.seq["baseline"]
        rows = [ship_row(i, x=self.r.randint(1, 9)) for i in ids]
        digest = zlib.adler32("\n".join(rows).encode())
        items = [("p1", ["UBEGIN", 1, lease(1), lease(1), rev, sector, len(rows), digest])]
        items += [("p1", ["UOBJECT", 1, lease(1), lease(1), rev, i, row]) for i, row in enumerate(rows, 1)]
        items += [("p1", ["UCOMMIT", 1, lease(1), lease(1), rev])]
        return rev, items

    def flow_delivery(self):
        r = self.r
        ids = sorted(r.sample(range(1, 40), r.randint(1, 15)))
        rev, items = self.baseline(ids)
        items = [("p2", ["UINTEREST", 1, lease(2), self.next_seq(("interest", "p2")), SECTOR])] + items + ["SNAP"]
        for index in [0] + list(range(1, len(ids) + 1)) + [len(ids) + 1]:
            if r.random() < .9:
                items.append(("p2", ["UACK", 1, lease(2), lease(1), rev, index]))
            if r.random() < .3:
                items.append("SNAP")
        items.append("SNAP")
        for entity in ids[:3]:
            items.append(("p1", ["UMOVE", 1, lease(1), lease(1), entity, self.next_seq(("motion",)), SECTOR, self.t + 1,
                                 *self.pose(), *self.pose(100)]))
        return items + ["SNAP"]

    def flow_health(self):
        r = self.r
        g = r.randint(1, 4)
        items = [("p1", ["WORLD", 1, lease(1), self.next_seq("world"), *world()]),
                 ("p2", ["CREW", 1, lease(2), self.next_seq(("crew", "p2")), 2, 2]), "SNAP"]
        for gen in range(g, g + 4):
            items.append(("p2", ["HACK", 1, lease(2), 2, gen]))
            items.append(("p2", ["HEALTH", 1, lease(2), 2, gen, self.next_seq(("health", 2)), self.num(1, 100), self.num(0, 100)]))
        items.append("SNAP")
        for gen in range(g, g + 4):
            items.append(("p2", ["SHOT", 1, lease(2), 2, gen, r.choice([1, 3]), r.choice([0, 1, 2, 3]),
                                 self.next_seq(("shot", 2)), r.randint(1, 5), "weapon_gen_s_laser_01_mk1_macro:12.500"]))
            items.append(("p1", ["SHOT", 1, lease(1), 1, gen, 2, gen, self.next_seq(("shot", 1)), r.randint(1, 5),
                                 "weapon_gen_s_laser_01_mk1_macro:12.500"]))
        return items + ["SNAP"]

    def flow_bolt(self):
        r = self.r
        epoch = r.choice([1, 2])
        source = round(self.t + 100, 3)
        items = [("p1", ["WORLD", 1, lease(1), self.next_seq("world"), *world()]),
                 ("p1", ["CREW", 1, lease(1), self.next_seq(("crew", "p1")), 1, 1]),
                 ("p1", ["NAV2", 1, lease(1), 1, epoch, self.next_seq(("nav", 1)), SECTOR, *self.pose(), 0, 1, source,
                         *self.pose(100)])]
        for gen in range(1, 7):
            ident = self.next_seq(("bolt", 1))
            items.append(("p1", ["BOLT", 1, lease(1), 1, epoch, gen, ident, source, SECTOR, r.choice([1, 2, 3]), *self.pose(),
                                 400, 0, 0, r.choice([0.5, 1.5, 3])]))
            if r.random() < .5:
                items.append(("p1", ["BEND", 1, lease(1), 1, epoch, gen, ident, source + .01]))
        return items + ["SNAP", "SNAP", "SNAP"]

    def flow_cargo(self):
        r = self.r
        epoch = r.choice([1, 2])
        req = f"{r.randint(7, 15):x}" * 32
        verb, obj = r.choice([("DROP", 0), ("PICKUP", 11)])
        request = ["CARGOREQ", 1, lease(2), req, lease(1), 2, epoch, verb, obj, "energycells", 5]
        items = [("p1", ["WORLD", 1, lease(1), self.next_seq("world"), *world()]),
                 ("p2", ["CREW", 1, lease(2), self.next_seq(("crew", "p2")), 2, 2]),
                 ("p2", request), "SNAP", ("p2", request), "SNAP"]
        if r.random() < .8:
            items.append(("p1", ["CARGODONE", 1, lease(1), req, r.choice(["OK", "CAPACITY", "STALE"]), r.randint(0, 9)]))
        items += [("p2", request), "SNAP", ("p1", ["CINV", 1, lease(1), lease(1), self.next_seq("cinv"), "energycells", 3, 4]), "SNAP"]
        return items

    def flow_npc(self):
        r = self.r
        rev, items = self.baseline([3, 7])
        items = [("p2", ["UINTEREST", 1, lease(2), self.next_seq(("interest", "p2")), SECTOR])] + items
        items += [("p1", ["WORLD", 1, lease(1), self.next_seq("world"), *world()]),
                  ("p2", ["CREW", 1, lease(2), self.next_seq(("crew", "p2")), 2, 2])]
        for gen in range(1, 5):
            items.append(("p2", ["HACK", 1, lease(2), 2, gen]))
        npc_gen = r.choice([1, 2])
        items.append(("p1", ["NHP", 1, lease(1), lease(1), 7, npc_gen, self.next_seq("nhp"), SECTOR, "ARG-7",
                             "ship_arg_s_fighter_01_a_macro", "argon", self.num(1, 100), self.num(0, 100), 1]))
        for gen in range(1, 5):
            items.append(("p2", ["NCLAIM", 1, lease(2), lease(1), 7, npc_gen, 2, gen, self.next_seq("nclaim"), r.randint(1, 4),
                                 "weapon_gen_s_laser_01_mk1_macro:10.000"]))
        items += ["SNAP", ("p1", ["NACK", 1, lease(1), lease(1), lease(2), 7, npc_gen, 2, r.randint(1, 4), r.randint(0, 3)]),
                  "SNAP"]
        items.append(("p1", ["IDENT", 1, lease(1), lease(1), self.next_seq(("ident", "N")), "N", 7, "ARG-7", "4142", 1, 0]))
        return items + ["SNAP"]

    def mutate(self, fields):
        r = self.r
        fields = [str(f) for f in fields]
        roll = r.random()
        if roll < .5:
            i = r.randrange(1, len(fields))
            fields[i] = r.choice(MUTATIONS)
        elif roll < .6:
            fields.insert(r.randrange(1, len(fields) + 1), r.choice(MUTATIONS))
        elif roll < .7:
            del fields[r.randrange(1, len(fields))]
        elif roll < .8:
            fields[1] = r.choice(["2", "", "01"])
        elif roll < .9:
            return ("|".join(fields) + "|" + "9" * 800).encode()
        else:
            data = bytearray("|".join(fields).encode("latin-1"))
            data[r.randrange(len(data))] = r.randrange(256)
            return bytes(data)
        return "|".join(fields).encode("latin-1")

    def run(self, steps):
        r, s = self.r, self.s
        s.hello(0, "p1", P1, 1)
        if r.random() < .9:
            s.hello(0.001, "p2", P2, 2)
        for _ in range(steps):
            roll = r.random()
            if roll < .02:
                self.t += r.choice([29.99, 30.0, 31.0, 15.1])            # lease / staging boundaries
            elif roll < .1:
                self.t += r.choice([0.5, 0.75, 0.76, 1.0, 2.0, 5.0, 10.0])
            else:
                self.t += r.choice([0.0, 0.001, 0.01, 0.04, 0.05, 0.1])
            self.t = round(self.t, 6)
            slot = "p1" if r.random() < .55 else "p2"
            for item in self.packet(slot):
                if item == "SNAP":
                    s.snap(self.t)
                    self.t = round(self.t + r.choice([0.01, 0.05, 0.26]), 6)
                    continue
                sender, fields = item if isinstance(item, tuple) else (slot, item)
                data = self.mutate(fields) if r.random() < .08 else "|".join(str(f) for f in fields).encode("latin-1")
                s.recv(self.t, self.addr(sender), data)
            if r.random() < .45:
                s.snap(self.t)
            if r.random() < .01:
                s.expire(self.t)
        s.snap(self.t + 0.05)
        return s


def randomized(count, seed):
    rng = random.Random(seed)
    out = []
    for i in range(count):
        level = LEVELS[i % len(LEVELS)]
        fuzz = Fuzzer(random.Random(rng.getrandbits(64)), level)
        s = fuzz.run(rng.choice([40, 80, 160, 320]))
        s.name = f"random-{level}-{i}"
        out.append(s.dict())
    return out


# ---------------------------------------------------------------- numbers


FLOAT_ALPHABET = "0123456789._eE+- \t\n\x0b\x0c\r\x1cinfatyINFATY"


def numbers(seed):
    r = random.Random(seed)
    texts = ["0", "-0", "1", ".5", "5.", "1e5", "1E-5", "1_0", "1__0", "_1", "1_", "1e1_0", "1e_1", "inf", "-Infinity",
             "nan", "-nan", "+inf", "infinit", "1e400", "-1e400", "1e-400", "4.9e-324", "2.4703282292062327e-324",
             "2.4703282292062328e-324", "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308",
             "0.1", "123456789012345678901234567890", " 1 ", "\x0b1", "\x1c1", "0x10", "", " ", ".", "e5", "1e"]
    texts += ["".join(r.choice(FLOAT_ALPHABET) for _ in range(r.randint(1, 8))) for _ in range(3000)]
    texts += [repr(r.uniform(-1e6, 1e6)) for _ in range(1000)]
    values = [0.0, -0.0, 0.125, 0.0005, 0.0015, 0.0025, 2.675, 1.0005, 123456789.5, 1234567885.0, 999999999.5,
              1e16, 1e22, 1e-5, 1e-4, 5e-324, 1.7976931348623157e308, float("inf"), float("-inf"), float("nan")]
    values += [r.uniform(-1e6, 1e6) for _ in range(2000)]
    values += [round(r.uniform(-1000, 1000), r.randint(0, 4)) + r.choice([0, 0.0005, 0.5]) for _ in range(2000)]
    bits = [struct.pack(">d", v).hex() for v in values]
    bits += [f"{r.getrandbits(64):016x}" for _ in range(2000)]
    commands = [{"op": "float", "text": t} for t in texts] + [{"op": "fmt", "bits": b} for b in bits]
    chunks = [commands[i:i + 500] for i in range(0, len(commands), 500)]
    return [{"name": f"numbers-{i}", "level": None, "commands": c} for i, c in enumerate(chunks)]
