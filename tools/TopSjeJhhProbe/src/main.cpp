#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace {
constexpr std::uint64_t kExpectedExeSize = 1654784ULL;
constexpr char kExpectedExeSha256[] =
    "9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42";
constexpr std::uint32_t kMaxIndexBytes = 32U * 1024U * 1024U;
constexpr std::uint32_t kMaxEntryBytes = 64U * 1024U * 1024U;
constexpr std::uint32_t kMaxEntries = 100000U;

std::vector<std::string> g_report;

void Log(const std::string& line) {
    g_report.push_back(line);
    std::cout << line << "\n";
}

std::string PathUtf8(const fs::path& path) {
    return path.u8string();
}

std::string Win32Error(const char* operation) {
    return std::string(operation) + " failed (Win32 error " +
           std::to_string(GetLastError()) + ")";
}

std::string LowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

bool StartsWith(const std::string& value, const char* prefix) {
    const std::string p(prefix);
    return value.size() >= p.size() && value.compare(0, p.size(), p) == 0;
}

bool EndsWith(const std::string& value, const char* suffix) {
    const std::string s(suffix);
    return value.size() >= s.size() &&
           value.compare(value.size() - s.size(), s.size(), s) == 0;
}

bool ReadBytesAt(const fs::path& path, std::uint64_t offset,
                 std::uint32_t length, std::vector<std::uint8_t>& out,
                 std::string& error) {
    std::error_code ec;
    const auto fileSize = fs::file_size(path, ec);
    if (ec) {
        error = "cannot query file size: " + ec.message();
        return false;
    }
    if (offset > fileSize || static_cast<std::uint64_t>(length) > fileSize - offset) {
        error = "requested byte range is outside the archive";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open archive";
        return false;
    }
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        error = "cannot seek to requested archive offset";
        return false;
    }
    out.assign(length, 0);
    if (length != 0) {
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(length));
        if (file.gcount() != static_cast<std::streamsize>(length)) {
            error = "short read from archive";
            return false;
        }
    }
    return true;
}

bool Rc4Transform(const std::vector<std::uint8_t>& input, const char* keyText,
                  bool finalBlock, std::vector<std::uint8_t>& output,
                  std::string& error) {
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
        error = "buffer is too large for the Windows CryptoAPI";
        return false;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HCRYPTKEY key = 0;
    auto cleanup = [&]() {
        if (key) CryptDestroyKey(key);
        if (hash) CryptDestroyHash(hash);
        if (provider) CryptReleaseContext(provider, 0);
    };

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) {
        error = Win32Error("CryptAcquireContext");
        cleanup();
        return false;
    }
    if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash)) {
        error = Win32Error("CryptCreateHash");
        cleanup();
        return false;
    }
    const DWORD keyBytes = static_cast<DWORD>(std::char_traits<char>::length(keyText));
    if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(keyText), keyBytes, 0)) {
        error = Win32Error("CryptHashData");
        cleanup();
        return false;
    }
    if (!CryptDeriveKey(provider, CALG_RC4, hash, CRYPT_CREATE_SALT, &key)) {
        error = Win32Error("CryptDeriveKey(RC4)");
        cleanup();
        return false;
    }

    DWORD required = static_cast<DWORD>(input.size());
    if (!CryptEncrypt(key, 0, finalBlock ? TRUE : FALSE, 0, nullptr, &required, 0)) {
        error = Win32Error("CryptEncrypt(size query)");
        cleanup();
        return false;
    }

    output.assign(required, 0);
    if (!input.empty()) {
        std::copy(input.begin(), input.end(), output.begin());
    }
    DWORD actual = static_cast<DWORD>(input.size());
    if (!CryptEncrypt(key, 0, finalBlock ? TRUE : FALSE, 0,
                      output.data(), &actual, required)) {
        error = Win32Error("CryptEncrypt(RC4 transform)");
        cleanup();
        return false;
    }
    output.resize(actual);
    cleanup();
    return true;
}

bool ReadU32(const std::vector<std::uint8_t>& data, std::size_t& cursor,
             std::uint32_t& value) {
    if (cursor > data.size() || data.size() - cursor < 4) return false;
    value = static_cast<std::uint32_t>(data[cursor]) |
            (static_cast<std::uint32_t>(data[cursor + 1]) << 8U) |
            (static_cast<std::uint32_t>(data[cursor + 2]) << 16U) |
            (static_cast<std::uint32_t>(data[cursor + 3]) << 24U);
    cursor += 4;
    return true;
}

