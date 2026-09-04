#include "test_util.hpp"

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#endif

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/protocol/payload.hpp"
#include "capacity_fabric/protocol/tcp.hpp"
#include "capacity_fabric/query/outcome.hpp"

using namespace capacity_fabric;

#ifndef CF_COORD_EXE
#  define CF_COORD_EXE "cfcoord"
#endif
#ifndef CF_WORKER_EXE
#  define CF_WORKER_EXE "cfworker"
#endif

#ifdef _WIN32

struct Proc {
    HANDLE h = nullptr;
    HANDLE readPipe = nullptr;
    std::string exe;
    std::string args;

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
        (void)readH;

        // Copy the command line into mutable buffer for CreateProcess.
        std::vector<char> cmd(command.begin(), command.end());
        cmd.push_back('\0');
        PROCESS_INFORMATION pi{};
        // Use a copy for the caller.
        char* cmdLine = cmd.data();
        if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi)) {
            CloseHandle(readH);
            CloseHandle(writePipe);
            return false;
        }
        CloseHandle(writePipe);
        h = pi.hProcess;
        this->readPipe = readH;
        CloseHandle(pi.hThread);
        return true;
    }

    std::string readLine(std::string& err) {
        std::string line;
        char buf[512];
        // Read until we see a newline or error.
        for (;;) {
            DWORD avail = 0;
            PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr);
            if (avail == 0) {
                // Check process still alive.
                if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { err = "process exited"; return line; }
                Sleep(5);
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
    void wait() { if (h) { WaitForSingleObject(h, INFINITE); } }
    [[nodiscard]] bool exited() const { return h && WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }
    ~Proc() { if (h) CloseHandle(h); if (readPipe) CloseHandle(readPipe); }
};
#else
struct Proc {
    bool started=false;
    bool start(const std::string&) { return false; }
    std::string readLine(std::string& e) { e = "unsupported"; return ""; }
    void kill() {}
    void wait() {}
};
#endif

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

static void testMultiprocess() {
    const std::string statePath = "mp_state.cf";
    Proc coord;
    std::string err;
    CHECK(coord.start(quote(CF_COORD_EXE) + " 0 " + statePath));
    const std::string line = coord.readLine(err);
    CHECK(!line.empty());
    std::size_t pos = line.find("PORT:");
    CHECK(pos != std::string::npos);
    const uint16_t port = static_cast<uint16_t>(std::stoi(line.substr(pos + 5)));

    // Worker A (resource 1) and Worker B (resource 2).
    Proc workerA;
    Proc workerB;
    CHECK(workerA.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                        " 1 100 1 1 16384 16384"));
    CHECK(workerB.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                        " 2 200 2 2 16384 16384"));
    // Give the workers a moment to register/publish (bounded real sync, not a test timeout).
    Sleep(400);

    TcpConnection query;
    CHECK(query.connect("127.0.0.1", port, err));
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 2.0, DeviceCount(2));
    // FIT_NOW while both workers are live.
    {
        BinaryWriter w;
        protocol::payload::writeDemand(w, d);
        w.writeU8(static_cast<uint8_t>(QueryMode::Expected));
        w.writeI64(0);
        FramedMessage m; m.type = MessageType::QueryFeasibility; m.seq = 1; m.payload = w.take();
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage r; bool closed = false;
        CHECK(query.recvFrame(r, e2, closed));
        CHECK(r.type == MessageType::QueryFeasibility);
        CHECK(static_cast<Outcome>(r.payload[0]) == Outcome::FitNow);
    }

    // Kill Worker A as a real OS process.
    workerA.kill();
    workerA.wait();
    // Wait briefly for the coordinator to observe the disconnect.
    Sleep(400);

    {
        BinaryWriter w;
        protocol::payload::writeDemand(w, d);
        w.writeU8(static_cast<uint8_t>(QueryMode::Expected));
        w.writeI64(0);
        FramedMessage m; m.type = MessageType::QueryFeasibility; m.seq = 2; m.payload = w.take();
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage r; bool closed = false;
        CHECK(query.recvFrame(r, e2, closed));
        CHECK(r.type == MessageType::QueryFeasibility);
        const Outcome out = static_cast<Outcome>(r.payload[0]);
        // A demand needing BOTH resources must not be asserted as FIT_NOW after A dies.
        CHECK(out != Outcome::FitNow);
    }

    // Worker A' returns with a fresh boot identity (the same resource id, new boot 300).
    Proc workerA2;
    CHECK(workerA2.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                         " 1 300 1 1 16384 16384"));
    Sleep(400);
    {
        BinaryWriter w;
        protocol::payload::writeDemand(w, d);
        w.writeU8(static_cast<uint8_t>(QueryMode::Expected));
        w.writeI64(0);
        FramedMessage m; m.type = MessageType::QueryFeasibility; m.seq = 3; m.payload = w.take();
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage r; bool closed = false;
        CHECK(query.recvFrame(r, e2, closed));
        CHECK(r.type == MessageType::QueryFeasibility);
        CHECK(static_cast<Outcome>(r.payload[0]) == Outcome::FitNow);
    }

    // Persist, then shut down the coordinator.
    {
        BinaryWriter w;
        w.writeString("mp_saved.cf");
        FramedMessage m; m.type = MessageType::Save; m.seq = 4; m.payload = w.take();
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage r; bool closed = false;
        CHECK(query.recvFrame(r, e2, closed));
        CHECK(r.type == MessageType::Save);
    }
    {
        FramedMessage m; m.type = MessageType::Shutdown; m.seq = 5;
        std::string e2;
        CHECK(query.sendFrame(m, e2));
    }
    coord.wait();
    workerB.kill();
    workerB.wait();
}

int main() {
    testMultiprocess();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}
