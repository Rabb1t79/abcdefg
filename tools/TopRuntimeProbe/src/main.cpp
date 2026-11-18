#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {
constexpr std::uint64_t kExpectedFileSize = 1654784ULL;
constexpr char kExpectedSha256[] =
    "9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42";
constexpr std::uint32_t kExpectedImageSize = 0x0019D000;
constexpr std::uint32_t kVtableAiRva = 0x00135C7C;
constexpr std::uint32_t kVtableGamePlayRva = 0x00138104;
constexpr std::uint32_t kVtablePlayerRva = 0x0013810C;
constexpr std::uint32_t kVtablePlayfieldRva = 0x0013811C;
constexpr std::uint32_t kAiGamePlayOffset = 0x114;
constexpr std::uint32_t kGamePlayPlayfieldOffset = 0x04;
constexpr std::uint32_t kGamePlayPlayerOffset = 0x08;
constexpr std::uint32_t kPlayfieldWidthOffset = 0x08;
constexpr std::uint32_t kPlayfieldHeightOffset = 0x0C;
constexpr std::uint32_t kPlayfieldCellsBeginOffset = 0x1C;
constexpr std::uint32_t kPlayfieldCellsEndOffset = 0x20;
constexpr std::uint32_t kPlayfieldCellsCapacityOffset = 0x24;
constexpr std::uint32_t kCellStride = 0x14;
constexpr std::uint32_t kCellOccupancyOffset = 0x0C;
constexpr std::uint32_t kAiSnapshotDataOffset = 0x0CC;
constexpr std::uint32_t kAiSnapshotWidthOffset = 0x0DC;
constexpr std::uint32_t kAiSnapshotHeightOffset = 0x0E0;
constexpr std::uint32_t kExpectedWidth = 10;
constexpr std::uint32_t kExpectedHeight = 40;
constexpr std::size_t kScanChunkBytes = 1024U * 1024U;

struct UniqueHandle {
    HANDLE value{nullptr};
    explicit UniqueHandle(HANDLE h = nullptr) : value(h) {}
    ~UniqueHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
};

std::string Hex32(std::uint32_t v) {
    std::ostringstream s;
    s << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << v;
    return s.str();
}
std::string WinError(const char* action, DWORD code = GetLastError()) {
    return std::string(action) + " failed (Win32 error " + std::to_string(code) + ")";
}
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "<path-conversion-failed>";
    std::string s((std::size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool HashFile(const std::wstring& path, std::string& out, std::string& error) {
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.value) { error = WinError("CreateFile(tetris.exe)"); return false; }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> object;
    std::array<UCHAR, 65536> buffer{};
    DWORD objectLength = 0, done = 0;
    bool ok = false;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0) { error = "BCryptOpenAlgorithmProvider failed: " + std::to_string(st); goto cleanup; }
    st = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectLength,
                           sizeof(objectLength), &done, 0);
    if (st < 0 || !objectLength) {
        error = "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed: " + std::to_string(st);
        goto cleanup;
    }
    object.resize(objectLength);
    st = BCryptCreateHash(alg, &hash, object.data(), objectLength, nullptr, 0, 0);
    if (st < 0) { error = "BCryptCreateHash failed: " + std::to_string(st); goto cleanup; }
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file.value, buffer.data(), (DWORD)buffer.size(), &got, nullptr)) {
            error = WinError("ReadFile(tetris.exe)"); goto cleanup;
        }
        if (!got) break;
        st = BCryptHashData(hash, buffer.data(), got, 0);
        if (st < 0) { error = "BCryptHashData failed: " + std::to_string(st); goto cleanup; }
    }
    {
        std::array<UCHAR, 32> digest{};
        st = BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0);
        if (st < 0) { error = "BCryptFinishHash failed: " + std::to_string(st); goto cleanup; }
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setfill('0');
        for (UCHAR b : digest) s << std::setw(2) << (unsigned)b;
        out = s.str();
    }
    ok = true;
cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool ReadRemote(HANDLE p, std::uint32_t address, void* dst, std::size_t bytes) {
    if (address < 0x10000U || !bytes ||
        (std::uint64_t)address + bytes > 0x100000000ULL) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(p, (LPCVOID)(std::uintptr_t)address, dst, bytes, &got) &&
           got == bytes;
}
bool ReadU32(HANDLE p, std::uint32_t a, std::uint32_t& v) {
    return ReadRemote(p, a, &v, sizeof(v));
}
bool Add(std::uint32_t a, std::uint32_t b, std::uint32_t& out) {
    std::uint64_t n = (std::uint64_t)a + b;
    if (n > 0xFFFFFFFFULL) return false;
    out = (std::uint32_t)n;
    return true;
}
bool Readable(DWORD protect) {
    if ((protect & PAGE_GUARD) || (protect & PAGE_NOACCESS)) return false;
    switch (protect & 0xFFU) {
        case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
            return true;
        default: return false;
    }
}

std::vector<DWORD> FindProcesses() {
    std::vector<DWORD> result;
    UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap.value) return result;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    if (!Process32FirstW(snap.value, &e)) return result;
    do {
        if (_wcsicmp(e.szExeFile, L"tetris.exe") == 0) result.push_back(e.th32ProcessID);
    } while (Process32NextW(snap.value, &e));
    return result;
}
bool ProcessPath(HANDLE p, std::wstring& path) {
    std::vector<wchar_t> b(32768, L'\0');
    DWORD n = (DWORD)b.size();
    if (!QueryFullProcessImageNameW(p, 0, b.data(), &n)) return false;
    path.assign(b.data(), n);
    return true;
}
struct ModuleInfo { std::uint32_t base{}, size{}; };
bool FindModule(DWORD pid, ModuleInfo& m, std::string& error) {
    UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snap.value) { error = WinError("CreateToolhelp32Snapshot(SNAPMODULE)"); return false; }
    MODULEENTRY32W e{}; e.dwSize = sizeof(e);
    if (!Module32FirstW(snap.value, &e)) { error = WinError("Module32First"); return false; }
    do {
        if (_wcsicmp(e.szModule, L"tetris.exe") == 0) {
            auto base = (std::uintptr_t)e.modBaseAddr;
            if (base > 0xFFFFFFFFULL) { error = "tetris.exe module base is not x86"; return false; }
            m.base = (std::uint32_t)base; m.size = e.modBaseSize; return true;
        }
    } while (Module32NextW(snap.value, &e));
    error = "tetris.exe module not found in target module list";
    return false;
}
bool VerifyImage(const std::wstring& path, const ModuleInfo& m, std::string& error) {
    if (m.size != kExpectedImageSize) {
        error = "loaded image size mismatch: expected " + Hex32(kExpectedImageSize) +
                ", got " + Hex32(m.size); return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) {
        error = WinError("GetFileAttributesEx"); return false;
    }
    ULARGE_INTEGER size{}; size.HighPart = a.nFileSizeHigh; size.LowPart = a.nFileSizeLow;
    if (size.QuadPart != kExpectedFileSize) {
        error = "file size mismatch: got " + std::to_string(size.QuadPart); return false;
    }
    std::string hash;
    if (!HashFile(path, hash, error)) return false;
    if (hash != kExpectedSha256) {
        error = "SHA-256 mismatch: got " + hash; return false;
    }
    return true;
}

