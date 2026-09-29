#include "ProcessRunner.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <thread>
#endif

namespace ProcessRunner {

namespace {
    // Junta los bytes que llegan y entrega cada línea completa (sin \r ni \n).
    struct LineSplitter {
        explicit LineSplitter(const LineCallback& callback) : onLine(callback) {}

        void add(const char* data, size_t size) {
            for (size_t i = 0; i < size; ++i) {
                const char c = data[i];
                if (c == '\n' || c == '\r') flush();
                else pending.push_back(c);
            }
        }
        void flush() {
            if (!pending.empty() && onLine) onLine(juce::String::fromUTF8(pending.data(), (int) pending.size()));
            pending.clear();
        }

        const LineCallback& onLine;
        std::string pending;
    };
}

juce::String commandLine(const juce::StringArray& command) {
    juce::String result;
    for (const auto& arg : command) {
        if (result.isNotEmpty()) result << ' ';
        if (arg.isNotEmpty() && !arg.containsAnyOf(" \t\"")) {
            result << arg;
            continue;
        }
        // Entre comillas: las barras antes de una comilla (o del cierre) se duplican.
        result << '"';
        int backslashes = 0;
        for (auto p = arg.getCharPointer(); !p.isEmpty(); ++p) {
            const auto c = *p;
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            if (c == '"') result << juce::String::repeatedString("\\", backslashes * 2 + 1) << '"';
            else result << juce::String::repeatedString("\\", backslashes) << juce::String::charToString(c);
            backslashes = 0;
        }
        result << juce::String::repeatedString("\\", backslashes * 2) << '"';
    }
    return result;
}

#if JUCE_WINDOWS

int run(const juce::StringArray& command, const LineCallback& onLine, const StopCheck& shouldStop, int silenceTimeoutMs) {
    SECURITY_ATTRIBUTES inherit { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) return couldNotStart;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    // El job cierra todo el árbol de procesos al terminarlo o al cerrar su handle.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process {};

    std::wstring line = commandLine(command).toWideCharPointer();  // CreateProcessW la puede modificar
    const bool started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, nullptr, &startup, &process) != 0;
    CloseHandle(writePipe);  // si no, la lectura nunca ve el final
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!started) {
        CloseHandle(readPipe);
        if (job != nullptr) CloseHandle(job);
        return couldNotStart;
    }
    if (job != nullptr) AssignProcessToJobObject(job, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    LineSplitter splitter(onLine);
    char chunk[4096];
    auto lastOutput = juce::Time::getMillisecondCounter();
    int result = 0;
    bool exited = false;

    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            if (ReadFile(readPipe, chunk, (DWORD) juce::jmin((DWORD) sizeof(chunk), available), &read, nullptr) && read > 0) {
                splitter.add(chunk, read);
                lastOutput = juce::Time::getMillisecondCounter();
            }
            continue;
        }
        if (exited) break;  // terminó y ya no queda nada por leer

        if (shouldStop && shouldStop()) { result = stopped; break; }
        if (silenceTimeoutMs > 0 && juce::Time::getMillisecondCounter() - lastOutput > (juce::uint32) silenceTimeoutMs) {
            result = timedOut;
            break;
        }
        if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) exited = true;
    }
    splitter.flush();

    if (result == 0) {
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        result = (int) code;
    } else {
        if (job != nullptr) TerminateJobObject(job, 1);
        else TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }

    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    if (job != nullptr) CloseHandle(job);  // lo que haya quedado abierto se cierra aquí
    return result;
}

#else

int run(const juce::StringArray& command, const LineCallback& onLine, const StopCheck& shouldStop, int silenceTimeoutMs) {
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) return couldNotStart;

    // readProcessOutput espera hasta tener datos: un vigilante mata el proceso si se cancela.
    std::atomic<bool> finished { false };
    std::atomic<juce::uint32> lastOutput { juce::Time::getMillisecondCounter() };
    std::atomic<int> reason { 0 };
    std::thread watchdog([&] {
        while (!finished) {
            if (shouldStop && shouldStop()) reason = stopped;
            else if (silenceTimeoutMs > 0 && juce::Time::getMillisecondCounter() - lastOutput > (juce::uint32) silenceTimeoutMs) reason = timedOut;
            if (reason != 0) {
                process.kill();
                break;
            }
            juce::Thread::sleep(100);
        }
    });

    LineSplitter splitter(onLine);
    char c = 0;
    while (process.readProcessOutput(&c, 1) == 1) {
        splitter.add(&c, 1);
        lastOutput = juce::Time::getMillisecondCounter();
    }
    splitter.flush();
    finished = true;
    watchdog.join();
    return reason != 0 ? reason.load() : (int) process.getExitCode();
}

#endif

}
