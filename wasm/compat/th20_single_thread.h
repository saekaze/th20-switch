// Single-threaded <mutex>/<thread>/<condition_variable> for the TH20 wasm32
// build. The wasi libc++ is configured without threads; TH20's TH_SDL3 code
// runs its workers inline (runtime/worker.hpp) and only uses the standard
// lock types as uncontended guards, so no-op versions keep it exact.
// std::jthread keeps the 8-byte shape of a wasm32 libc++ jthread (thread
// handle + stop state) because TH20 embeds it in laid-out structs.
#pragma once
// Minimal libc++ internals only: <mutex> is reached from <ios>/<locale>, so
// pulling in all of <chrono> here would re-enter the stream headers.
#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__utility/forward.h>
#include <__mutex/once_flag.h>
#include <cstddef>
namespace std {
class mutex { public: constexpr mutex() noexcept = default; mutex(const mutex&)=delete; mutex& operator=(const mutex&)=delete; void lock(){} bool try_lock(){return true;} void unlock(){} };
class recursive_mutex { public: recursive_mutex() noexcept = default; recursive_mutex(const recursive_mutex&)=delete; recursive_mutex& operator=(const recursive_mutex&)=delete; void lock(){} bool try_lock() noexcept{return true;} void unlock(){} };
class timed_mutex : public mutex { public: template<class ThD> bool try_lock_for(const ThD&){return true;} template<class ThT> bool try_lock_until(const ThT&){return true;} };
class recursive_timed_mutex : public recursive_mutex { public: template<class ThD> bool try_lock_for(const ThD&){return true;} template<class ThT> bool try_lock_until(const ThT&){return true;} };
struct defer_lock_t { explicit defer_lock_t() = default; }; struct try_to_lock_t { explicit try_to_lock_t() = default; }; struct adopt_lock_t { explicit adopt_lock_t() = default; };
inline constexpr defer_lock_t defer_lock{}; inline constexpr try_to_lock_t try_to_lock{}; inline constexpr adopt_lock_t adopt_lock{};
template<class ThM> class lock_guard { ThM& m; public: using mutex_type=ThM; explicit lock_guard(ThM& x):m(x){m.lock();} lock_guard(ThM& x,adopt_lock_t):m(x){} ~lock_guard(){m.unlock();} lock_guard(const lock_guard&)=delete; lock_guard& operator=(const lock_guard&)=delete; };
template<class ThM> class unique_lock { ThM* m=nullptr; bool owns=false; public: using mutex_type=ThM;
  unique_lock() noexcept = default; explicit unique_lock(ThM& x):m(&x),owns(true){x.lock();} unique_lock(ThM& x,defer_lock_t) noexcept:m(&x){} unique_lock(ThM& x,try_to_lock_t):m(&x),owns(x.try_lock()){} unique_lock(ThM& x,adopt_lock_t):m(&x),owns(true){}
  unique_lock(unique_lock&& o) noexcept:m(o.m),owns(o.owns){o.m=nullptr;o.owns=false;} unique_lock& operator=(unique_lock&& o) noexcept{if(owns)m->unlock();m=o.m;owns=o.owns;o.m=nullptr;o.owns=false;return *this;}
  ~unique_lock(){if(owns)m->unlock();} void lock(){m->lock();owns=true;} bool try_lock(){return owns=m->try_lock();} void unlock(){m->unlock();owns=false;}
  ThM* release() noexcept{ThM* r=m;m=nullptr;owns=false;return r;} bool owns_lock() const noexcept{return owns;} explicit operator bool() const noexcept{return owns;} ThM* mutex() const noexcept{return m;} };
template<class... ThM> class scoped_lock { public: explicit scoped_lock(ThM&...){} scoped_lock(const scoped_lock&)=delete; };
// once_flag/call_once come from libc++'s own <mutex> (no-threads build).
enum class cv_status { no_timeout, timeout };
class condition_variable { public: void notify_one() noexcept{} void notify_all() noexcept{} template<class ThL> void wait(ThL&){} template<class ThL,class ThP> void wait(ThL&,ThP p){(void)p;} };
class condition_variable_any : public condition_variable {};
class thread { public: class id { unsigned v=0; public: id()=default; friend bool operator==(id a,id b){return a.v==b.v;} }; using native_handle_type=void*;
  thread() noexcept = default; thread(thread&& o) noexcept:handle(o.handle){o.handle=nullptr;} thread& operator=(thread&& o) noexcept{handle=o.handle;o.handle=nullptr;return *this;}
  template<class ThF,class... ThA> explicit thread(ThF&& f,ThA&&... a){std::forward<ThF>(f)(std::forward<ThA>(a)...);}
  bool joinable() const noexcept{return false;} void join(){} void detach(){} id get_id() const noexcept{return {};} static unsigned hardware_concurrency() noexcept{return 1;}
  private: void* handle=nullptr; };
class jthread { public: jthread() noexcept = default; jthread(jthread&&) noexcept = default; jthread& operator=(jthread&&) noexcept = default;
  template<class ThF,class... ThA> explicit jthread(ThF&& f,ThA&&... a){std::forward<ThF>(f)(std::forward<ThA>(a)...);}
  bool joinable() const noexcept{return false;} void join(){} void detach(){} bool request_stop() noexcept{return false;}
  private: thread t; void* stop=nullptr; };
namespace this_thread { inline void yield() noexcept{} inline thread::id get_id() noexcept{return {};} template<class ThR,class ThP> void sleep_for(const chrono::duration<ThR,ThP>&){} template<class ThC,class ThD> void sleep_until(const chrono::time_point<ThC,ThD>&){} }
}
