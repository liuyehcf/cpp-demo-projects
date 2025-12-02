#include <cxxabi.h>
#include <jni_utils.h>
#include <libunwind.h>
#include <signal.h>
#include <string.h>

#include <iostream>

// Function to print the current stack trace
void print_stack_trace() {
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
        } else {
            std::cout << " -- error: unable to obtain symbol name for this frame" << std::endl;
        }
    }
}

// Signal handler function
void signal_handler(int sig_num) {
    std::cerr << "Caught signal " << sig_num << ": " << strsignal(sig_num) << std::endl;
    print_stack_trace();

    // Re-raise the signal to get a core dump
    signal(sig_num, SIG_DFL);
    raise(sig_num);
}

// Function that will cause a segmentation fault
void cause_segfault(int depth) {
    if (depth <= 0) {
        int* null_ptr = nullptr;
        *null_ptr = 42;
    } else {
        cause_segfault(depth - 1);
    }
}

void do_something_via_jni() {
    JNIEnv* env = jni_utils::get_env();
    jclass jcls = jni_utils::find_class(env, "TriggerGC");
    jni_utils::Method method = jni_utils::get_mid(env, jcls, "trigger", "()V", true);
    jni_utils::invoke_static_method(env, jcls, &method);
}

int main() {
    // Register signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGFPE, signal_handler);

    try {
        do_something_via_jni();
    } catch (...) {
        // ignore
    }

    cause_segfault(10);

    return 0;
}
