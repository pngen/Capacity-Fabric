#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include "cuda_bridge.h"
#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"
#include "capacity_fabric/query/outcome.hpp"

namespace capacity_fabric::cuda {
int runCudaProof();
}

#ifndef CF_COORD_EXE
#  define CF_COORD_EXE "cfcoord"
#endif
#ifndef CF_CUDA_WORKER_EXE
#  define CF_CUDA_WORKER_EXE "cf_cuda_worker"
#endif

namespace {

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

#ifdef _WIN32
struct Proc {
    HANDLE h = nullptr;
    HANDLE readPipe = nullptr;

    bool start(const std::string& command) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE readH = nullptr, writePipe = nullptr;
        if (!CreatePipe(&readH, &writePipe, &sa, 0)) return false;
        SetHandleInformation(readH, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        std::vector<char> cmd(command.begin(), command.end());
        cmd.push_back('\0');
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi)) { CloseHandle(readH); CloseHandle(writePipe); return false; }
        CloseHandle(writePipe);
        h = pi.hProcess; readPipe = readH; CloseHandle(pi.hThread);
        return true;
    }
    std::string readLine(std::string& err) {
        std::string line;
        char buf[512];
        for (;;) {
            DWORD avail = 0;
            PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr);
            if (avail == 0) {
                if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { err = "exited"; return line; }
                Sleep(2);
                continue;
            }
            DWORD n = 0;
            if (!ReadFile(readPipe, buf, sizeof(buf) - 1, &n, nullptr) || n == 0) break;
            buf[n] = '\0';
            line.append(buf, n);
            if (line.find('\n') != std::string::npos) break;
        }
        return line;
    }
    void kill() { if (h) TerminateProcess(h, 1); }
    void wait() { if (h) WaitForSingleObject(h, INFINITE); }
    ~Proc() { if (h) CloseHandle(h); if (readPipe) CloseHandle(readPipe); }
};
#else
struct Proc { bool start(const std::string&) { return false; } std::string readLine(std::string& e){e="x";return "";} void kill(){} void wait(){} };
#endif

using namespace capacity_fabric;

uint8_t queryOutcome(TcpConnection& q, const WorkloadDemand& d, uint32_t& seq) {
    BinaryWriter w;
    protocol::payload::writeDemand(w, d);
    w.writeU8(static_cast<uint8_t>(QueryMode::Expected));
    w.writeI64(0);
    FramedMessage m; m.type = MessageType::QueryFeasibility; m.seq = seq++; m.payload = w.take();
    std::string e;
    if (!q.sendFrame(m, e)) return 255;
    FramedMessage r; bool closed = false;
    if (!q.recvFrame(r, e, closed)) return 255;
    return r.payload.empty() ? 255 : r.payload[0];
}

bool waitForState(TcpConnection& q, const WorkloadDemand& d, uint32_t& seq,
                  bool (*pred)(Outcome), int maxPoll = 400) {
    for (int i = 0; i < maxPoll; ++i) {
        const Outcome o = static_cast<Outcome>(queryOutcome(q, d, seq));
        if (pred(o)) return true;
        Sleep(2);
    }
    return false;
}

WorkloadDemand makeCUDA_basedDemand(WorkloadDemandId id) {
    WorkloadDemand d = reference::makeDemand(id, WorkloadDemandGeneration(1), 1.0, DeviceCount(1), true,
                                             ByteCount(1u << 20));
    d.requirements.setVram(ByteCount(1u << 20));
    return d;
}