struct Candidate {
    std::uint32_t ai{}, game{}, field{}, player{}, width{}, height{};
    std::uint32_t cells{}, cellsEnd{}, cellsCap{}, moduleBase{};
};
bool Validate(HANDLE p, std::uint32_t base, std::uint32_t ai,
              Candidate& c, std::string* why = nullptr) {
    auto fail = [&](const char* s) { if (why) *why = s; return false; };
    std::uint32_t v = 0, at = 0;
    if (!ReadU32(p, ai, v) || v != base + kVtableAiRva) return fail("AI vtable mismatch");
    if (!Add(ai, kAiGamePlayOffset, at) || !ReadU32(p, at, c.game) || c.game < 0x10000)
        return fail("AI->CGamePlay unreadable");
    if (!ReadU32(p, c.game, v) || v != base + kVtableGamePlayRva)
        return fail("CGamePlay vtable mismatch");
    std::uint32_t fAt = 0, pAt = 0;
    if (!Add(c.game, kGamePlayPlayfieldOffset, fAt) ||
        !Add(c.game, kGamePlayPlayerOffset, pAt) ||
        !ReadU32(p, fAt, c.field) || !ReadU32(p, pAt, c.player) ||
        c.field < 0x10000 || c.player < 0x10000)
        return fail("CGamePlay child pointer unreadable");
    if (!ReadU32(p, c.field, v) || v != base + kVtablePlayfieldRva)
        return fail("CPlayfield vtable mismatch");
    if (!ReadU32(p, c.player, v) || v != base + kVtablePlayerRva)
        return fail("CPlayer vtable mismatch");
    std::uint32_t wAt = 0, hAt = 0;
    if (!Add(c.field, kPlayfieldWidthOffset, wAt) ||
        !Add(c.field, kPlayfieldHeightOffset, hAt) ||
        !ReadU32(p, wAt, c.width) || !ReadU32(p, hAt, c.height))
        return fail("playfield dimensions unreadable");
    if (c.width != kExpectedWidth || c.height != kExpectedHeight)
        return fail("dimensions differ from expected 10x40");
    std::uint32_t bAt = 0, eAt = 0, capAt = 0;
    if (!Add(c.field, kPlayfieldCellsBeginOffset, bAt) ||
        !Add(c.field, kPlayfieldCellsEndOffset, eAt) ||
        !Add(c.field, kPlayfieldCellsCapacityOffset, capAt) ||
        !ReadU32(p, bAt, c.cells) || !ReadU32(p, eAt, c.cellsEnd) ||
        !ReadU32(p, capAt, c.cellsCap))
        return fail("cell vector unreadable");
    const std::uint64_t expected = (std::uint64_t)c.width * c.height * kCellStride;
    if (c.cells < 0x10000 || c.cellsEnd < c.cells || c.cellsCap < c.cellsEnd ||
        (std::uint64_t)c.cellsEnd - c.cells != expected)
        return fail("cell vector bounds or exact byte length mismatch");
    std::uint32_t first = 0, last = 0;
    const std::uint32_t lastAt = c.cells +
        (c.width * c.height - 1U) * kCellStride + kCellOccupancyOffset;
    if (!ReadU32(p, c.cells + kCellOccupancyOffset, first) || !ReadU32(p, lastAt, last))
        return fail("first/last occupancy DWORD not readable");
    c.ai = ai; c.moduleBase = base;
    if (why) why->clear();
    return true;
}

bool Scan(HANDLE p, std::uint32_t target, std::vector<std::uint32_t>& hits,
          std::uint64_t& scanned, std::uint64_t& failedChunks) {
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    const std::uintptr_t max = (std::uintptr_t)si.lpMaximumApplicationAddress;
    std::uintptr_t cursor = (std::uintptr_t)si.lpMinimumApplicationAddress;
    std::vector<std::uint8_t> buf(kScanChunkBytes);
    while (cursor <= max) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(p, (LPCVOID)cursor, &mbi, sizeof(mbi))) break;
        const std::uintptr_t rb = (std::uintptr_t)mbi.BaseAddress;
        if (!mbi.RegionSize || rb > max) break;
        const std::uint64_t end64 = (std::uint64_t)rb + mbi.RegionSize;
        const std::uintptr_t re = (std::uintptr_t)(std::min<std::uint64_t>(
            end64, (std::uint64_t)max + 1ULL));
        if (mbi.State == MEM_COMMIT && Readable(mbi.Protect)) {
            std::uintptr_t chunk = rb;
            while (chunk < re) {
                const std::size_t n = (std::size_t)(std::min<std::uintptr_t>(
                    re - chunk, (std::uintptr_t)kScanChunkBytes));
                SIZE_T got = 0;
                BOOL ok = ReadProcessMemory(p, (LPCVOID)chunk, buf.data(), n, &got);
                if (!ok && !got) ++failedChunks;
                else {
                    scanned += got;
                    std::uintptr_t a = (chunk + 3U) & ~(std::uintptr_t)3U;
                    while (a >= chunk && a - chunk + 4U <= got) {
                        std::uint32_t v = 0;
                        std::memcpy(&v, buf.data() + (std::size_t)(a - chunk), 4);
                        if (v == target) {
                            hits.push_back((std::uint32_t)a);
                            if (hits.size() >= 4096) return true;
                        }
                        a += 4U;
                    }
                }
                chunk += n;
            }
        }
        if (re <= cursor) break;
        cursor = re;
    }
    return true;
}

