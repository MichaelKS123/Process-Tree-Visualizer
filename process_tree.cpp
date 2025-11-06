/*
 * Process Tree Visualizer - C++ Implementation
 * Created by: Michael Semera
 *
 * A cross-platform system utility that displays running processes in a 
 * hierarchical tree structure using native system APIs.
 *
 * Supported Platforms: Linux, macOS, Windows
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory>
#include <ctime>
#include <cstring>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <tlhelp32.h>
    #include <psapi.h>
    #pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
    #include <libproc.h>
    #include <sys/proc_info.h>
    #include <unistd.h>
#else  // Linux
    #include <dirent.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <cstdlib>
#endif

// ANSI color codes for terminal output
namespace Color {
    const std::string RESET   = "\033[0m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string WHITE   = "\033[37m";
    const std::string BRIGHT  = "\033[1m";
}

/**
 * Structure to hold process information
 */
struct ProcessInfo {
    int pid;
    int ppid;
    std::string name;
    std::string status;
    double cpu_percent;
    size_t memory_kb;
    int num_threads;
    std::string username;
    std::vector<ProcessInfo*> children;
    
    ProcessInfo() : pid(0), ppid(0), cpu_percent(0.0), memory_kb(0), num_threads(0) {}
    
    /**
     * Format memory in human-readable form
     */
    std::string formatMemory() const {
        if (memory_kb >= 1024 * 1024) {
            return std::to_string(memory_kb / (1024 * 1024)) + "GB";
        } else if (memory_kb >= 1024) {
            return std::to_string(memory_kb / 1024) + "MB";
        }
        return std::to_string(memory_kb) + "KB";
    }
};

/**
 * Main ProcessTree class for collecting and displaying process information
 */
class ProcessTree {
private:
    std::map<int, ProcessInfo> processes;
    std::vector<ProcessInfo*> rootProcesses;
    bool showResources;
    bool verbose;
    int totalProcesses;
    int collectionErrors;
    
    /**
     * Linux-specific: Read process information from /proc filesystem
     */
#ifdef __linux__
    bool readProcessInfo(int pid, ProcessInfo& info) {
        info.pid = pid;
        
        // Read /proc/[pid]/stat for basic process info
        std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream statFile(statPath);
        if (!statFile.is_open()) {
            return false;
        }
        
        std::string line;
        std::getline(statFile, line);
        
        // Parse the stat file (format: pid (name) state ppid ...)
        size_t nameStart = line.find('(');
        size_t nameEnd = line.rfind(')');
        if (nameStart != std::string::npos && nameEnd != std::string::npos) {
            info.name = line.substr(nameStart + 1, nameEnd - nameStart - 1);
        }
        
        // Get PPID (4th field after name)
        std::istringstream iss(line.substr(nameEnd + 2));
        std::string state;
        iss >> state >> info.ppid;
        info.status = state;
        
        // Read /proc/[pid]/status for additional info
        std::string statusPath = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream statusFile(statusPath);
        if (statusFile.is_open()) {
            std::string statusLine;
            while (std::getline(statusFile, statusLine)) {
                if (statusLine.find("VmRSS:") == 0) {
                    std::istringstream vmrss(statusLine);
                    std::string label;
                    vmrss >> label >> info.memory_kb;
                } else if (statusLine.find("Threads:") == 0) {
                    std::istringstream threads(statusLine);
                    std::string label;
                    threads >> label >> info.num_threads;
                } else if (statusLine.find("Uid:") == 0) {
                    // Could extract UID here
                }
            }
        }
        
        return true;
    }
    
    void collectProcesses() {
        std::cout << Color::CYAN << "Collecting process information..." << Color::RESET << std::endl;
        
        DIR* procDir = opendir("/proc");
        if (!procDir) {
            std::cerr << Color::RED << "Error: Cannot open /proc directory" << Color::RESET << std::endl;
            return;
        }
        
        struct dirent* entry;
        while ((entry = readdir(procDir)) != nullptr) {
            // Check if directory name is a number (PID)
            if (entry->d_type == DT_DIR) {
                int pid = atoi(entry->d_name);
                if (pid > 0) {
                    ProcessInfo info;
                    if (readProcessInfo(pid, info)) {
                        processes[pid] = info;
                        totalProcesses++;
                    } else {
                        collectionErrors++;
                    }
                }
            }
        }
        closedir(procDir);
        
        std::cout << Color::GREEN << "Collected " << processes.size() << " processes" << Color::RESET << std::endl;
    }
#endif

