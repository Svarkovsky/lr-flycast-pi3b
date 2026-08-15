#include <new>
#include <exception>
// gcc12 emits calls to std::throw_bad_array_new_length as a separate
// symbol; gcc8 libstdc++ only provides __cxa_throw_bad_array_new_length.
namespace std {
  void __throw_bad_array_new_length() {
    throw std::bad_array_new_length();
  }
}
