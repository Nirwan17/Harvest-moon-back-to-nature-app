// modmenu.cpp - Mod menu untuk Harvest Moon BTN (emulator PS1 wasm2c buatan Penyu)
//
// Cara kerja:
//  1. Menempelkan hook (GOT/PLT) pada wasm_rt_allocate_memory milik libhmbtn.so
//     supaya kita tahu di mana "RAM PS1" berada (memori linear wasm 16 MB,
//     alamat PS1 0x80071A5C = offset 0x71A5C).
//  2. Thread kecil menulis ulang nilai cheat yang aktif ~60x per detik (seperti GameShark).
//  3. Fungsi JNI dipakai ModMenu.java untuk membangun menu.
//
// Hanya mengubah nilai RAM game single-player di perangkat sendiri.

#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "cheats_data.h"

#define TAG "HMBTNMod"
#define LOG_PATH "/storage/emulated/0/HarvestMoonBTN/modmenu.log"

#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64 257
#endif
#ifndef R_AARCH64_GLOB_DAT
#define R_AARCH64_GLOB_DAT 1025
#endif
#ifndef R_AARCH64_JUMP_SLOT
#define R_AARCH64_JUMP_SLOT 1026
#endif

// ---------------------------------------------------------------- log
static std::mutex g_logmu;
static void logf(const char* fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
    std::lock_guard<std::mutex> l(g_logmu);
    FILE* f = fopen(LOG_PATH, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

// ---------------------------------------------------------------- memori game
struct WasmMem {            // wasm_rt_memory_t (diverifikasi dari disassembly libhmbtn)
    uint8_t* data;          // +0
    uint64_t pages;         // +8
    uint64_t max_pages;     // +16
    uint64_t size;          // +24  (= pages * 65536)
    uint8_t  is64;          // +32
};
struct Mem { uint8_t* base; size_t size; const char* via; };

static std::atomic<WasmMem*> g_ms{nullptr};   // diisi hook
static std::atomic<int64_t>  g_off{0};        // koreksi alamat (kalibrasi)
static uintptr_t g_bias = 0;                  // load bias libhmbtn.so
static std::atomic<uint8_t*> g_fb_base{nullptr};
static std::atomic<size_t>   g_fb_size{0};
static const char*           g_fb_via = "-";

static bool maps_region_ok(uintptr_t p, size_t* regSize) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[700];
    bool ok = false;
    while (fgets(line, sizeof line, f)) {
        unsigned long s, e; char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perms) != 3) continue;
        if (p >= s && p < e && perms[0] == 'r' && perms[1] == 'w') {
            if (regSize) *regSize = e - p;
            ok = true;
            break;
        }
    }
    fclose(f);
    return ok;
}

// Cadangan: cari mapping anonim rw (16 MB) yang diikuti area "---p" sampai total 8 GB
static bool find_by_maps(uint8_t** base, size_t* size) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[700];
    unsigned long ps = 0, pe = 0;
    bool prevAnonRw = false;
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        unsigned long s, e, off; char perms[8]; char rest[512]; rest[0] = 0;
        int n = sscanf(line, "%lx-%lx %7s %lx %*s %*s %511[^\n]", &s, &e, perms, &off, rest);
        if (n < 4) { prevAnonRw = false; continue; }
        bool anon = (n == 4) || rest[0] == 0 || strncmp(rest, "[anon:", 6) == 0;
        if (prevAnonRw && strcmp(perms, "---p") == 0 && s == pe &&
            (e - ps) == 0x200000000UL && (pe - ps) >= 0x200000UL) {
            *base = (uint8_t*)ps; *size = pe - ps; found = true; break;
        }
        prevAnonRw = anon && strcmp(perms, "rw-p") == 0 && (e - s) >= 0x200000UL;
        ps = s; pe = e;
    }
    fclose(f);
    return found;
}

static bool mem_get(Mem& m) {
    WasmMem* ms = g_ms.load();
    if (ms && ms->data && ms->size >= 0x200000) {
        m.base = ms->data; m.size = (size_t)ms->size; m.via = "hook"; return true;
    }
    uint8_t* fb = g_fb_base.load();
    if (fb) { m.base = fb; m.size = g_fb_size.load(); m.via = g_fb_via; return true; }
    // cadangan 1: pointer global di libhmbtn.so v1.3.1 (0x257a90)
    if (g_bias) {
        uint8_t* p = *(uint8_t* volatile*)(g_bias + 0x257a90);
        size_t rs = 0;
        if (p && maps_region_ok((uintptr_t)p, &rs) && rs >= 0x200000) {
            g_fb_size = rs > 0x1000000 ? 0x1000000 : rs; g_fb_via = "global"; g_fb_base = p;
            logf("memori via pointer global: %p", (void*)p);
            m.base = p; m.size = g_fb_size; m.via = g_fb_via; return true;
        }
    }
    // cadangan 2: heuristik /proc/self/maps
    uint8_t* b = nullptr; size_t sz = 0;
    if (find_by_maps(&b, &sz)) {
        g_fb_size = sz; g_fb_via = "maps"; g_fb_base = b;
        logf("memori via maps: %p (%zu byte)", (void*)b, sz);
        m.base = b; m.size = sz; m.via = g_fb_via; return true;
    }
    return false;
}

