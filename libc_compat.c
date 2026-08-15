/* Compat shim: glibc < 2.32 lacks __libc_single_threaded.
   gcc12 emits reads of it for C++11 once_flag/call_once.
   Value 0 == multi-threaded mode (safe default). */
int __libc_single_threaded = 0;
