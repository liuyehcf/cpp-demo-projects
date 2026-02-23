#include <cxxabi.h>
#include <dlfcn.h>
#include <libunwind.h>

#include <array>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>

enum class UWN_MODE {
    UNW_INIT_LOCAL,
    UNW_INIT_LOCAL_2,
    UNW_BACKTRACE,
};
UWN_MODE unw_mode;

void print_symbol(const char* sym, const unw_word_t& pc, const unw_word_t& offset) {
    int status;
    // Attempt to demangle the symbol
    char* demangled_name = abi::__cxa_demangle(sym, nullptr, nullptr, &status);

    std::cout << "0x" << std::hex << pc << ": ";

    if (status == 0 && demangled_name) {
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
    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result += buffer.data();
    }

    pclose(pipe);
    return result;
}

void print_source_line(unw_word_t pc) {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
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

// Function to print the current stack trace
void print_stacktrace_with_unw_init_local() {
    unw_cursor_t cursor;
    unw_context_t context;

    // Initialize context to the current machine state.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    // Walk the stack up, one frame at a time.
    while (unw_step(&cursor) > 0) {
        unw_word_t offset, pc;
        char sym[256];

        if (unw_get_reg(&cursor, UNW_REG_IP, &pc)) {
            std::cout << "Error: cannot read program counter" << std::endl;
            break;
        }

        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
            print_frame(sym, pc, offset);
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

// Function to print stack trace using unw_init_local2
void print_stacktrace_with_unw_init_local2() {
    unw_cursor_t cursor;
    unw_context_t context;

    // Initialize context to the current machine state using unw_init_local2.
    unw_getcontext(&context);
    // No special flags; use 0 for default behavior.
    unw_init_local2(&cursor, &context, 0);

    while (unw_step(&cursor) > 0) {
        unw_word_t offset, pc;
        char sym[256];

        if (unw_get_reg(&cursor, UNW_REG_IP, &pc)) {
            std::cout << "Error: cannot read program counter" << std::endl;
            break;
        }

        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
            print_symbol(sym, pc, offset);
            print_source_line(pc);
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

// Function to print stack trace using unw_backtrace
void print_stacktrace_unw_backtrace() {
    constexpr int kMaxFrames = 256;
    void* buffer[kMaxFrames];

    int nframes = unw_backtrace(buffer, kMaxFrames);
    if (nframes <= 0) {
        std::cout << "Error: unw_backtrace failed" << std::endl;
        return;
    }

    for (int i = 0; i < nframes; ++i) {
        unw_word_t pc = reinterpret_cast<unw_word_t>(buffer[i]);

        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_sname) {
            const char* sym = info.dli_sname;
            unw_word_t offset = pc - reinterpret_cast<unw_word_t>(info.dli_saddr);
            print_frame(sym, pc, offset);
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

// Signal handler function
void signal_handler(int sig_num) {
    std::cerr << "Caught signal " << sig_num << ": " << strsignal(sig_num) << std::endl;
    switch (unw_mode) {
    case UWN_MODE::UNW_INIT_LOCAL:
        std::cerr << "Stack trace (unw_init_local):" << std::endl;
        print_stacktrace_with_unw_init_local();
        break;
    case UWN_MODE::UNW_INIT_LOCAL_2:
        std::cerr << "Stack trace (unw_init_local2):" << std::endl;
        print_stacktrace_with_unw_init_local2();
        break;
    case UWN_MODE::UNW_BACKTRACE:
        std::cerr << "Stack trace (unw_backtrace):" << std::endl;
        print_stacktrace_unw_backtrace();
        break;
    }

    // Re-raise the signal to get a core dump
    signal(sig_num, SIG_DFL);
    raise(sig_num);
}

struct Item {};

class ItemHolder {
public:
    virtual Item* get_item() { return item.get(); }

private:
    std::shared_ptr<Item> item;
};

// Function that will cause a segmentation fault
void cause_segfault(std::shared_ptr<ItemHolder> holder, int depth) {
    if (depth <= 0) {
        holder->get_item();
    } else {
        cause_segfault(holder, depth - 1);
    }
}

int main(int argc, char* argv[]) {
    const char* unw_mode_str = std::getenv("UNW_MODE");
    if (!unw_mode_str) unw_mode_str = "";

    if (std::strcmp(unw_mode_str, "unw_init_local") == 0)
        unw_mode = UWN_MODE::UNW_INIT_LOCAL;
    else if (std::strcmp(unw_mode_str, "unw_init_local2") == 0)
        unw_mode = UWN_MODE::UNW_INIT_LOCAL_2;
    else if (std::strcmp(unw_mode_str, "unw_backtrace") == 0)
        unw_mode = UWN_MODE::UNW_BACKTRACE;
    else {
        std::cerr << "Invalid UNW_MODE value: '" << unw_mode_str << "'" << std::endl;
        std::cerr << "Please set env 'UNW_MODE=unw_init_local', 'UNW_MODE=unw_init_local2' or 'UNW_MODE=unw_backtrace'"
                  << std::endl;
        return EXIT_FAILURE;
    }

    // Register signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGFPE, signal_handler);

    cause_segfault({}, 10);

    return 0;
}