    /**
     * macOS-specific: Use libproc APIs
     */
#ifdef __APPLE__
    bool readProcessInfo(int pid, ProcessInfo& info) {
        info.pid = pid;
        
        struct proc_bsdinfo proc;
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, sizeof(proc)) <= 0) {
            return false;
        }
        
        info.ppid = proc.pbi_ppid;
        info.name = proc.pbi_comm;
        info.status = proc.pbi_status == SRUN ? "R" : "S";
        
        // Get task info for memory
        struct proc_taskinfo task;
        if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task, sizeof(task)) > 0) {
            info.memory_kb = task.pti_resident_size / 1024;
            info.num_threads = task.pti_threadnum;
        }
        
        return true;
    }
    
    void collectProcesses() {
        std::cout << Color::CYAN << "Collecting process information..." << Color::RESET << std::endl;
        
        int numPids = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        std::vector<int> pids(numPids * 2);
        
        numPids = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), pids.size() * sizeof(int));
        if (numPids <= 0) {
            std::cerr << Color::RED << "Error getting process list" << Color::RESET << std::endl;
            return;
        }
        
        for (int i = 0; i < numPids; i++) {
            if (pids[i] == 0) continue;
            
            ProcessInfo info;
            if (readProcessInfo(pids[i], info)) {
                processes[pids[i]] = info;
                totalProcesses++;
            } else {
                collectionErrors++;
            }
        }
        
        std::cout << Color::GREEN << "Collected " << processes.size() << " processes" << Color::RESET << std::endl;
    }
#endif

    /**
     * Windows-specific: Use CreateToolhelp32Snapshot API
     */
#ifdef _WIN32
    std::string wideToString(const WCHAR* wstr) {
        if (!wstr) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        std::string str(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size, nullptr, nullptr);
        return str;
    }
    
    bool readProcessInfo(DWORD pid, ProcessInfo& info) {
        info.pid = pid;
        
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) {
            return false;
        }
        
        // Get process name
        WCHAR processName[MAX_PATH];
        if (GetModuleBaseNameW(hProcess, nullptr, processName, MAX_PATH)) {
            info.name = wideToString(processName);
        }
        
        // Get memory info
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            info.memory_kb = pmc.WorkingSetSize / 1024;
        }
        
        // Get thread count
        DWORD threadCount = 0;
        HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hThreadSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te32;
            te32.dwSize = sizeof(THREADENTRY32);
            if (Thread32First(hThreadSnap, &te32)) {
                do {
                    if (te32.th32OwnerProcessID == pid) {
                        threadCount++;
                    }
                } while (Thread32Next(hThreadSnap, &te32));
            }
            CloseHandle(hThreadSnap);
        }
        info.num_threads = threadCount;
        
        CloseHandle(hProcess);
        return true;
    }
    
    void collectProcesses() {
        std::cout << Color::CYAN << "Collecting process information..." << Color::RESET << std::endl;
        
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << Color::RED << "Error: Cannot create process snapshot" << Color::RESET << std::endl;
            return;
        }
        
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                ProcessInfo info;
                info.pid = pe32.th32ProcessID;
                info.ppid = pe32.th32ParentProcessID;
                info.name = wideToString(pe32.szExeFile);
                info.num_threads = pe32.cntThreads;
                
                // Try to get more detailed info
                readProcessInfo(pe32.th32ProcessID, info);
                
                processes[info.pid] = info;
                totalProcesses++;
                
            } while (Process32NextW(hSnapshot, &pe32));
        }
        
        CloseHandle(hSnapshot);
        std::cout << Color::GREEN << "Collected " << processes.size() << " processes" << Color::RESET << std::endl;
    }
#endif

    /**
     * Build the hierarchical tree structure
     */
    void buildTree() {
        // Link children to parents
        for (auto& [pid, proc] : processes) {
            if (processes.find(proc.ppid) != processes.end()) {
                processes[proc.ppid].children.push_back(&proc);
            } else {
                // Root process
                rootProcesses.push_back(&proc);
            }
        }
        
        // Sort children by PID
        for (auto& [pid, proc] : processes) {
            std::sort(proc.children.begin(), proc.children.end(),
                     [](const ProcessInfo* a, const ProcessInfo* b) {
                         return a->pid < b->pid;
                     });
        }
        
        // Sort root processes
        std::sort(rootProcesses.begin(), rootProcesses.end(),
                 [](const ProcessInfo* a, const ProcessInfo* b) {
                     return a->pid < b->pid;
                 });
    }
    
    /**
     * Recursively display process tree
     */
    void displayTree(ProcessInfo* proc, const std::string& prefix, 
                    bool isLast, std::set<int>& visited) {
        if (!proc || visited.count(proc->pid)) return;
        visited.insert(proc->pid);
        
        std::string connector = isLast ? "└── " : "├── ";
        
        // Color code by status
        std::string nameColor = Color::CYAN;
        if (proc->status == "R" || proc->status == "running") {
            nameColor = Color::GREEN;
        } else if (proc->status == "Z" || proc->status == "zombie") {
            nameColor = Color::RED;
        }
        
        // Display process info
        std::cout << prefix << connector 
                  << nameColor << Color::BRIGHT << proc->name << Color::RESET
                  << Color::YELLOW << " [PID: " << proc->pid << "]" << Color::RESET;
        
        if (showResources) {
            std::string cpuColor = proc->cpu_percent > 50 ? Color::RED : Color::GREEN;
            std::string memColor = proc->memory_kb > 500*1024 ? Color::RED : Color::YELLOW;
            
            std::cout << " " << cpuColor << "CPU: " << std::fixed << std::setprecision(1) 
                      << proc->cpu_percent << "%" << Color::RESET
                      << " " << memColor << "MEM: " << proc->formatMemory() << Color::RESET;
        }
        
        if (verbose) {
            std::cout << " " << Color::BLUE << "Threads: " << proc->num_threads << Color::RESET;
        }
        
        std::cout << std::endl;
        
        // Display children
        for (size_t i = 0; i < proc->children.size(); i++) {
            bool isLastChild = (i == proc->children.size() - 1);
            std::string extension = isLast ? "    " : "│   ";
            displayTree(proc->children[i], prefix + extension, isLastChild, visited);
        }
    }