void AppendU32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xffU));
    data.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    data.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    data.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void AppendStringBlob(std::vector<std::uint8_t>& data, const std::string& value) {
    const auto length = static_cast<std::uint32_t>(value.size() + 1);
    AppendU32(data, length);
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
}

bool ReadStringBlob(const std::vector<std::uint8_t>& data, std::size_t& cursor,
                    std::string& value) {
    std::uint32_t length = 0;
    if (!ReadU32(data, cursor, length) || length == 0 || length > 1024U * 1024U)
        return false;
    if (cursor > data.size() || data.size() - cursor < length) return false;
    if (data[cursor + length - 1] != 0) return false;
    value.assign(reinterpret_cast<const char*>(data.data() + cursor), length - 1);
    cursor += length;
    return true;
}

struct Entry {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    bool encrypted = false;
};

struct ArchiveIndex {
    std::string folderId;
    std::vector<Entry> entries;
    std::uint64_t fileSize = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexLength = 0;
};

bool IsEntryEncrypted(const std::string& folderId, const std::string& name) {
    // Mirrors the TOP SJE.JHH folder rules documented by the TOP-specific
    // SjeJhhUtil implementation: config/*, ai/*.lua, and ui/*.uis are encrypted.
    return StartsWith(folderId, "config") ||
           (EndsWith(name, ".lua") && StartsWith(folderId, "ai")) ||
           (EndsWith(name, ".uis") && StartsWith(folderId, "ui"));
}

bool ParseArchive(const fs::path& path, ArchiveIndex& index, std::string& error) {
    std::error_code ec;
    index.fileSize = fs::file_size(path, ec);
    if (ec || index.fileSize < 8) {
        error = ec ? "cannot query archive size: " + ec.message()
                   : "archive is smaller than the 8-byte header";
        return false;
    }

    std::vector<std::uint8_t> header;
    if (!ReadBytesAt(path, 0, 8, header, error)) return false;
    std::size_t cursor = 0;
    if (!ReadU32(header, cursor, index.indexOffset) ||
        !ReadU32(header, cursor, index.indexLength)) {
        error = "cannot read archive index header";
        return false;
    }
    if (index.indexLength == 0 || index.indexLength > kMaxIndexBytes ||
        index.indexOffset > index.fileSize ||
        static_cast<std::uint64_t>(index.indexLength) >
            index.fileSize - index.indexOffset) {
        error = "archive index range is invalid or exceeds safety limits";
        return false;
    }

    std::vector<std::uint8_t> encryptedIndex;
    if (!ReadBytesAt(path, index.indexOffset, index.indexLength, encryptedIndex, error))
        return false;

    std::vector<std::uint8_t> plainIndex;
    if (!Rc4Transform(encryptedIndex, "1111", true, plainIndex, error)) return false;

    cursor = 0;
    std::uint32_t count = 0;
    if (!ReadU32(plainIndex, cursor, count) || count > kMaxEntries) {
        error = "decrypted index has an invalid entry count";
        return false;
    }
    if (!ReadStringBlob(plainIndex, cursor, index.folderId)) {
        error = "decrypted index has an invalid internal folder identifier";
        return false;
    }

    index.entries.clear();
    index.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry entry;
        if (!ReadStringBlob(plainIndex, cursor, entry.name) ||
            !ReadU32(plainIndex, cursor, entry.offset) ||
            !ReadU32(plainIndex, cursor, entry.length)) {
            error = "decrypted index is truncated while reading entry " +
                    std::to_string(i);
            return false;
        }
        if (entry.length > kMaxEntryBytes ||
            entry.offset > index.fileSize ||
            static_cast<std::uint64_t>(entry.length) >
                index.fileSize - entry.offset) {
            error = "entry " + std::to_string(i) +
                    " points outside the archive or exceeds size limits";
            return false;
        }
        entry.encrypted = IsEntryEncrypted(index.folderId, entry.name);
        index.entries.push_back(std::move(entry));
    }
    return true;
}

bool IsSelectedEntry(const ArchiveIndex& archive, const Entry& entry) {
    if (StartsWith(archive.folderId, "ai")) return EndsWith(entry.name, ".lua");
    if (StartsWith(archive.folderId, "config")) return EndsWith(entry.name, ".ini");
    return false;
}