static inline bool resolve(const Mem& m, uint32_t addr, size_t len, size_t& o) {
    int64_t x = (int64_t)(addr & 0x1FFFFFu) + g_off.load();
    if (x < 0 || (uint64_t)x + len > m.size) return false;
    o = (size_t)x;
    return true;
}

// ---------------------------------------------------------------- hook GOT
#if defined(__LP64__)
struct LibInfo { uintptr_t base; const Elf64_Phdr* ph; int n; bool found; };
static LibInfo g_hm = {0, nullptr, 0, false};

static int phdr_cb(struct dl_phdr_info* i, size_t, void*) {
    if (i->dlpi_name && strstr(i->dlpi_name, "libhmbtn.so")) {
        g_hm.base = (uintptr_t)i->dlpi_addr;
        g_hm.ph = (const Elf64_Phdr*)i->dlpi_phdr;
        g_hm.n = i->dlpi_phnum;
        g_hm.found = true;
        return 1;
    }
    return 0;
}

static bool find_lib() {
    dl_iterate_phdr(phdr_cb, nullptr);
    if (!g_hm.found) {
        void* h = dlopen("libhmbtn.so", RTLD_NOW);      // muat sendiri bila belum
        logf("dlopen libhmbtn.so -> %p", h);
        dl_iterate_phdr(phdr_cb, nullptr);
    }
    if (g_hm.found) g_bias = g_hm.base;
    return g_hm.found;
}

