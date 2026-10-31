// Test only (never shipped): runs the Jolt code embedded in the owned Linux X4 9.00
// executable inside an offline test process, so jolt_determinism_scenario.cpp can drive
// X4's own Jolt instead of ours and the two results can be compared bit for bit.
// The executable is read from the user's installation at run time (size and SHA-256
// pinned); nothing of it is stored in this repository. Steps: map each PT_LOAD segment
// at its fixed address (the test binary is linked above 0x10000000 so the range
// 0x400000-0x7bb7000 is free), give every thread the executable's thread-local block,
// resolve the executable's imports against this process (libc, libm, libstdc++), point
// its operator new/delete at this process's, then run its merged static initializer
// (which fills Jolt's file-local tables such as CylinderShape's cCylinderTopFace). The
// scenario's references to Jolt functions and data are bound to the executable's
// addresses by a linker script generated from its .dynsym. The scenario ends with
// _Exit: the executable's exit handlers belong to the game process.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Geometry/AABox.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <new>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include "lockstep_sha256.h"

namespace {
constexpr unsigned long long kSize = 60459280ull;
constexpr const char *kSha = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
// Static initializers inside the merged startup function, by address in the pinned build.
constexpr uintptr_t kInitFixedToWorld = 0x933970, kInitUnitSphere = 0x9340e0, kMergedInit = 0x9a2d70;
constexpr uintptr_t kCollisionGroupInvalid = 0x44fc6e0, kQuadTreeInvalidBounds = 0x4521b00;
constexpr size_t kTlsSize = 0x2740; // PT_TLS memsz of the pinned build, 16-byte aligned

[[noreturn]] void Fail(const char *what, const char *detail = "") {
    std::fprintf(stderr, "x4 loader: %s %s\n", what, detail);
    std::exit(70);
}
template <class T> T At(const std::vector<unsigned char> &f, size_t o) { T v; std::memcpy(&v, f.data() + o, sizeof(T)); return v; }
void MissingImport() { Fail("called an import this test does not provide"); }

// The executable's Jolt functions (from .dynsym) for X4_JOLT_REDIRECT.
struct Function { std::string name; uint64_t value, size; };
std::vector<Function> gJoltFunctions;
// Version checks stay the executable's: our build carries the determinism feature bit.
const char *const kKeep[] = {"_ZN3JPH21RegisterTypesInternalEm", "_ZN3JPH27VerifyJoltVersionIDInternalEm"};

// Entry patches: 14-byte absolute jump in place, or a 5-byte jump to a near thunk for
// entries of 5..13 bytes; shorter entries (empty stubs) are left (returns false).
unsigned char *gThunks; size_t gThunksUsed;
bool JumpTo(uint64_t at, uint64_t size, const void *target) {
    unsigned char *entry = reinterpret_cast<unsigned char *>(at);
    uint64_t to = reinterpret_cast<uint64_t>(target);
    if (size >= 14) {
        entry[0] = 0xff; entry[1] = 0x25; std::memset(entry + 2, 0, 4); std::memcpy(entry + 6, &to, 8);
        return true;
    }
    if (size < 5) return false;
    if (!gThunks) {
        gThunks = static_cast<unsigned char *>(mmap(reinterpret_cast<void *>(0x300000), 0x10000, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0));
        if (gThunks != reinterpret_cast<unsigned char *>(0x300000)) Fail("thunk page");
    }
    if (gThunksUsed + 16 > 0x10000) Fail("thunk page full");
    unsigned char *t = gThunks + gThunksUsed; gThunksUsed += 16;
    t[0] = 0xff; t[1] = 0x25; std::memset(t + 2, 0, 4); std::memcpy(t + 6, &to, 8);
    int32_t rel = int32_t(reinterpret_cast<intptr_t>(t) - reinterpret_cast<intptr_t>(entry + 5));
    entry[0] = 0xe9; std::memcpy(entry + 1, &rel, 4);
    return true;
}

// The executable defines its own operator new/delete. In the game they interpose the C++
// runtime's; in this process they do not, so memory allocated by one and released by the
// other would mix heaps. Point the executable's definitions at this process's ones.
void UseProcessAllocator(const std::vector<Function> &operators) {
    unsigned bound = 0;
    for (const Function &f : operators) {
        if (f.name == "_ZnwmPv") continue; // placement new
        void *target = dlsym(RTLD_DEFAULT, f.name.c_str());
        if (!target || !JumpTo(f.value, f.size, target)) Fail("operator without a process twin", f.name.c_str());
        ++bound;
    }
    if (bound < 16) Fail("operator new/delete set incomplete");
}

// Point the executable's Jolt function entries at our build's twins (namespace QPH), the
// offline model of the in-game redirect.
void Redirect(const char *filter) {
    unsigned redirected = 0, missing = 0, small = 0, kept = 0;
    std::string fragments = filter;
    for (const Function &f : gJoltFunctions) {
        bool keep = false;
        for (const char *k : kKeep) keep |= f.name == k;
        if (keep) { ++kept; continue; }
        if (fragments != "all") {
            bool hit = false; size_t start = 0;
            while (start <= fragments.size()) {
                size_t end = fragments.find(',', start); if (end == std::string::npos) end = fragments.size();
                std::string part = fragments.substr(start, end - start);
                if (!part.empty() && f.name.find(part) != std::string::npos) hit = true;
                start = end + 1;
            }
            if (!hit) continue;
        }
        std::string twin = f.name;
        for (size_t at = 0; (at = twin.find("3JPH", at)) != std::string::npos; at += 4) twin[at + 1] = 'Q';
        void *target = dlsym(RTLD_DEFAULT, twin.c_str());
        if (!target) { ++missing; continue; }
        if (JumpTo(f.value, f.size, target)) ++redirected; else ++small;
    }
    std::fprintf(stderr, "x4 loader: redirected %u Jolt functions to our build (%u without a twin, %u stubs under 5 bytes, %u version checks kept)\n",
                 redirected, missing, small, kept);
}

// The executable addresses its thread-local block with fixed offsets below the thread
// pointer (local-exec model). This array is the only thread-local data of the test
// program, so it is the block directly below the thread pointer on every thread; its
// initialization image receives the executable's .tdata before any thread is started.
alignas(16) thread_local unsigned char gX4Tls[kTlsSize] = {1};

struct OwnTls { const ElfW(Phdr) *phdr; uintptr_t bias; };
int MainProgramTls(dl_phdr_info *info, size_t, void *out) {
    for (int i = 0; i < info->dlpi_phnum; i++)
        if (info->dlpi_phdr[i].p_type == PT_TLS) *static_cast<OwnTls *>(out) = {&info->dlpi_phdr[i], info->dlpi_addr};
    return 1; // the main program is reported first
}

void InstallThreadBlock(const std::vector<unsigned char> &file, uint64_t phoff, uint16_t phnum) {
    const unsigned char *image = nullptr; uint64_t filesz = 0;
    for (int i = 0; i < phnum; i++) {
        size_t p = phoff + 56u * size_t(i);
        if (At<uint32_t>(file, p) != 7) continue;
        if (At<uint64_t>(file, p + 40) != kTlsSize || At<uint64_t>(file, p + 48) != 16) Fail("thread-local block differs from the pinned layout");
        image = file.data() + At<uint64_t>(file, p + 8); filesz = At<uint64_t>(file, p + 32);
    }
    if (!image || filesz > kTlsSize) Fail("thread-local block missing");
    uintptr_t tp; __asm__("mov %%fs:0,%0" : "=r"(tp));
    if (reinterpret_cast<uintptr_t>(gX4Tls) + kTlsSize != tp) Fail("test program has thread-local data of its own");
    OwnTls own{};
    dl_iterate_phdr(MainProgramTls, &own);
    if (!own.phdr || own.phdr->p_memsz != kTlsSize || own.phdr->p_filesz != kTlsSize) Fail("test program thread-local image");
    unsigned char *ownImage = reinterpret_cast<unsigned char *>(own.bias + own.phdr->p_vaddr);
    uintptr_t lo = reinterpret_cast<uintptr_t>(ownImage) & ~uintptr_t(4095);
    uintptr_t hi = (reinterpret_cast<uintptr_t>(ownImage) + kTlsSize + 4095) & ~uintptr_t(4095);
    if (mprotect(reinterpret_cast<void *>(lo), hi - lo, PROT_READ | PROT_WRITE)) Fail("thread-local image protection");
    std::memset(ownImage, 0, kTlsSize); std::memcpy(ownImage, image, filesz);
    if (mprotect(reinterpret_cast<void *>(lo), hi - lo, PROT_READ)) Fail("thread-local image protection");
    std::memcpy(gX4Tls, ownImage, kTlsSize);
}

void Load(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) Fail("cannot open", path);
    std::vector<unsigned char> file(kSize);
    size_t got = 0;
    while (got < kSize) { ssize_t n = read(fd, file.data() + got, kSize - got); if (n <= 0) break; got += size_t(n); }
    char extra; bool longer = read(fd, &extra, 1) == 1;
    close(fd);
    if (got != kSize || longer) Fail("not the pinned executable (size)", path);
    unsigned char raw[32]; char hex[65];
    lockstep_sha256(file.data(), file.size(), raw);
    for (int i = 0; i < 32; i++) std::snprintf(hex + 2 * i, 3, "%02x", raw[i]);
    if (std::strcmp(hex, kSha)) Fail("not the pinned executable (SHA-256)", hex);
    // Segments at their fixed addresses.
    uint64_t phoff = At<uint64_t>(file, 32); uint16_t phnum = At<uint16_t>(file, 56);
    struct Seg { uint64_t vaddr, offset, filesz, memsz; uint32_t flags; };
    std::vector<Seg> segs;
    for (int i = 0; i < phnum; i++) {
        size_t p = phoff + 56u * size_t(i);
        if (At<uint32_t>(file, p) != 1) continue;
        segs.push_back({At<uint64_t>(file, p + 16), At<uint64_t>(file, p + 8), At<uint64_t>(file, p + 32), At<uint64_t>(file, p + 40), At<uint32_t>(file, p + 4)});
    }
    InstallThreadBlock(file, phoff, phnum);
    for (const Seg &s : segs) {
        uintptr_t lo = s.vaddr & ~uintptr_t(4095), hi = (s.vaddr + s.memsz + 4095) & ~uintptr_t(4095);
        void *m = mmap(reinterpret_cast<void *>(lo), hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (m != reinterpret_cast<void *>(lo)) Fail("address range of the executable is not free");
        std::memcpy(reinterpret_cast<void *>(s.vaddr), file.data() + s.offset, s.filesz);
    }
    // Section headers: .dynsym, .dynstr, .rela.dyn, .rela.plt.
    uint64_t shoff = At<uint64_t>(file, 40); uint16_t shnum = At<uint16_t>(file, 60), shstrndx = At<uint16_t>(file, 62);
    auto sh = [&](int i, int field) { return At<uint64_t>(file, shoff + 64u * size_t(i) + size_t(field)); };
    uint64_t names = sh(shstrndx, 24);
    uint64_t dynsym = 0, dynsymSize = 0, dynstr = 0;
    std::vector<std::pair<uint64_t, uint64_t>> relas;
    for (int i = 0; i < shnum; i++) {
        const char *name = reinterpret_cast<const char *>(file.data() + names + At<uint32_t>(file, shoff + 64u * size_t(i)));
        if (!std::strcmp(name, ".dynsym")) { dynsym = sh(i, 24); dynsymSize = sh(i, 32); }
        else if (!std::strcmp(name, ".dynstr")) dynstr = sh(i, 24);
        else if (!std::strcmp(name, ".rela.dyn") || !std::strcmp(name, ".rela.plt")) relas.emplace_back(sh(i, 24), sh(i, 32));
    }
    if (!dynsym || !dynstr || relas.size() != 2) Fail("dynamic tables missing");
    uintptr_t tp; __asm__("mov %%fs:0,%0" : "=r"(tp));
    for (auto [offset, size] : relas) for (uint64_t r = offset; r < offset + size; r += 24) {
        uint64_t where = At<uint64_t>(file, r), info = At<uint64_t>(file, r + 8); int64_t addend = At<int64_t>(file, r + 16);
        uint32_t type = uint32_t(info), index = uint32_t(info >> 32);
        size_t s = dynsym + 24u * index;
        const char *name = reinterpret_cast<const char *>(file.data() + dynstr + At<uint32_t>(file, s));
        uint16_t shndx = At<uint16_t>(file, s + 6); uint64_t value = At<uint64_t>(file, s + 8), symsize = At<uint64_t>(file, s + 16);
        uint64_t *slot = reinterpret_cast<uint64_t *>(where);
        if (type == 1 || type == 6 || type == 7) {
            uint64_t target = shndx ? value : reinterpret_cast<uint64_t>(index ? dlsym(RTLD_DEFAULT, name) : nullptr);
            if (!target && index && (type == 7 || (At<uint8_t>(file, s + 4) & 0xf) == 2)) target = reinterpret_cast<uint64_t>(&MissingImport);
            *slot = target ? target + uint64_t(type == 1 ? addend : 0) : 0;
        } else if (type == 5) {
            void *source = dlsym(RTLD_DEFAULT, name);
            if (source) std::memcpy(slot, source, symsize);
        } else if (type == 18) {
            if (shndx) { // offset in the executable's own block, which ends at the thread pointer
                if (value >= kTlsSize) Fail("thread-local symbol outside the block", name);
                *slot = value - kTlsSize + uint64_t(addend);
                continue;
            }
            void *source = dlsym(RTLD_DEFAULT, name);
            *slot = source ? uint64_t(reinterpret_cast<uintptr_t>(source) - tp + uint64_t(addend)) : 0;
        } else {
            Fail("unexpected relocation type");
        }
    }
    std::vector<Function> operators;
    for (uint64_t s = dynsym; s < dynsym + dynsymSize; s += 24) {
        const char *name = reinterpret_cast<const char *>(file.data() + dynstr + At<uint32_t>(file, s));
        if ((At<uint8_t>(file, s + 4) & 0xf) != 2 || !At<uint16_t>(file, s + 6)) continue;
        Function f{name, At<uint64_t>(file, s + 8), At<uint64_t>(file, s + 16)};
        if (std::strstr(name, "3JPH")) gJoltFunctions.push_back(f);
        else if (!std::strncmp(name, "_Znwm", 5) || !std::strncmp(name, "_Znam", 5) || !std::strncmp(name, "_ZdlPv", 6) || !std::strncmp(name, "_ZdaPv", 6))
            operators.push_back(f);
    }
    UseProcessAllocator(operators);
    if (const char *filter = std::getenv("X4_JOLT_REDIRECT")) Redirect(filter);
    if (gThunks && mprotect(gThunks, 0x10000, PROT_READ | PROT_EXEC)) Fail("thunk page protection");
    for (const Seg &s : segs) {
        uintptr_t lo = s.vaddr & ~uintptr_t(4095), hi = (s.vaddr + s.memsz + 4095) & ~uintptr_t(4095);
        int prot = ((s.flags & 4) ? PROT_READ : 0) | ((s.flags & 2) ? PROT_WRITE : 0) | ((s.flags & 1) ? PROT_EXEC : 0);
        if (mprotect(reinterpret_cast<void *>(lo), hi - lo, prot)) Fail("mprotect");
    }
}
} // namespace

