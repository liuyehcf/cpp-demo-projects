#include <cxxabi.h>
#include <dlfcn.h>
#include <libunwind.h>
#include <ucontext.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>

enum class UWN_MODE {
    UNW_INIT_LOCAL_WITHOUT_CONTEXT,
    UNW_INIT_LOCAL_WITH_CONTEXT,
    UNW_BACKTRACE,
};
static UWN_MODE unw_mode;
static thread_local ucontext_t* g_last_ucontext = nullptr;
// Separate user-space stack (fiber) used when CONTEXT_MODE=bad.
// Running the unwinder on this stack means the backtrace starts from a
// different stack and cannot jump back to the interrupted core stack
// across the signal frame.
static char g_bt_stack[64 * 1024];

void print_symbol(const char* sym, const unw_word_t& pc, const unw_word_t& offset) {
    int status;
    // Attempt to demangle the symbol
    char* demangled_name = abi::__cxa_demangle(sym, nullptr, nullptr, &status);

    std::cout << "0x" << std::hex << pc << ": ";

    if (status == 0 && (demangled_name != nullptr)) {
        std::cout << demangled_name << " (+0x" << std::hex << offset << ")";
        free(demangled_name); // Free the demangled name
    } else {
        // If demangling failed, print the mangled name
        std::cout << sym << " (+0x" << std::hex << offset << ")";
    }
}

std::string exec_capture(const std::string& cmd) {
    std::array<char, 256> buffer;
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return "";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    pclose(pipe);
    return result;
}

void print_source_line(unw_word_t pc) {
    Dl_info info;
    if ((dladdr(reinterpret_cast<void*>(pc) /* NOLINT(performance-no-int-to-ptr) */, &info) != 0) &&
        (info.dli_fname != nullptr)) {
        std::cout << " at ";
        std::ostringstream cmd;
        auto base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        auto rel = static_cast<uintptr_t>(pc) - base;
        cmd << "addr2line -e " << info.dli_fname << " -C -p 0x" << std::hex << rel;
        // Print source line (function and file:line) using addr2line
        // std::system(cmd.str().c_str());
        std::cout << exec_capture(cmd.str());
    }
}

void print_frame(const char* sym, const unw_word_t& pc, const unw_word_t& offset) {
    print_symbol(sym, pc, offset);
    print_source_line(pc);
}

