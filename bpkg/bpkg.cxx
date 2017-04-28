// file      : bpkg/bpkg.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef _WIN32
#  include <signal.h> // signal()
#else
#  include <stdlib.h> // getenv(), _putenv()
#endif

#include <cstring>   // strcmp()
#include <iostream>

#include <bpkg/types>
#include <bpkg/utility>

#include <bpkg/diagnostics>
#include <bpkg/bpkg-options>

// Commands.
//
#include <bpkg/help>

#include <bpkg/cfg-create>

#include <bpkg/pkg-build>
#include <bpkg/pkg-clean>
#include <bpkg/pkg-configure>
#include <bpkg/pkg-disfigure>
#include <bpkg/pkg-drop>
#include <bpkg/pkg-fetch>
#include <bpkg/pkg-install>
#include <bpkg/pkg-purge>
#include <bpkg/pkg-status>
#include <bpkg/pkg-test>
#include <bpkg/pkg-uninstall>
#include <bpkg/pkg-unpack>
#include <bpkg/pkg-update>
#include <bpkg/pkg-verify>

#include <bpkg/rep-add>
#include <bpkg/rep-create>
#include <bpkg/rep-fetch>
#include <bpkg/rep-info>

using namespace std;
using namespace bpkg;

// Initialize the command option class O with the common options and then
// parse the rest of the command line placing non-option arguments to args.
// Once this is done, use the "final" values of the common options to do
// global initializations (verbosity level, etc).
//
template <typename O>
static O
parse (const common_options& co, cli::scanner& scan, strings& args)
{
  O o;
  static_cast<common_options&> (o) = co;

  // We want to be able to specify options and arguments in any order (it is
  // really handy to just add -v at the end of the command line).
  //
  for (bool opt (true); scan.more (); )
  {
    if (opt)
    {
      // If we see first "--", then we are done parsing options.
      //
      if (strcmp (scan.peek (), "--") == 0)
      {
        scan.next ();
        opt = false;
        continue;
      }

      // Parse the next chunk of options until we reach an argument (or eos).
      //
      o.parse (scan);

      if (!scan.more ())
        break;

      // Fall through.
    }

    args.push_back (scan.next ());
  }

  // Global initializations.
  //

  // Diagnostics verbosity.
  //
  verb = o.verbose_specified ()
    ? o.verbose ()
    : o.V () ? 3 : o.v () ? 2 : o.quiet () ? 0 : 1;

  return o;
}

