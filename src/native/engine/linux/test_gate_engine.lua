-- Test only (test_gate_engine.c): the X4 UI globals that tools/dev/native_gate_probe.lua
-- uses in a snapshot boot with the clock digest, emulated without X4. The ffi.C
-- functions it calls (GetSaveFolderPath, IsStartmenu, ...) are exported by the
-- emulator itself. Not shipped.
local ffi=require('ffi')
local bit=require('bit')
ffi.cdef('void engine_load_game(const char *);')
local out=io.stdout
function DebugError(text) out:write(tostring(text),'\n');out:flush() end
function SetScript(name,handler) assert(name=='onUpdate');ENGINE_ON_UPDATE=handler end
function RegisterEvent() end
function AddUITriggeredEvent() end
function GetSaveList() return {{filename='save_009'}} end
function LoadGame(name) assert(name=='save_009');ffi.C.engine_load_game(name) end
-- One UI frame: the probe's onUpdate, as X4 calls it.
function ENGINE_FRAME()
 if ENGINE_ON_UPDATE then ENGINE_ON_UPDATE() end
end
-- Independent pure-Lua SHA-256 for X4LAN_SHA256 (the probe asserts gate.sha256 equals it).
local band,bor,bxor,bnot,rshift,lshift,ror,tobit,tohex=bit.band,bit.bor,bit.bxor,bit.bnot,bit.rshift,bit.lshift,bit.ror,bit.tobit,bit.tohex
local K={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
 0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
 0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
 0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}
local function digest(message)
 local length=#message
 assert(length<0x10000000,'test SHA-256: message too long')
 local bits=length*8
 message=message..'\128'..string.rep('\0',(55-length)%64)..string.char(0,0,0,0,
  band(rshift(bits,24),255),band(rshift(bits,16),255),band(rshift(bits,8),255),band(bits,255))
 local h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}
 local w={}
 for at=1,#message,64 do
  for i=0,15 do
   local a,b,c,d=message:byte(at+i*4,at+i*4+3)
   w[i]=bor(lshift(a,24),lshift(b,16),lshift(c,8),d)
  end
  for i=16,63 do
   local s0=bxor(ror(w[i-15],7),ror(w[i-15],18),rshift(w[i-15],3))
   local s1=bxor(ror(w[i-2],17),ror(w[i-2],19),rshift(w[i-2],10))
   w[i]=tobit(w[i-16]+s0+w[i-7]+s1)
  end
  local a,b,c,d,e,f,g,hh=h[1],h[2],h[3],h[4],h[5],h[6],h[7],h[8]
  for i=0,63 do
   local t1=tobit(hh+bxor(ror(e,6),ror(e,11),ror(e,25))+bxor(band(e,f),band(bnot(e),g))+K[i+1]+w[i])
   local t2=tobit(bxor(ror(a,2),ror(a,13),ror(a,22))+bxor(band(a,b),band(a,c),band(b,c)))
   hh,g,f,e,d,c,b,a=g,f,e,tobit(d+t1),c,b,a,tobit(t1+t2)
  end
  h[1],h[2],h[3],h[4]=tobit(h[1]+a),tobit(h[2]+b),tobit(h[3]+c),tobit(h[4]+d)
  h[5],h[6],h[7],h[8]=tobit(h[5]+e),tobit(h[6]+f),tobit(h[7]+g),tobit(h[8]+hh)
 end
 local parts={}
 for i=1,8 do parts[i]=tohex(h[i],8) end
 return table.concat(parts)
end
assert(digest('abc')=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
assert(digest('')=='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')
X4LAN_SHA256={digest=digest}
