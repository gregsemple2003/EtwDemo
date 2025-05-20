#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <dbghelp.h>
#include <stdint.h>
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <cassert>
#include <iostream>

struct TreeNode {
    std::string symbol;
    uint64_t count = 0;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
};

static std::mutex g_treeMutex;
static TreeNode* g_root = new TreeNode();

static void AddCallstack(const std::vector<ULONG64>& stack, SYMBOL_INFO* symInfo) {
    std::lock_guard<std::mutex> lock(g_treeMutex);
    TreeNode* node = g_root;
    for (ULONG64 addr : stack) {
        SymFromAddr(GetCurrentProcess(), addr, nullptr, symInfo);
        std::string name(symInfo->Name, symInfo->NameLen);
        if (!node->left) node->left = new TreeNode{ name };
        node = node->left;
        node->count++;
    }
}

static void BusyWorker(std::atomic<bool>& running) {
    volatile uint64_t v = 0;
    while (running.load()) {
        for (int i = 0; i < 100000; ++i) {
            v += i;
        }
    }
}

static std::atomic<bool> g_running{true};
static std::atomic<int> g_stackCount{0};

// ProcessTrace callback
static VOID WINAPI StackEvent(PEVENT_RECORD record) {
    if (record->EventHeader.ProcessId != GetCurrentProcessId()) return;
    if (record->EventHeader.EventDescriptor.Opcode != 46) return; // Sampled profile
    DWORD pid = record->EventHeader.ProcessId;
    (void)pid;
    auto info = (PEVENT_TRACE_STACK_TRACE32)record->UserData;
    if (info->Depth == 0) return;
    std::vector<ULONG64> stack;
    for (ULONG i = 0; i < info->Depth; ++i) {
        stack.push_back(info->StackTrace[i]);
    }
    char buffer[sizeof(SYMBOL_INFO) + 256];
    PSYMBOL_INFO symInfo = (PSYMBOL_INFO)buffer;
    symInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
    symInfo->MaxNameLen = 255;
    AddCallstack(stack, symInfo);
    ++g_stackCount;
}

static void StartSession(TRACEHANDLE& hTrace, TRACEHANDLE& hSession) {
    EVENT_TRACE_PROPERTIES* props;
    const int BUFSIZE = sizeof(EVENT_TRACE_PROPERTIES) + 2 * MAX_PATH;
    props = (EVENT_TRACE_PROPERTIES*)calloc(1, BUFSIZE);
    props->Wnode.BufferSize = BUFSIZE;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    StartTrace(&hSession, L"EtwDemoSession", props);
    EnableTraceEx2(hSession, &SystemTraceControlGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                   TRACE_LEVEL_INFORMATION, KERNEL_LOGGER_USE_PMCC, 0, 0, nullptr);
    EVENT_TRACE_LOGFILE logfile = {};
    logfile.LoggerName = const_cast<LPWSTR>(L"EtwDemoSession");
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = StackEvent;
    hTrace = OpenTrace(&logfile);
}

static void StopSession(TRACEHANDLE hTrace, TRACEHANDLE hSession) {
    ControlTrace(hSession, L"EtwDemoSession", nullptr, EVENT_TRACE_CONTROL_STOP);
    CloseTrace(hTrace);
}

int main() {
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    TRACEHANDLE hTrace = 0, hSession = 0;
    StartSession(hTrace, hSession);
    std::thread processor([&](){ ProcessTrace(&hTrace, 1, nullptr, nullptr); });
    const int NUM_THREADS = 4;
    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_THREADS; ++i)
        workers.emplace_back(BusyWorker, std::ref(g_running));
    Sleep(2000);
    g_running = false;
    for (auto& t : workers) t.join();
    StopSession(hTrace, hSession);
    processor.join();
    // Validate that at least one non-empty call stack was captured
    assert(g_stackCount.load() > 0);
    assert(g_root->left && g_root->left->count > 0);
    std::cout << "Sampled " << g_stackCount.load() << " stacks\n";
    return 0;
}