bool MakeSafeRelativePath(const std::string& raw, fs::path& safe) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty() || normalized.front() == '/' ||
        normalized.find(':') != std::string::npos) return false;

    std::stringstream stream(normalized);
    std::string part;
    safe.clear();
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") return false;
        if (part.find('\0') != std::string::npos) return false;
        safe /= fs::u8path(part);
    }
    return !safe.empty();
}

bool WriteBytes(const fs::path& path, const std::vector<std::uint8_t>& data,
                std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create output directory: " + ec.message();
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot create selected output file";
        return false;
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        error = "failed while writing selected output file";
        return false;
    }
    return true;
}

struct HashResult {
    bool ok = false;
    std::uint64_t size = 0;
    std::string sha256;
    std::string error;
};

std::string HexBytes(const std::vector<UCHAR>& bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (UCHAR byte : bytes) {
        out.push_back(kHex[(byte >> 4U) & 0xfU]);
        out.push_back(kHex[byte & 0xfU]);
    }
    return out;
}

HashResult HashFileSha256(const fs::path& path) {
    HashResult result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.error = Win32Error("CreateFile");
        return result;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        result.error = Win32Error("GetFileSizeEx");
        CloseHandle(file);
        return result;
    }
    result.size = static_cast<std::uint64_t>(size.QuadPart);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, hashLength = 0, returned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = "BCryptOpenAlgorithmProvider(SHA256) failed";
        CloseHandle(file);
        return result;
    }
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }
    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &returned, 0);
    if (!BCRYPT_SUCCESS(status) || hashLength != 32) {
        result.error = "BCryptGetProperty(BCRYPT_HASH_LENGTH) failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }

    std::vector<UCHAR> object(objectLength), digest(hashLength);
    status = BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                              nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = "BCryptCreateHash failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }

    std::vector<UCHAR> buffer(1024U * 1024U);
    bool readOk = true;
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &bytesRead, nullptr)) {
            result.error = Win32Error("ReadFile");
            readOk = false;
            break;
        }
        if (bytesRead == 0) break;
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
        if (!BCRYPT_SUCCESS(status)) {
            result.error = "BCryptHashData failed";
            readOk = false;
            break;
        }
    }
    if (readOk) {
        status = BCryptFinishHash(hash, digest.data(), hashLength, 0);
        if (BCRYPT_SUCCESS(status)) {
            result.sha256 = HexBytes(digest);
            result.ok = true;
        } else {
            result.error = "BCryptFinishHash failed";
        }
    }

    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

bool VerifyTargetBuild(const fs::path& root) {
    const fs::path exe = root / L"tetris.exe";
    const HashResult hash = HashFileSha256(exe);
    if (!hash.ok) {
        Log("TARGET EXE: verification failed: " + hash.error);
        return false;
    }
    Log("Target image: " + PathUtf8(exe));
    Log("Target size: " + std::to_string(hash.size));
    Log("Target SHA256: " + hash.sha256);
    if (hash.size != kExpectedExeSize || hash.sha256 != kExpectedExeSha256) {
        Log("RESULT: STOPPED — tetris.exe does not match the recorded TO-P_CHS_05 build.");
        Log("No archive was opened. This tool is intentionally pinned to the verified build.");
        return false;
    }
    Log("RESULT: EXACT MATCH to the recorded TO-P_CHS_05 executable.");
    return true;
}