static bool patch_slot(void** slot, void* fn) {
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page = (uintptr_t)slot & ~(uintptr_t)(ps - 1);
    if (mprotect((void*)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0) return false;
    *slot = fn;
    mprotect((void*)page, (size_t)ps, PROT_READ);
    return true;
}

static int hook_import(const char* name, void* fn, void** orig) {
    if (!g_hm.found) return 0;
    const Elf64_Dyn* dyn = nullptr;
    for (int i = 0; i < g_hm.n; i++)
        if (g_hm.ph[i].p_type == PT_DYNAMIC) { dyn = (const Elf64_Dyn*)(g_hm.base + g_hm.ph[i].p_vaddr); break; }
    if (!dyn) return 0;
    uintptr_t jmprel = 0, rela = 0, symtab = 0, strtab = 0;
    size_t pltsz = 0, relasz = 0;
    auto fix = [&](uintptr_t p) { return p < g_hm.base ? p + g_hm.base : p; };
    for (const Elf64_Dyn* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL:   jmprel = fix(d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
            case DT_RELA:     rela = fix(d->d_un.d_ptr); break;
            case DT_RELASZ:   relasz = d->d_un.d_val; break;
            case DT_SYMTAB:   symtab = fix(d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = fix(d->d_un.d_ptr); break;
        }
    }
    if (!symtab || !strtab) return 0;
    int patched = 0;
    auto scan = [&](uintptr_t tab, size_t sz) {
        if (!tab || !sz) return;
        const Elf64_Rela* r = (const Elf64_Rela*)tab;
        size_t n = sz / sizeof(Elf64_Rela);
        for (size_t i = 0; i < n; i++) {
            unsigned t = (unsigned)ELF64_R_TYPE(r[i].r_info);
            if (t != R_AARCH64_JUMP_SLOT && t != R_AARCH64_GLOB_DAT && t != R_AARCH64_ABS64) continue;
            const Elf64_Sym* s = (const Elf64_Sym*)symtab + ELF64_R_SYM(r[i].r_info);
            const char* nm = (const char*)strtab + s->st_name;
            if (strcmp(nm, name) != 0) continue;
            void** slot = (void**)(g_hm.base + r[i].r_offset);
            if (!*orig) *orig = *slot;
            if (patch_slot(slot, fn)) patched++;
        }
    };
    scan(jmprel, pltsz);
    scan(rela, relasz);
    return patched;
}
#else
static bool find_lib() { return false; }
static int hook_import(const char*, void*, void**) { return 0; }
#endif

typedef void (*alloc_fn)(void*, uint64_t, uint64_t, bool);
static alloc_fn g_real_alloc = nullptr;

static void hook_alloc(void* mem, uint64_t initial, uint64_t maxp, bool is64) {
    g_real_alloc(mem, initial, maxp, is64);
    WasmMem* ms = (WasmMem*)mem;
    if (ms->size >= 0x200000) g_ms.store(ms);
    logf("wasm_rt_allocate_memory: struct=%p data=%p pages=%llu max=%llu",
         mem, (void*)ms->data, (unsigned long long)initial, (unsigned long long)maxp);
}

// ---------------------------------------------------------------- cheat engine
static std::mutex g_mu;
static uint8_t g_on[kGroupCount > 0 ? kGroupCount : 1];
static std::vector<int> g_order;                 // urutan aktif (yang terakhir menang)

static void apply_group(const Mem& m, int gi) {
    const Group& g = kGroups[gi];
    bool skip = false;
    for (int k = 0; k < g.count; k++) {
        const Op& op = kOps[g.start + k];
        size_t o;
        if (op.type == 2) {                       // D0: jalankan baris berikut hanya jika 16-bit == nilai
            uint16_t cur = 0;
            if (resolve(m, op.addr, 2, o)) memcpy(&cur, m.base + o, 2);
            skip = (cur != op.val);
            continue;
        }
        if (skip) { skip = false; continue; }
        if (op.type == 0) {
            if (resolve(m, op.addr, 2, o)) { uint16_t v = op.val; memcpy(m.base + o, &v, 2); }
        } else {
            if (resolve(m, op.addr, 1, o)) m.base[o] = (uint8_t)op.val;
        }
    }
}

static void apply_all() {
    std::vector<int> order;
    { std::lock_guard<std::mutex> l(g_mu); order = g_order; }
    if (order.empty()) return;
    Mem m;
    if (!mem_get(m)) return;
    for (int gi : order) apply_group(m, gi);
}

static void worker() {
    bool announced = false;
    for (;;) {
        bool any;
        { std::lock_guard<std::mutex> l(g_mu); any = !g_order.empty(); }
        if (any) {
            apply_all();
            if (!announced) { Mem m; if (mem_get(m)) { announced = true; logf("cheat aktif, memori via %s", m.via); } }
            usleep(16000);
        } else {
            usleep(120000);
        }
    }
}

static std::atomic<bool> g_started{false};

static void install() {
    if (g_started.exchange(true)) return;
    logf("=== HM BTN Mod Menu dimulai (grup=%d, op=%d) ===", kGroupCount, kOpCount);
    if (find_lib()) {
        int n = hook_import("wasm_rt_allocate_memory", (void*)hook_alloc, (void**)&g_real_alloc);
        logf("libhmbtn bias=0x%lx, hook allocate_memory: %d slot", (unsigned long)g_bias, n);
        if (n == 0) g_real_alloc = nullptr;
    } else {
        logf("libhmbtn.so tidak ditemukan - pakai metode cadangan");
    }
    std::thread(worker).detach();
}

// ---------------------------------------------------------------- scanner / kalibrasi
static std::vector<uint32_t> g_cand;
static bool g_have_cand = false;

static std::string do_scan(int value, bool first) {
    Mem m;
    if (!mem_get(m)) return "Memori game belum ditemukan. Mulai/muat game dulu.";
    char buf[300];
    if (first || !g_have_cand) {
        g_cand.clear();
        const uint32_t v = (uint32_t)value;
        for (size_t o = 0; o + 4 <= m.size && g_cand.size() < 20000; o += 4) {
            uint32_t x; memcpy(&x, m.base + o, 4);
            if (x == v) g_cand.push_back((uint32_t)o);
        }
        g_have_cand = true;
    } else {
        std::vector<uint32_t> keep;
        for (uint32_t o : g_cand) {
            uint32_t x; memcpy(&x, m.base + o, 4);
            if (x == (uint32_t)value) keep.push_back(o);
        }
        g_cand.swap(keep);
    }
    std::string out;
    snprintf(buf, sizeof buf, "Ditemukan %zu kandidat.", g_cand.size());
    out = buf;
    if (g_cand.empty()) { out += " Coba lagi dari awal (tekan Reset)."; return out; }
    if (g_cand.size() == 1) {
        int64_t off = (int64_t)g_cand[0] - 0x71A5C;
        g_off.store(off);
        snprintf(buf, sizeof buf, "\nCocok! Alamat uang = 0x%X. Offset koreksi diterapkan: %+lld.",
                 g_cand[0], (long long)off);
        out += buf;
    } else {
        out += "\nUbah uangmu (jual/beli), isi angka baru, lalu tekan Cari lagi untuk menyaring.";
        for (size_t i = 0; i < g_cand.size() && i < 5; i++) {
            snprintf(buf, sizeof buf, "\n - 0x%X (offset %+lld)", g_cand[i], (long long)((int64_t)g_cand[i] - 0x71A5C));
            out += buf;
        }
    }
    return out;
}

static std::string status_text() {
    Mem m;
    int active;
    { std::lock_guard<std::mutex> l(g_mu); active = (int)g_order.size(); }
    char buf[400];
    if (!mem_get(m)) {
        snprintf(buf, sizeof buf, "Memori game: BELUM ditemukan (tunggu game selesai loading)\nCheat aktif: %d", active);
        return buf;
    }
    size_t o; uint32_t money = 0; uint16_t sta = 0;
    if (resolve(m, 0x80071A5C, 4, o)) memcpy(&money, m.base + o, 4);
    if (resolve(m, 0x80071A12, 2, o)) memcpy(&sta, m.base + o, 2);
    snprintf(buf, sizeof buf, "Memori: OK (%zu MB via %s) | offset %+lld\nUang: %u | Stamina: %u | Cheat aktif: %d",
             m.size >> 20, m.via, (long long)g_off.load(), money, (unsigned)sta, active);
    return buf;
}

// ---------------------------------------------------------------- JNI
#define JF(ret, name) extern "C" JNIEXPORT ret JNICALL Java_id_penyu_modmenu_ModMenu_##name

JF(void, nInstall)(JNIEnv*, jclass) { install(); }
JF(jint, nCatCount)(JNIEnv*, jclass) { return kCatCount; }
JF(jstring, nCatName)(JNIEnv* e, jclass, jint c) {
    return e->NewStringUTF(c >= 0 && c < kCatCount ? kCats[c] : "?");
}
JF(jint, nCatSize)(JNIEnv*, jclass, jint c) {
    return (c >= 0 && c < kCatCount) ? kCatIdx[c][1] : 0;
}
JF(jint, nCatGroup)(JNIEnv*, jclass, jint c, jint i) {
    if (c < 0 || c >= kCatCount || i < 0 || i >= kCatIdx[c][1]) return -1;
    return kCatGroups[kCatIdx[c][0] + i];
}
JF(jstring, nGroupName)(JNIEnv* e, jclass, jint g) {
    return e->NewStringUTF(g >= 0 && g < kGroupCount ? kGroups[g].name : "?");
}
JF(jboolean, nGroupExp)(JNIEnv*, jclass, jint g) {
    return (g >= 0 && g < kGroupCount && kGroups[g].exp) ? JNI_TRUE : JNI_FALSE;
}
JF(jboolean, nIsOn)(JNIEnv*, jclass, jint g) {
    if (g < 0 || g >= kGroupCount) return JNI_FALSE;
    std::lock_guard<std::mutex> l(g_mu);
    return g_on[g] ? JNI_TRUE : JNI_FALSE;
}
JF(void, nSetOn)(JNIEnv*, jclass, jint g, jboolean on) {
    if (g < 0 || g >= kGroupCount) return;
    std::lock_guard<std::mutex> l(g_mu);
    if (on) {
        const Group& gr = kGroups[g];
        for (int k = 0; k < gr.conflCount; k++) {          // matikan cheat yang bentrok
            int c = kConfl[gr.conflStart + k];
            if (g_on[c]) {
                g_on[c] = 0;
                g_order.erase(std::remove(g_order.begin(), g_order.end(), c), g_order.end());
            }
        }
        if (!g_on[g]) { g_on[g] = 1; g_order.push_back(g); }
    } else if (g_on[g]) {
        g_on[g] = 0;
        g_order.erase(std::remove(g_order.begin(), g_order.end(), g), g_order.end());
    }
}
JF(void, nClearAll)(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> l(g_mu);
    memset(g_on, 0, sizeof g_on);
    g_order.clear();
}
JF(jint, nActiveCount)(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> l(g_mu);
    return (jint)g_order.size();
}
JF(jstring, nStatus)(JNIEnv* e, jclass) { return e->NewStringUTF(status_text().c_str()); }
JF(jstring, nScan)(JNIEnv* e, jclass, jint value, jboolean first) {
    return e->NewStringUTF(do_scan(value, first == JNI_TRUE).c_str());
}
JF(jstring, nResetOff)(JNIEnv* e, jclass) {
    g_off.store(0); g_cand.clear(); g_have_cand = false;
    return e->NewStringUTF("Offset dikembalikan ke 0 dan pencarian direset.");
}