public:
    ProcessTree(bool resources = false, bool verb = false) 
        : showResources(resources), verbose(verb), totalProcesses(0), collectionErrors(0) {}
    
    /**
     * Main execution flow
     */
    void run() {
        collectProcesses();
        buildTree();
        displayHeader();
        
        std::set<int> visited;
        for (auto* root : rootProcesses) {
            displayTree(root, "", true, visited);
        }
    }
    
    /**
     * Display header information
     */
    void displayHeader() {
        std::cout << "\n" << Color::CYAN << Color::BRIGHT 
                  << "======================================================================" 
                  << Color::RESET << "\n";
        std::cout << Color::CYAN << Color::BRIGHT << "Process Tree Visualizer" << Color::RESET << "\n";
        std::cout << Color::CYAN << "Created by: Michael Semera" << Color::RESET << "\n";
        
        time_t now = time(nullptr);
        char buf[80];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        std::cout << Color::CYAN << "Timestamp: " << buf << Color::RESET << "\n";
        std::cout << Color::CYAN << "Total Processes: " << processes.size() << Color::RESET << "\n";
        std::cout << Color::CYAN << Color::BRIGHT 
                  << "======================================================================" 
                  << Color::RESET << "\n\n";
    }
    
    /**
     * Find process by PID
     */
    ProcessInfo* findProcess(int pid) {
        auto it = processes.find(pid);
        return it != processes.end() ? &it->second : nullptr;
    }
    
    /**
     * Display process and its subtree
     */
    void displayProcessSubtree(int pid) {
        ProcessInfo* proc = findProcess(pid);
        if (!proc) {
            std::cout << Color::RED << "Process with PID " << pid << " not found" 
                      << Color::RESET << std::endl;
            return;
        }
        
        std::cout << "\n" << Color::CYAN << "Process Subtree for: " << Color::BRIGHT 
                  << proc->name << Color::RESET << "\n";
        std::cout << Color::CYAN << "======================================================================" 
                  << Color::RESET << "\n\n";
        
        std::set<int> visited;
        displayTree(proc, "", true, visited);
    }
    
    /**
     * Export tree to file
     */
    void exportToFile(const std::string& filename) {
        std::ofstream outFile(filename);
        if (!outFile.is_open()) {
            std::cerr << Color::RED << "Error: Cannot open file " << filename 
                      << Color::RESET << std::endl;
            return;
        }
        
        // Redirect cout to file
        std::streambuf* coutbuf = std::cout.rdbuf();
        std::cout.rdbuf(outFile.rdbuf());
        
        displayHeader();
        std::set<int> visited;
        for (auto* root : rootProcesses) {
            displayTree(root, "", true, visited);
        }
        
        // Restore cout
        std::cout.rdbuf(coutbuf);
        outFile.close();
        
        std::cout << Color::GREEN << "Process tree exported to " << filename 
                  << Color::RESET << std::endl;
    }
};

/**
 * Display usage information
 */
void printUsage(const char* progName) {
    std::cout << "Process Tree Visualizer - Created by Michael Semera\n\n";
    std::cout << "Usage: " << progName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help         Show this help message\n";
    std::cout << "  -r, --resources    Show CPU and memory usage\n";
    std::cout << "  -v, --verbose      Show verbose process information\n";
    std::cout << "  -p, --pid PID      Show specific process and its children\n";
    std::cout << "  -o, --output FILE  Export process tree to file\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << "                    # Display full process tree\n";
    std::cout << "  " << progName << " -r                 # Show with resource usage\n";
    std::cout << "  " << progName << " -p 1234            # Show specific process\n";
    std::cout << "  " << progName << " -o tree.txt        # Export to file\n\n";
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    bool showResources = false;
    bool verbose = false;
    int targetPid = -1;
    std::string outputFile;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-r" || arg == "--resources") {
            showResources = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if ((arg == "-p" || arg == "--pid") && i + 1 < argc) {
            targetPid = std::atoi(argv[++i]);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputFile = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    try {
        ProcessTree tree(showResources, verbose);
        tree.run();
        
        if (targetPid >= 0) {
            tree.displayProcessSubtree(targetPid);
        }
        
        if (!outputFile.empty()) {
            tree.exportToFile(outputFile);
        }
        
    } catch (const std::exception& e) {
        std::cerr << Color::RED << "Error: " << e.what() << Color::RESET << std::endl;
        return 1;
    }
    
    return 0;
}