int
main (int argc, char* argv[])
try
{
  using namespace cli;

  exec_dir = path (argv[0]).directory ();

  // This is a little hack to make out baseutils for Windows work when called
  // with absolute path. In a nutshell, MSYS2's exec*p() doesn't search in the
  // parent's executable directory, only in PATH. And since we are running
  // without a shell (that would read /etc/profile which sets PATH to some
  // sensible values), we are only getting Win32 PATH values. And MSYS2 /bin
  // is not one of them. So what we are going to do is add /bin at the end of
  // PATH (which will be passed as is by the MSYS2 machinery). This will make
  // MSYS2 search in /bin (where our baseutils live). And for everyone else
  // this should be harmless since it is not a valid Win32 path.
  //
#ifdef _WIN32
  {
    string mp ("PATH=");
    if (const char* p = getenv ("PATH"))
    {
      mp += p;
      mp += ';';
    }
    mp += "/bin";

    _putenv (mp.c_str ());
  }
#endif

  // On POSIX ignore SIGPIPE which is signaled to a pipe-writing process if
  // the pipe reading end is closed. Note that by default this signal
  // terminates a process. Also note that there is no way to disable this
  // behavior on a file descriptor basis or for the write() function call.
  //
#ifndef _WIN32
  if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
    fail << "unable to ignore broken pipe (SIGPIPE) signal: "
         << system_error (errno, generic_category ()); // Sanitize.
#endif

  argv_file_scanner scan (argc, argv, "--options-file");

  // First parse common options and --version/--help.
  //
  options o;
  o.parse (scan, unknown_mode::stop);

  if (o.version ())
  {
    cout << "bpkg " << BPKG_VERSION_STR << endl
         << "libbpkg " << LIBBPKG_VERSION_STR << endl
         << "libbutl " << LIBBUTL_VERSION_STR << endl
         << "Copyright (c) 2014-2017 Code Synthesis Ltd" << endl
         << "This is free software released under the MIT license." << endl;
    return 0;
  }

  strings argsv; // To be filled by parse() above.
  vector_scanner args (argsv);

  const common_options& co (o);

  if (o.help ())
    return help (parse<help_options> (co, scan, argsv), "", nullptr);

  // The next argument should be a command.
  //
  if (!scan.more ())
    fail << "bpkg command expected" <<
      info << "run 'bpkg help' for more information";

  int cmd_argc (2);
  char* cmd_argv[] {argv[0], const_cast<char*> (scan.next ())};
  commands cmd;
  cmd.parse (cmd_argc, cmd_argv, true, unknown_mode::stop);

  if (cmd_argc != 1)
    fail << "unknown bpkg command/option '" << cmd_argv[1] << "'" <<
      info << "run 'bpkg help' for more information";

  // If the command is 'help', then what's coming next is another
  // command. Parse it into cmd so that we only need to check for
  // each command in one place.
  //
  bool h (cmd.help ());
  help_options ho;

  if (h)
  {
    ho = parse<help_options> (co, scan, argsv);

    if (args.more ())
    {
      cmd_argc = 2;
      cmd_argv[1] = const_cast<char*> (args.next ());

      // First see if this is a command.
      //
      cmd = commands (); // Clear the help option.
      cmd.parse (cmd_argc, cmd_argv, true, unknown_mode::stop);

      // If not, then it got to be a help topic.
      //
      if (cmd_argc != 1)
        return help (ho, cmd_argv[1], nullptr);
    }
    else
      return help (ho, "", nullptr);
  }

  // Handle commands.
  //
  int r (1);
  for (;;)
  {
    // help
    //
    if (cmd.help ())
    {
      assert (h);
      r = help (ho, "help", print_bpkg_help_usage);
      break;
    }

    // Commands.
    //
    // if (cmd.pkg_verify ())
    // {
    //  if (h)
    //    r = help (ho, "pkg-verify", print_bpkg_pkg_verify_usage);
    //  else
    //    r = pkg_verify (parse<pkg_verify_options> (co, scan, argsv), args);
    //
    //  break;
    // }
    //
#define COMMAND_IMPL(NP, SP, CMD)                                       \
    if (cmd.NP##CMD ())                                                 \
    {                                                                   \
      if (h)                                                            \
        r = help (ho, SP#CMD, print_bpkg_##NP##CMD##_usage);            \
      else                                                              \
        r = NP##CMD (parse<NP##CMD##_options> (co, scan, argsv), args); \
                                                                        \
      break;                                                            \
    }

    // cfg-* commands
    //
#define CFG_COMMAND(CMD) COMMAND_IMPL(cfg_, "cfg-", CMD)

    CFG_COMMAND (create);

    // pkg-* commands
    //
#define PKG_COMMAND(CMD) COMMAND_IMPL(pkg_, "pkg-", CMD)

    PKG_COMMAND (build);
    PKG_COMMAND (clean);
    PKG_COMMAND (configure);
    PKG_COMMAND (disfigure);
    PKG_COMMAND (drop);
    PKG_COMMAND (fetch);
    PKG_COMMAND (install);
    PKG_COMMAND (purge);
    PKG_COMMAND (status);
    PKG_COMMAND (test);
    PKG_COMMAND (uninstall);
    PKG_COMMAND (unpack);
    PKG_COMMAND (update);
    PKG_COMMAND (verify);

    // rep-* commands
    //
#define REP_COMMAND(CMD) COMMAND_IMPL(rep_, "rep-", CMD)

    REP_COMMAND (add);
    REP_COMMAND (create);
    REP_COMMAND (fetch);
    REP_COMMAND (info);

    assert (false);
    fail << "unhandled command";
  }

  if (r != 0)
    return r;

  // Warn if args contain some leftover junk. We already successfully
  // performed the command so failing would probably be misleading.
  //
  if (args.more ())
  {
    diag_record dr;
    dr << warn << "ignoring unexpected argument(s)";
    while (args.more ())
      dr << " '" << args.next () << "'";
  }

  return 0;
}
catch (const failed&)
{
  return 1; // Diagnostics has already been issued.
}
catch (const cli::exception& e)
{
  error << e;
  return 1;
}
/*
catch (const std::exception& e)
{
  error << e;
  return 1;
}
*/