void print_stacktrace_with_unw_init_local_without_context() {
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    // Walk the stack up, one frame at a time.
    int step_res = 0;
    while ((step_res = unw_step(&cursor)) > 0) {
        unw_word_t offset;
        unw_word_t pc;
        char sym[256];

        if (unw_get_reg(&cursor, UNW_REG_IP, &pc)) {
            std::cerr << "Error: cannot read program counter" << std::endl;
            break;
        }

        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
            print_frame(sym, pc, offset);
        } else {
            std::cerr << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

void print_stacktrace_with_unw_init_local_with_context() {
    unw_cursor_t cursor;

    if (g_last_ucontext == nullptr) {
        std::cerr << "Error: no ucontext available" << std::endl;
        return;
    }
    unw_init_local(&cursor, reinterpret_cast<unw_context_t*>(g_last_ucontext));

    while (unw_step(&cursor) > 0) {
        unw_word_t offset;
        unw_word_t pc;
        char sym[256];

        if (unw_get_reg(&cursor, UNW_REG_IP, &pc)) {
            std::cerr << "Error: cannot read program counter" << std::endl;
            break;
        }

        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
            print_symbol(sym, pc, offset);
            print_source_line(pc);
        } else {
            std::cerr << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

void print_stacktrace_unw_backtrace() {
    constexpr int kMaxFrames = 256;
    void* buffer[kMaxFrames];

    int nframes = unw_backtrace(buffer, kMaxFrames);
    if (nframes <= 0) {
        std::cerr << "Error: unw_backtrace failed" << std::endl;
        return;
    }

    for (int i = 0; i < nframes; ++i) {
        auto pc = reinterpret_cast<unw_word_t>(buffer[i]);

        Dl_info info;
        if ((dladdr(buffer[i], &info) != 0) && (info.dli_sname != nullptr)) {
            const char* sym = info.dli_sname;
            unw_word_t offset = pc - reinterpret_cast<unw_word_t>(info.dli_saddr);
            print_frame(sym, pc, offset);
        } else {
            std::cerr << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

void run_on_fiber() {
    switch (unw_mode) {
    case UWN_MODE::UNW_INIT_LOCAL_WITHOUT_CONTEXT:
        std::cerr << "Stack trace (unw_init_local_without_context):" << std::endl;
        print_stacktrace_with_unw_init_local_without_context();
        break;
    case UWN_MODE::UNW_INIT_LOCAL_WITH_CONTEXT:
        std::cerr << "Stack trace (unw_init_local_with_context):" << std::endl;
        print_stacktrace_with_unw_init_local_with_context();
        break;
    case UWN_MODE::UNW_BACKTRACE:
        std::cerr << "Stack trace (unw_backtrace):" << std::endl;
        print_stacktrace_unw_backtrace();
        break;
    }
}

void signal_handler(int sig_num, siginfo_t* /*info*/, void* uctx) {
    g_last_ucontext = reinterpret_cast<ucontext_t*>(uctx);
    std::cerr << "Caught signal " << sig_num << ": " << strsignal(sig_num) << std::endl;

    const char* ctx_mode = std::getenv("CONTEXT_MODE");
    if (ctx_mode != nullptr && std::strcmp(ctx_mode, "bad") == 0) {
        // Run on a separate user stack (fiber) so that unw_backtrace cannot
        // cross back over the signal frame into the interrupted core stack.
        // This simulates stack switching (fibers/coroutines) where
        // unw_backtrace often fails to bridge the signal trampoline, while
        // unw_init_local_with_context succeeds.
        ucontext_t cur{};
        ucontext_t fiber{};
        getcontext(&fiber);
        fiber.uc_stack.ss_sp = g_bt_stack;
        fiber.uc_stack.ss_size = sizeof(g_bt_stack);
        fiber.uc_link = &cur;
        makecontext(&fiber, run_on_fiber, 0);
        swapcontext(&cur, &fiber);
    } else {
        run_on_fiber();
    }

    // Re-raise the signal to get a core dump
    signal(sig_num, SIG_DFL);
    raise(sig_num);
}

__attribute__((naked, noinline)) static void fault_site_naked() {
#if defined(__x86_64__)
    __asm__ __volatile__("xorl %ebp, %ebp; ud2");
#else
    __builtin_trap();
#endif
}

__attribute__((noinline)) void cause_segfault(int depth) {
    if (depth <= 0) {
        // Method 1: Directly execute an invalid instruction (naked function)
        // fault_site_naked();

        // Method 2: Dereference a null pointer
        volatile int* p = nullptr;
        *p = 1;
    } else {
        cause_segfault(depth - 1);
    }
}

void install_handler_legacy() {
    auto legacy_signal_handler = +[](int sig_num) { signal_handler(sig_num, nullptr, nullptr); };
    signal(SIGSEGV, legacy_signal_handler);
    signal(SIGABRT, legacy_signal_handler);
    signal(SIGILL, legacy_signal_handler);
    signal(SIGFPE, legacy_signal_handler);
}

void install_handlers_normal() {
    struct sigaction sa = {};
    sa.sa_sigaction = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

void install_handlers_on_altstack() {
    // sigaltstack provides an alternate stack for signal handlers so they can
    // run safely even if the normal thread stack is overflowed or corrupted.
    const size_t alt_sz = SIGSTKSZ;
    static std::unique_ptr<char[]> alt_mem(new char[alt_sz]);
    stack_t ss{};
    ss.ss_sp = alt_mem.get();
    ss.ss_size = alt_sz;
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa = {};
    sa.sa_sigaction = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

int main(int argc, char* argv[]) {
    const char* unw_mode_str = std::getenv("UNW_MODE");
    if (unw_mode_str == nullptr) {
        unw_mode_str = "";
    }
    if (std::strcmp(unw_mode_str, "unw_init_local_without_context") == 0) {
        unw_mode = UWN_MODE::UNW_INIT_LOCAL_WITHOUT_CONTEXT;
    } else if (std::strcmp(unw_mode_str, "unw_init_local_with_context") == 0) {
        unw_mode = UWN_MODE::UNW_INIT_LOCAL_WITH_CONTEXT;
    } else if (std::strcmp(unw_mode_str, "unw_backtrace") == 0) {
        unw_mode = UWN_MODE::UNW_BACKTRACE;
    } else {
        std::cerr << "Invalid UNW_MODE value: '" << unw_mode_str << "'" << std::endl;
        std::cerr << "Please set env 'UNW_MODE=unw_init_local_without_context', 'UNW_MODE=unw_init_local_with_context' "
                     "or 'UNW_MODE=unw_backtrace'"
                  << std::endl;
        return EXIT_FAILURE;
    }

    const char* handler_mode_str = std::getenv("HANDLER_MODE");
    if (handler_mode_str == nullptr) {
        handler_mode_str = "";
    }
    if (std::strcmp(handler_mode_str, "legacy") == 0) {
        install_handler_legacy();
    } else if (std::strcmp(handler_mode_str, "altstack") == 0) {
        install_handlers_on_altstack();
    } else if (std::strcmp(handler_mode_str, "normal") == 0) {
        install_handlers_normal();
    } else {
        std::cerr << "Invalid HANDLER_MODE value: '" << handler_mode_str << "'" << std::endl;
        std::cerr << "Please set env 'HANDLER_MODE=legacy', 'HANDLER_MODE=normal' or 'HANDLER_MODE=altstack'"
                  << std::endl;
        return EXIT_FAILURE;
    }

    cause_segfault(10);
    return 0;
}
