// The wasi libc++ archive is built without file streams; the sysroot made by
// scripts/make_wasi_sysroot.sh re-enables <fstream>, and the stream classes
// TH20 uses are instantiated here.
#include <fstream>
template class std::basic_filebuf<char>;
template class std::basic_ifstream<char>;
template class std::basic_ofstream<char>;
template class std::basic_fstream<char>;