// Called at the start of the scenario's main when built against X4's Jolt.
void X4JoltLoad() {
    const char *path = std::getenv("X4_LINUX_EXE");
    if (!path) Fail("set X4_LINUX_EXE to the owned Linux X4 executable");
    Load(path);
    JPH::RegisterDefaultAllocator(); // X4's: sets X4's allocation hooks before its initializers allocate
    if (std::getenv("X4_JOLT_SELECTIVE_INIT")) {
        // Only the two exported Jolt statics with dynamic initialization (leaves file-local
        // tables such as CylinderShape's cCylinderTopFace zero: kept to show that effect).
        reinterpret_cast<void (*)()>(kInitFixedToWorld)();
        reinterpret_cast<void (*)()>(kInitUnitSphere)();
        new (reinterpret_cast<void *>(kCollisionGroupInvalid)) JPH::CollisionGroup();
        new (reinterpret_cast<void *>(kQuadTreeInvalidBounds)) JPH::AABox(JPH::Vec3::sReplicate(JPH::cLargeFloat), JPH::Vec3::sReplicate(-JPH::cLargeFloat));
    } else {
        // The executable's merged static initializer (all link-time-optimised units, Jolt's
        // file-local constant tables included), as at process start.
        reinterpret_cast<void (*)()>(kMergedInit)();
    }
}