std::vector<fs::path> FindCandidateArchives(const fs::path& root) {
    std::vector<fs::path> files;
    const std::array<fs::path, 2> roots = {root / L"ai", root / L"config"};
    std::set<std::wstring> seen;
    for (const auto& directory : roots) {
        std::error_code ec;
        if (!fs::exists(directory, ec) || ec || !fs::is_directory(directory, ec))
            continue;
        fs::recursive_directory_iterator it(
            directory, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) continue;
            if (LowerAscii(it->path().extension().u8string()) != ".jhh") continue;
            const std::wstring key = it->path().lexically_normal().wstring();
            if (seen.insert(key).second) files.push_back(it->path());
            if (files.size() >= 512) break;
        }
        if (files.size() >= 512) break;
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool ExtractArchive(const fs::path& archivePath, const fs::path& root,
                    const fs::path& outputRoot, std::uint32_t& selectedCount,
                    std::uint32_t& extractedCount) {
    const std::string archiveName = PathUtf8(archivePath);
    ArchiveIndex index;
    std::string error;
    if (!ParseArchive(archivePath, index, error)) {
        Log("ARCHIVE ERROR: " + archiveName + " — " + error);
        return false;
    }

    std::error_code ec;
    fs::path relativeArchive = fs::relative(archivePath, root, ec);
    if (ec || relativeArchive.empty() || relativeArchive.native().find(L"..") == 0) {
        Log("ARCHIVE ERROR: cannot form a safe relative output path for " + archiveName);
        return false;
    }
    fs::path archiveOutput = outputRoot / relativeArchive;
    archiveOutput += L".contents";

    std::uint32_t archiveSelected = 0;
    std::uint32_t archiveExtracted = 0;
    Log("ARCHIVE: " + archiveName);
    Log("  Internal folder: " + index.folderId);
    Log("  Index entries: " + std::to_string(index.entries.size()));

    for (const auto& entry : index.entries) {
        if (!IsSelectedEntry(index, entry)) continue;
        ++archiveSelected;

        fs::path safeRelative;
        if (!MakeSafeRelativePath(entry.name, safeRelative)) {
            Log("  SKIP unsafe entry path: " + entry.name);
            continue;
        }

        std::vector<std::uint8_t> payload;
        if (!ReadBytesAt(archivePath, entry.offset, entry.length, payload, error)) {
            Log("  ERROR reading " + entry.name + ": " + error);
            continue;
        }
        if (entry.encrypted) {
            std::vector<std::uint8_t> plain;
            if (!Rc4Transform(payload, "TOJ", false, plain, error)) {
                Log("  ERROR decrypting " + entry.name + ": " + error);
                continue;
            }
            payload.swap(plain);
        }

        const fs::path destination = archiveOutput / safeRelative;
        if (!WriteBytes(destination, payload, error)) {
            Log("  ERROR extracting " + entry.name + ": " + error);
            continue;
        }
        ++archiveExtracted;
        Log("  EXTRACTED: " + entry.name + " (" + std::to_string(payload.size()) +
            " bytes, encrypted=" + std::string(entry.encrypted ? "yes" : "no") + ")");
    }

    selectedCount += archiveSelected;
    extractedCount += archiveExtracted;
    Log("  Selected: " + std::to_string(archiveSelected) +
        "; extracted: " + std::to_string(archiveExtracted));
    return true;
}

bool SaveReport(const fs::path& outputRoot) {
    std::error_code ec;
    fs::create_directories(outputRoot, ec);
    if (ec) return false;
    const fs::path reportPath = outputRoot / L"TopSjeJhhProbe-report.txt";
    std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
    if (!report) return false;
    for (const auto& line : g_report) report << line << "\r\n";
    report.close();
    return static_cast<bool>(report);
}

int RunRoot(const fs::path& rootArgument, const fs::path& outArgument) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(rootArgument, ec);
    if (ec || !fs::is_directory(root)) {
        Log("ERROR: --root is not a readable directory.");
        return 64;
    }

    Log("TopSjeJhhProbe 0.1 — read-only TOP SJE.JHH selector");
    Log("Root: " + PathUtf8(root));
    if (!VerifyTargetBuild(root)) return 2;

    fs::path outputRoot;
    if (!outArgument.empty()) {
        outputRoot = fs::absolute(outArgument, ec);
        if (ec) {
            Log("ERROR: cannot resolve --out directory.");
            return 64;
        }
    } else {
        outputRoot = root.parent_path() / L"TOP-SJEJHH-Selected";
    }
    Log("Output: " + PathUtf8(outputRoot));
    Log("Scope: only .JHH archives below the TOP ai/ and config/ directories.");
    Log("Payload filter: ai/*.lua and config/*.ini only; no other archive members are extracted.");

    const auto archives = FindCandidateArchives(root);
    Log("Candidate archives found: " + std::to_string(archives.size()));
    if (archives.empty()) {
        Log("No .JHH archives were found below ai/ or config/.");
        SaveReport(outputRoot);
        return 3;
    }
    if (archives.size() >= 512) {
        Log("WARNING: scan reached the 512-archive safety cap.");
    }

    std::uint32_t parsed = 0, selected = 0, extracted = 0;
    for (const auto& archive : archives) {
        if (ExtractArchive(archive, root, outputRoot, selected, extracted)) ++parsed;
    }

    Log("SUMMARY: parsed archives=" + std::to_string(parsed) +
        ", selected entries=" + std::to_string(selected) +
        ", extracted files=" + std::to_string(extracted));
    Log("Original TOP files were opened read-only and were not modified.");
    if (!SaveReport(outputRoot)) {
        Log("WARNING: could not save the report file.");
    } else {
        Log("Report: " + PathUtf8(outputRoot / L"TopSjeJhhProbe-report.txt"));
    }
    return parsed > 0 ? 0 : 4;
}

