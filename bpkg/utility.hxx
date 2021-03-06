// file      : bpkg/utility.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_UTILITY_HXX
#define BPKG_UTILITY_HXX

#include <memory>   // make_shared()
#include <string>   // to_string()
#include <cstring>  // strcmp(), strchr()
#include <utility>  // move(), forward(), declval(), make_pair()
#include <cassert>  // assert()
#include <iterator> // make_move_iterator()

#include <libbutl/ft/lang.hxx>

#include <libbutl/utility.mxx> // casecmp(), reverse_iterate(), etc

#include <libbutl/filesystem.mxx>

#include <bpkg/types.hxx>
#include <bpkg/version.hxx>
#include <bpkg/common-options.hxx>

namespace bpkg
{
  using std::move;
  using std::forward;
  using std::declval;

  using std::make_pair;
  using std::make_shared;
  using std::make_move_iterator;
  using std::to_string;

  using std::strcmp;
  using std::strchr;

  // <libbutl/utility.mxx>
  //
  using butl::casecmp;
  using butl::reverse_iterate;

  using butl::make_guard;
  using butl::make_exception_guard;

  using butl::getenv;
  using butl::setenv;
  using butl::unsetenv;

  // <libbutl/filesystem.mxx>
  //
  using butl::auto_rmfile;
  using butl::auto_rmdir;

  // Empty string and path.
  //
  extern const string empty_string;
  extern const path empty_path;
  extern const dir_path empty_dir_path;

  // Widely-used paths.
  //
  extern const dir_path bpkg_dir;    // .bpkg/
  extern const dir_path certs_dir;   // .bpkg/certs/
  extern const dir_path repos_dir;   // .bpkg/repos/
  extern const dir_path current_dir; // ./

  // Temporary directory facility.
  //
  // This is normally .bpkg/tmp/ but can also be some system-wide directory
  // (e.g., /tmp/bpkg-XXX/) if there is no bpkg configuration. This directory
  // is automatically created and cleaned up for most commands in main() so
  // you don't need to call init_tmp() explicitly except for certain special
  // commands (like cfg-create).
  //
  extern dir_path temp_dir;

  auto_rmfile
  tmp_file (const string& prefix);

  auto_rmdir
  tmp_dir (const string& prefix);

  void
  init_tmp (const dir_path& cfg);

  void
  clean_tmp (bool ignore_errors);

  // Progress.
  //
  extern bool stderr_term; // True if stderr is a terminal.

  // Y/N prompt. See butl::yn_prompt() for details (this is a thin wrapper).
  //
  // Issue diagnostics and throw failed if no answer could be extracted from
  // stdin (e.g., because it was closed).
  //
  bool
  yn_prompt (const string& prompt, char def = '\0');

  // Filesystem.
  //
  bool
  exists (const path&, bool ignore_error = false);

  bool
  exists (const dir_path&, bool ignore_error = false);

  bool
  empty (const dir_path&);

  void
  mk (const dir_path&);

  void
  mk_p (const dir_path&);

  void
  rm (const path&, uint16_t verbosity = 3);

  enum class rm_error_mode {ignore, warn, fail};

  void
  rm_r (const dir_path&,
        bool dir_itself = true,
        uint16_t verbosity = 3,
        rm_error_mode = rm_error_mode::fail);

  void
  mv (const dir_path& from, const dir_path& to);

  // File descriptor streams.
  //
  fdpipe
  open_pipe ();

  auto_fd
  open_dev_null ();

  // Directory extracted from argv[0] (i.e., this process' recall directory)
  // or empty if there is none. Can be used as a search fallback.
  //
  extern dir_path exec_dir;

  // Run build2, mapping verbosity levels. If quiet is true, then run build2
  // quiet if our verbosity level is 1.
  //
  const char*
  name_b (const common_options&);

  // Verbosity level 1 mapping.
  //
  enum class verb_b
  {
    quiet,    // Run quiet.
    progress, // Run quiet but (potentially) with progress.
    normal    // Run normally (at verbosity 1).
  };

  template <typename O, typename E, typename... A>
  process
  start_b (const common_options&, O&& out, E&& err, verb_b, A&&... args);

  template <typename... A>
  void
  run_b (const common_options&, verb_b, A&&... args);
}

#include <bpkg/utility.txx>

#endif // BPKG_UTILITY_HXX
