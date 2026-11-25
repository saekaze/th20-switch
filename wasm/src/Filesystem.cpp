// The two std::filesystem operations TH20 uses (creating the save folders and
// checking for existing snapshots), on the C library: the wasi libc++ archive
// has no filesystem implementation.
#include <filesystem>
#include <cerrno>
#include <sys/stat.h>
_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM
file_status __status(const path& p, error_code* ec) {
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) {
        if (ec) *ec = error_code(errno, generic_category());
        return file_status(errno == ENOENT ? file_type::not_found : file_type::none);
    }
    if (ec) ec->clear();
    return file_status(S_ISDIR(st.st_mode) ? file_type::directory : S_ISREG(st.st_mode) ? file_type::regular : file_type::unknown,
                       static_cast<perms>(st.st_mode & 07777));
}
bool __create_directory(const path& p, error_code* ec) {
    if (::mkdir(p.c_str(), 0777) == 0) { if (ec) ec->clear(); return true; }
    const int e = errno;
    struct stat st {};
    if (e == EEXIST && ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { if (ec) ec->clear(); return false; }
    if (ec) *ec = error_code(e, generic_category());
    return false;
}
_LIBCPP_END_NAMESPACE_FILESYSTEM