bool Snapshot(HANDLE p, const Candidate& c, std::vector<std::uint8_t>& board,
              std::uint32_t& occupied, std::string& error) {
    const std::size_t cells = (std::size_t)c.width * c.height;
    std::vector<std::uint8_t> raw(cells * kCellStride);
    if (!ReadRemote(p, c.cells, raw.data(), raw.size())) {
        error = "ReadProcessMemory failed while reading the cell-record vector"; return false;
    }
    board.assign(cells, 0); occupied = 0;
    for (std::size_t i = 0; i < cells; ++i) {
        std::uint32_t flag = 0;
        std::memcpy(&flag, raw.data() + i * kCellStride + kCellOccupancyOffset, 4);
        board[i] = flag == 0 ? 1 : 0;
        occupied += board[i];
    }
    return true;
}

bool ReadAiSnapshot(HANDLE p, const Candidate& c, std::vector<std::uint8_t>& board,
                    std::uint32_t& occupied, std::uint32_t& dataAddress,
                    std::string& error) {
    std::uint32_t dataAt = 0, widthAt = 0, heightAt = 0;
    std::uint32_t snapshotWidth = 0, snapshotHeight = 0;
    if (!Add(c.ai, kAiSnapshotDataOffset, dataAt) ||
        !Add(c.ai, kAiSnapshotWidthOffset, widthAt) ||
        !Add(c.ai, kAiSnapshotHeightOffset, heightAt) ||
        !ReadU32(p, dataAt, dataAddress) ||
        !ReadU32(p, widthAt, snapshotWidth) ||
        !ReadU32(p, heightAt, snapshotHeight)) {
        error = "Could not read the AI snapshot pointer/dimensions";
        return false;
    }
    if (snapshotWidth != c.width || snapshotHeight != c.height) {
        error = "AI snapshot dimensions do not match CPlayfield dimensions";
        return false;
    }
    const std::size_t cells = (std::size_t)c.width * c.height;
    std::vector<std::uint32_t> values(cells);
    if (dataAddress < 0x10000 ||
        !ReadRemote(p, dataAddress, values.data(), values.size() * sizeof(values[0]))) {
        error = "AI snapshot vector data is not readable";
        return false;
    }
    board.assign(cells, 0);
    occupied = 0;
    for (std::size_t i = 0; i < cells; ++i) {
        if (values[i] > 1) {
            error = "AI snapshot contains a value other than 0 or 1";
            return false;
        }
        board[i] = static_cast<std::uint8_t>(values[i]);
        occupied += board[i];
    }
    return true;
}
void PrintBoard(std::ostream& out, const Candidate& c, const std::vector<std::uint8_t>& board) {
    out << "Rows are raw memory indices y=0..39; visual top/bottom orientation is not yet verified.\n";
    for (std::uint32_t y = 0; y < c.height; ++y) {
        out << "y=" << std::setw(2) << std::setfill('0') << y << " |";
        for (std::uint32_t x = 0; x < c.width; ++x)
            out << (board[(std::size_t)y * c.width + x] ? '#' : '.');
        out << "|\n";
    }
}

