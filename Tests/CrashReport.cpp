// Si las pruebas se caen, escribe en la salida de errores dónde: la pila de llamadas, con el
// nombre de cada función y su línea cuando hay .pdb (compilación RelWithDebInfo o Debug).
// Va aparte de RaaadioTests.cpp porque necesita <windows.h>, que choca con los nombres de JUCE.

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI reportCrash(EXCEPTION_POINTERS* info)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    std::fprintf(stderr, "\n*** Caída: excepción 0x%08lX en %p\n",
                 info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
    for (int depth = 0; depth < 48; ++depth)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 address = frame.AddrPC.Offset;
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        const char* name = SymFromAddr(process, address, &displacement, symbol) ? symbol->Name : "?";

        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
            std::fprintf(stderr, "  %s  (%s:%lu)\n", name, line.FileName, line.LineNumber);
        else
            std::fprintf(stderr, "  %s  +0x%llx\n", name, (unsigned long long) displacement);
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashReport()
{
    SetUnhandledExceptionFilter(reportCrash);
}
#else
void installCrashReport() {}
#endif
