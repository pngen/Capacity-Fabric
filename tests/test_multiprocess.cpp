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
        char* cmdLine = cmd.data();
        if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi)) {
            CloseHandle(readH);
            CloseHandle(writePipe);
            return false;
        }
        CloseHandle(writePipe);
        h = pi.hProcess;
        readPipe = readH;
        CloseHandle(pi.hThread);
        return true;
    }

    std::string readLine(std::string& err) {
        std::string line;
        char buf[512];
        for (;;) {
            DWORD avail = 0;
            PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr);
            if (avail == 0) {
                if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { err = "process exited"; return line; }
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
struct Proc {
    bool start(const std::string&) { return false; }
    std::string readLine(std::string& e) { e = "unsupported"; return ""; }
    void kill() {}
    void wait() {}
};
#endif

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

static uint8_t queryOutcome(TcpConnection& q, const WorkloadDemand& d, uint32_t& seq) {
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

static bool waitForState(TcpConnection& q, const WorkloadDemand& d, uint32_t& seq,
                         bool (*pred)(Outcome), int maxPoll = 400) {
    for (int i = 0; i < maxPoll; ++i) {
        const Outcome o = static_cast<Outcome>(queryOutcome(q, d, seq));
        if (pred(o)) return true;
        Sleep(2);
    }
    return false;
}

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

    Proc workerA;
    Proc workerB;
    CHECK(workerA.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                        " 1 100 1 1 16384 16384"));
    CHECK(workerB.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                        " 2 200 2 2 16384 16384"));
    CHECK(workerA.readLine(err).find("READY") != std::string::npos);
    CHECK(workerB.readLine(err).find("READY") != std::string::npos);

    TcpConnection query;
    CHECK(query.connect("127.0.0.1", port, err));
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 2.0, DeviceCount(2));
    uint32_t seq = 1;
    CHECK(waitForState(query, d, seq, [](Outcome o) { return o == Outcome::FitNow; }));

    workerA.kill();
    workerA.wait();
    CHECK(waitForState(query, d, seq, [](Outcome o) { return o != Outcome::FitNow; }));

    Proc workerA2;
    CHECK(workerA2.start(quote(CF_WORKER_EXE) + " 127.0.0.1 " + std::to_string(port) +
                         " 1 300 1 1 16384 16384"));
    CHECK(workerA2.readLine(err).find("READY") != std::string::npos);
    CHECK(waitForState(query, d, seq, [](Outcome o) { return o == Outcome::FitNow; }));

    // Stale boot replay: a live incarnation (worker 1 / boot 300) must not be
    // replaced by the old incarnation (worker 1 / boot 100). The coordinator
    // rejects the REGISTER.
    {
        TcpConnection replay;
        CHECK(replay.connect("127.0.0.1", port, err));
        BinaryWriter w;
        w.writeU64(1);
        w.writeU64(100);
        FramedMessage m; m.type = MessageType::Register; m.seq = seq++; m.payload = w.take();
        std::string e2;
        CHECK(replay.sendFrame(m, e2));
        FramedMessage ack; bool closed = false;
        CHECK(replay.recvFrame(ack, e2, closed));
        CHECK(ack.type == MessageType::Register);
        CHECK(!ack.payload.empty() && ack.payload[0] != 1);
    }

    {
        BinaryWriter w;
        w.writeString("mp_saved.cf");
        FramedMessage m; m.type = MessageType::Save; m.seq = seq++; m.payload = w.take();
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage r; bool closed = false;
        CHECK(query.recvFrame(r, e2, closed));
        CHECK(r.type == MessageType::Save);
    }
    {
        FramedMessage m; m.type = MessageType::Shutdown; m.seq = seq++;
        std::string e2;
        CHECK(query.sendFrame(m, e2));
        FramedMessage ack; bool closed = false;
        CHECK(query.recvFrame(ack, e2, closed));
        CHECK(ack.type == MessageType::Shutdown);
    }
    // Detach the still-connected workers so the coordinator can join their handlers
    // cleanly during shutdown; do this before waiting on the coordinator itself.
    workerA2.kill();
    workerA2.wait();
    workerB.kill();
    workerB.wait();
    coord.wait();
}

int main() {
    testMultiprocess();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}