void runCudaWorkerDeath() {
    std::size_t baseFree = 0;
    std::string err;
    capacity_fabric::cuda::currentFreeBytes(baseFree, err);

    Proc coord;
    if (!coord.start(quote(CF_COORD_EXE) + " 0")) { std::printf("[CUDA] E: FAILED to start coordinator\n"); return; }
    const std::string line = coord.readLine(err);
    const std::size_t p = line.find("PORT:");
    if (p == std::string::npos) { std::printf("[CUDA] E: FAILED to get port\n"); coord.kill(); return; }
    const uint16_t port = static_cast<uint16_t>(std::stoi(line.substr(p + 5)));

    // Worker A (worker 1, boot 100) publishes REAL CUDA evidence.
    Proc workerA;
    if (!workerA.start(quote(CF_CUDA_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) + " 1 100 1")) {
        std::printf("[CUDA] E: FAILED to start worker A\n"); coord.wait(); return;
    }
    std::string readyErr;
    if (workerA.readLine(readyErr).find("READY") == std::string::npos) {
        std::printf("[CUDA] E: worker A not ready: %s\n", readyErr.c_str());
    }

    TcpConnection q;
    if (!q.connect("127.0.0.1", port, err)) { std::printf("[CUDA] E: query connect fail\n"); return; }
    const WorkloadDemand d = makeCUDA_basedDemand(WorkloadDemandId(100));
    uint32_t seq = 1;

    const bool fitLive = waitForState(q, d, seq, [](Outcome o) { return o == Outcome::FitNow; });
    std::printf("[CUDA] E: worker live (real CUDA evidence) -> %s\n", fitLive ? "FIT_NOW" : "NOT_FIT_NOW");

    // Bounded real CUDA work while the evidence is authoritative.
    const std::size_t poolBytes = (baseFree / 8) < (256u << 20) ? (baseFree / 8) : (256u << 20);
    void* bufA = nullptr;
    if (capacity_fabric::cuda::allocate(poolBytes, &bufA, err)) {
        std::vector<float> in(1u << 20, 1.0f);
        if (capacity_fabric::cuda::kernelAndVerify(in.data(), in.size(), err)) {
            std::printf("[CUDA] E: real CUDA work parity OK (REAL)\n");
        } else {
            std::printf("[CUDA] E: CUDA work parity FAILED: %s\n", err.c_str());
        }
        capacity_fabric::cuda::release(bufA, err);
    }

    // Kill Worker A as a real OS process.
    workerA.kill();
    workerA.wait();
    const bool staleDetected = waitForState(q, d, seq, [](Outcome o) { return o != Outcome::FitNow; });
    const Outcome dead = static_cast<Outcome>(queryOutcome(q, d, seq));
    std::printf("[CUDA] E: worker killed -> %s (outcome %s)\n",
                staleDetected ? "REVALIDATION_REQUIRED" : "STILL_FIT", to_string(dead));

    // Worker A' returns with a fresh boot identity.
    Proc workerA2;
    if (!workerA2.start(quote(CF_CUDA_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) + " 1 300 1")) {
        std::printf("[CUDA] E: FAILED to start worker A'\n"); coord.wait(); return;
    }
    if (workerA2.readLine(readyErr).find("READY") == std::string::npos) {
        std::printf("[CUDA] E: worker A' not ready: %s\n", readyErr.c_str());
    }
    const bool fitRestored = waitForState(q, d, seq, [](Outcome o) { return o == Outcome::FitNow; });
    std::printf("[CUDA] E: worker restarted (fresh boot) -> %s\n", fitRestored ? "FIT_NOW" : "NOT_FIT_NOW");

    // Stale boot replay must be rejected now that a live incarnation (boot 300)
    // is authoritative; a replay of the dead boot 100 must not replace it.
    bool replayRejected = false;
    {
        TcpConnection replay;
        if (replay.connect("127.0.0.1", port, err)) {
            BinaryWriter w;
            w.writeU64(1);
            w.writeU64(100);
            FramedMessage m; m.type = MessageType::Register; m.seq = seq++; m.payload = w.take();
            std::string e2;
            if (replay.sendFrame(m, e2)) {
                FramedMessage ack; bool closed = false;
                if (replay.recvFrame(ack, e2, closed) && ack.type == MessageType::Register) {
                    replayRejected = ack.payload.empty() || ack.payload[0] != 1;
                }
            }
        }
    }
    std::printf("[CUDA] E: stale WorkerBootId replay rejected=%s\n", replayRejected ? "true" : "false");

    // Real CUDA work under fresh authority.
    void* bufB = nullptr;
    if (capacity_fabric::cuda::allocate(poolBytes, &bufB, err)) {
        std::vector<float> in(1u << 20, 2.0f);
        if (capacity_fabric::cuda::kernelAndVerify(in.data(), in.size(), err)) {
            std::printf("[CUDA] E: real CUDA work under fresh authority parity OK\n");
        }
        capacity_fabric::cuda::release(bufB, err);
    }

    std::size_t endFree = 0;
    capacity_fabric::cuda::currentFreeBytes(endFree, err);
    std::printf("[CUDA] E: baseline free=%zu final free=%zu\n", baseFree, endFree);

    // Shut the coordinator down cleanly.
    {
        FramedMessage m; m.type = MessageType::Shutdown; m.seq = seq++;
        std::string e2;
        q.sendFrame(m, e2);
        FramedMessage ack; bool closed = false;
        q.recvFrame(ack, e2, closed);
    }
    workerA2.kill();
    workerA2.wait();
    coord.wait();
}

}  // namespace

int main() {
    std::printf("=== Capacity Fabric CUDA hardware proof (RTX 5090 / sm_120) ===\n");
    const int rc = capacity_fabric::cuda::runCudaProof();
#ifdef _WIN32
    runCudaWorkerDeath();
#endif
    std::printf("=== END ===\n");
    return rc;
}
