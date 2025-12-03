#include <cxxabi.h>
#include <dlfcn.h>
#include <libunwind.h>

#include <array>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>

int method = 1;

void print_symbol(const char* sym, const unw_word_t& pc, const unw_word_t& offset) {
    int status;
    // Attempt to demangle the symbol
    char* demangled_name = abi::__cxa_demangle(sym, nullptr, nullptr, &status);

    std::cout << "0x" << std::hex << pc << ": ";

    if (status == 0 && demangled_name) {
        std::cout << demangled_name << " (+0x" << std::hex << offset << ")" << std::endl;
        free(demangled_name); // Free the demangled name
    } else {
        // If demangling failed, print the mangled name
        std::cout << sym << " (+0x" << std::hex << offset << ")" << std::endl;
    }
}

// Function to print the current stack trace
void print_stacktrace_1() {
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
            print_symbol(sym, pc, offset);
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

void print_stacktrace_2() {
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
            print_symbol(sym, pc, offset);
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

// Signal handler function
void signal_handler(int sig_num) {
    std::cerr << "Caught signal " << sig_num << ": " << strsignal(sig_num) << std::endl;
    if (method == 1) {
        std::cerr << "Stack trace (method 1):" << std::endl;
        print_stacktrace_1();
    } else {
        std::cerr << "Stack trace (method 2):" << std::endl;
        print_stacktrace_2();
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
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <method>\n"
                  << "method: 1 - Use print_stacktrace_1\n"
                  << "        2 - print_stacktrace_2\n";
        return 1;
    }
    method = std::stoi(argv[1]);
    // Register signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGFPE, signal_handler);

    cause_segfault({}, 10);

    return 0;
}