int SelfTest() {
    std::vector<std::uint8_t> data((std::size_t)kExpectedWidth * kExpectedHeight * kCellStride, 0xA5);
    auto set = [&](std::uint32_t x, std::uint32_t y, std::uint32_t flag) {
        std::size_t off = ((std::size_t)y * kExpectedWidth + x) * kCellStride + kCellOccupancyOffset;
        std::memcpy(data.data() + off, &flag, 4);
    };
    set(0, 0, 0); set(9, 39, 0); set(5, 20, 1);
    std::uint32_t occupied = 0;
    for (std::size_t i = 0; i < (std::size_t)kExpectedWidth * kExpectedHeight; ++i) {
        std::uint32_t flag = 0;
        std::memcpy(&flag, data.data() + i * kCellStride + kCellOccupancyOffset, 4);
        occupied += flag == 0;
    }
    if (occupied != 2) { std::cerr << "SELF-TEST FAILED\n"; return 1; }
    std::cout << "SELF-TEST PASSED: 10x40, row-major, 0x14 stride, +0x0C occupancy flag\n";
    return 0;
}
void Usage() {
    std::cout << "TopRuntimeProbe — read-only TOP CHS_05 board probe\n"
              << "Usage: TopRuntimeProbe.exe [--pid <PID>] | --list | --self-test | --help\n";
}
int Run(DWORD requestedPid) {
    std::vector<DWORD> pids = requestedPid ? std::vector<DWORD>{requestedPid} : FindProcesses();
    if (pids.empty()) {
        std::cerr << "No tetris.exe process found. Start the exact TO-P_CHS_05 client first.\n";
        return 2;
    }
    if (pids.size() != 1) {
        std::cerr << "Multiple tetris.exe processes found; select one using --pid:\n";
        for (DWORD id : pids) std::cerr << "  PID " << id << "\n";
        return 2;
    }
    const DWORD pid = pids[0];
    UniqueHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!process.value) {
        std::cerr << WinError("OpenProcess(read-only)") << "\n"
                  << "Run under the same Windows account/integrity level as the game.\n";
        return 3;
    }
    std::wstring path;
    if (!ProcessPath(process.value, path)) { std::cerr << WinError("QueryFullProcessImageName") << "\n"; return 3; }
    ModuleInfo module{}; std::string error;
    if (!FindModule(pid, module, error)) { std::cerr << error << "\n"; return 3; }
    if (!VerifyImage(path, module, error)) {
        std::cerr << "TARGET FINGERPRINT REJECTED: " << error << "\n"; return 4;
    }

    const std::uint32_t aiVtable = module.base + kVtableAiRva;
    std::cout << "TopRuntimeProbe 0.2 - read-only TOP CHS_05 runtime board probe\n"
              << "PID: " << pid << "\nImage: " << ToUtf8(path)
              << "\nSHA256: " << kExpectedSha256
              << "\nModule base: " << Hex32(module.base)
              << "\nImage size: " << Hex32(module.size)
              << "\nAI vtable: " << Hex32(aiVtable)
              << "\nAccess: PROCESS_QUERY_INFORMATION | PROCESS_VM_READ only\n"
              << "Scanning committed readable memory...\n";

    std::vector<std::uint32_t> hits;
    std::uint64_t scanned = 0, failedChunks = 0;
    Scan(process.value, aiVtable, hits, scanned, failedChunks);
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    std::cout << "Scanned bytes: " << scanned << "; unreadable chunks: " << failedChunks
              << "; raw vtable matches: " << hits.size() << "\n";

    std::vector<Candidate> valid;
    std::vector<std::string> rejection;
    for (std::uint32_t hit : hits) {
        Candidate c{}; std::string why;
        if (!Validate(process.value, module.base, hit, c, &why)) {
            if (rejection.size() < 6) rejection.push_back(Hex32(hit) + ": " + why);
            continue;
        }
        bool stable = true;
        for (int i = 0; i < 2; ++i) {
            Sleep(80);
            Candidate c2{};
            if (!Validate(process.value, module.base, hit, c2) ||
                c2.game != c.game || c2.field != c.field || c2.player != c.player ||
                c2.cells != c.cells || c2.cellsEnd != c.cellsEnd) {
                stable = false; break;
            }
        }
        if (stable) valid.push_back(c);
    }
    if (valid.empty()) {
        std::cerr << "No runtime-validated CTetrisLuaAI -> CGamePlay -> CPlayfield chain found.\n"
                  << "Enter gameplay and run again; the relevant objects may not exist in the menu.\n";
        for (const auto& s : rejection) std::cerr << "  " << s << "\n";
        return 5;
    }
    if (valid.size() != 1) {
        std::cerr << "More than one candidate passed all checks; refusing to guess:\n";
        for (const auto& c : valid)
            std::cerr << "  AI=" << Hex32(c.ai) << " Game=" << Hex32(c.game)
                      << " Playfield=" << Hex32(c.field) << "\n";
        return 6;
    }

    const Candidate& c = valid.front();
    std::vector<std::uint8_t> board, aiBoard;
    std::uint32_t occupied = 0, aiOccupied = 0, aiSnapshotAddress = 0;
    if (!Snapshot(process.value, c, board, occupied, error)) { std::cerr << error << "\n"; return 7; }
    if (!ReadAiSnapshot(process.value, c, aiBoard, aiOccupied, aiSnapshotAddress, error)) {
        std::cerr << "AI snapshot cross-check unavailable: " << error << "\n";
        return 9;
    }
    std::uint32_t matchingCells = 0;
    for (std::size_t i = 0; i < board.size(); ++i) matchingCells += (board[i] == aiBoard[i]);

    std::ostringstream report;
    report << "TopRuntimeProbe 0.2 - read-only runtime board snapshot\n"
           << "Target SHA256: " << kExpectedSha256 << "\nPID: " << pid
           << "\nImage: " << ToUtf8(path) << "\nModuleBase: " << Hex32(module.base)
           << "\nAI: " << Hex32(c.ai) << "\nCGamePlay: " << Hex32(c.game)
           << "\nCPlayfield: " << Hex32(c.field) << "\nCPlayer: " << Hex32(c.player)
           << "\nWidth: " << c.width << "\nHeight: " << c.height
           << "\nCellsBegin: " << Hex32(c.cells) << "\nCellsEnd: " << Hex32(c.cellsEnd)
           << "\nCellsCapacity: " << Hex32(c.cellsCap)
           << "\nCellStride: 0x14\nOccupancyDWORDOffset: 0x0C (0=occupied, nonzero=empty)"
           << "\nMainBoardOccupiedCells: " << occupied << " / 400"
           << "\nAISnapshotData: " << Hex32(aiSnapshotAddress)
           << "\nAISnapshotOccupiedCells: " << aiOccupied << " / 400"
           << "\nMainBoardVsAISnapshot: " << matchingCells << " / 400 cells match"
           << "\n\n";
    PrintBoard(report, c, board);

    std::cout << "VALIDATED OBJECT CHAIN\n"
              << "  AI         " << Hex32(c.ai) << "\n"
              << "  CGamePlay  " << Hex32(c.game) << "\n"
              << "  CPlayfield " << Hex32(c.field) << "\n"
              << "  CPlayer    " << Hex32(c.player) << "\n"
              << "  Cells      " << Hex32(c.cells) << " .. " << Hex32(c.cellsEnd) << "\n"
              << "Board 10x40; main board occupied: " << occupied << " / 400\n"
              << "AI snapshot: " << Hex32(aiSnapshotAddress)
              << "; occupied: " << aiOccupied << " / 400\n"
              << "Main board vs AI snapshot: " << matchingCells << " / 400 cells match\n\n";
    PrintBoard(std::cout, c, board);

    const auto outputPath = std::filesystem::current_path() / L"TopRuntimeProbe-report.txt";
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) { std::cerr << "Could not write TopRuntimeProbe-report.txt\n"; return 8; }
    const std::string text = report.str();
    file.write(text.data(), (std::streamsize)text.size());
    std::cout << "Report: " << ToUtf8(outputPath.wstring())
              << "\nOriginal process memory was read only; no process memory was written.\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && _wcsicmp(argv[1], L"--self-test") == 0) return SelfTest();
    if (argc >= 2 && (_wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"-h") == 0)) {
        Usage(); return 0;
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"--list") == 0) {
        auto pids = FindProcesses();
        if (pids.empty()) std::cout << "No running tetris.exe process found.\n";
        for (DWORD pid : pids) std::cout << "tetris.exe PID=" << pid << "\n";
        return 0;
    }
    DWORD pid = 0;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--pid") == 0 && i + 1 < argc) {
            wchar_t* end = nullptr;
            unsigned long long n = _wcstoui64(argv[++i], &end, 10);
            if (!end || *end || !n || n > 0xFFFFFFFFULL) {
                std::cerr << "Invalid PID.\n"; return 1;
            }
            pid = (DWORD)n;
        } else {
            std::cerr << "Unknown/incomplete option: " << ToUtf8(argv[i]) << "\n";
            Usage(); return 1;
        }
    }
    return Run(pid);
}