bool WriteVector(std::ofstream& file, const std::vector<std::uint8_t>& bytes) {
    if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()),
                                   static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

int SelfTest() {
    const std::string expected = "function Start() return 42 end\n";
    std::vector<std::uint8_t> filePlain(expected.begin(), expected.end());
    std::vector<std::uint8_t> fileCipher;
    std::string error;
    if (!Rc4Transform(filePlain, "TOJ", false, fileCipher, error)) {
        std::cerr << "SELF-TEST FAIL: file encryption setup: " << error << "\n";
        return 1;
    }

    std::vector<std::uint8_t> indexPlain;
    AppendU32(indexPlain, 1);
    AppendStringBlob(indexPlain, "ai");
    AppendStringBlob(indexPlain, "probe.lua");
    AppendU32(indexPlain, 8);
    AppendU32(indexPlain, static_cast<std::uint32_t>(fileCipher.size()));

    std::vector<std::uint8_t> indexCipher;
    if (!Rc4Transform(indexPlain, "1111", true, indexCipher, error)) {
        std::cerr << "SELF-TEST FAIL: index encryption setup: " << error << "\n";
        return 1;
    }

    const fs::path temp = fs::temp_directory_path() / L"TopSjeJhhProbe-selftest.jhh";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "SELF-TEST FAIL: cannot create synthetic archive.\n";
            return 1;
        }
        std::vector<std::uint8_t> header;
        AppendU32(header, static_cast<std::uint32_t>(8 + fileCipher.size()));
        AppendU32(header, static_cast<std::uint32_t>(indexCipher.size()));
        if (!WriteVector(file, header) || !WriteVector(file, fileCipher) ||
            !WriteVector(file, indexCipher)) {
            std::cerr << "SELF-TEST FAIL: cannot write synthetic archive.\n";
            fs::remove(temp);
            return 1;
        }
    }

    ArchiveIndex archive;
    const bool parsed = ParseArchive(temp, archive, error);
    if (!parsed || archive.folderId != "ai" || archive.entries.size() != 1 ||
        archive.entries[0].name != "probe.lua" || !archive.entries[0].encrypted) {
        std::cerr << "SELF-TEST FAIL: archive index parse: "
                  << (parsed ? "unexpected index contents" : error) << "\n";
        fs::remove(temp);
        return 1;
    }

    std::vector<std::uint8_t> encryptedPayload;
    if (!ReadBytesAt(temp, archive.entries[0].offset, archive.entries[0].length,
                     encryptedPayload, error)) {
        std::cerr << "SELF-TEST FAIL: payload read: " << error << "\n";
        fs::remove(temp);
        return 1;
    }
    std::vector<std::uint8_t> decryptedPayload;
    if (!Rc4Transform(encryptedPayload, "TOJ", false, decryptedPayload, error)) {
        std::cerr << "SELF-TEST FAIL: payload decrypt: " << error << "\n";
        fs::remove(temp);
        return 1;
    }
    fs::remove(temp);

    const std::string actual(decryptedPayload.begin(), decryptedPayload.end());
    if (actual != expected) {
        std::cerr << "SELF-TEST FAIL: decrypted payload differs from source text.\n";
        return 1;
    }
    std::cout << "PASS: SJE.JHH index parsing and TOP-style selected Lua decryption round trip.\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc == 2 && _wcsicmp(argv[1], L"--self-test") == 0) return SelfTest();

    fs::path root;
    fs::path output;
    bool haveRoot = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--root") == 0 && i + 1 < argc) {
            root = fs::path(argv[++i]);
            haveRoot = true;
        } else if (_wcsicmp(argv[i], L"--out") == 0 && i + 1 < argc) {
            output = fs::path(argv[++i]);
        } else {
            std::cerr << "Usage: TopSjeJhhProbe.exe --root <TO-P_CHS_05-folder> [--out <output-folder>]\n"
                         "       TopSjeJhhProbe.exe --self-test\n";
            return 64;
        }
    }
    if (!haveRoot) {
        std::cerr << "Usage: TopSjeJhhProbe.exe --root <TO-P_CHS_05-folder> [--out <output-folder>]\n"
                     "       TopSjeJhhProbe.exe --self-test\n";
        return 64;
    }
    return RunRoot(root, output);
}
