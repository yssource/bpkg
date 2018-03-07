// file      : bpkg/rep-add.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/rep-add.hxx>

#include <bpkg/package.hxx>
#include <bpkg/package-odb.hxx>
#include <bpkg/database.hxx>
#include <bpkg/diagnostics.hxx>
#include <bpkg/manifest-utility.hxx>

using namespace std;
using namespace butl;

namespace bpkg
{
  shared_ptr<repository>
  rep_add (transaction& t, const repository_location& rl)
  {
    const string& rn (rl.canonical_name ());

    database& db (t.database ());
    shared_ptr<repository> r (db.find<repository> (rn));

    bool updated (false);

    if (r == nullptr)
    {
      r.reset (new repository (rl));
      db.persist (r);
    }
    else if (r->location.url () != rl.url ())
    {
      r->location = rl;
      db.update (r);

      updated = true;
    }

    shared_ptr<repository> root (db.load<repository> (""));

    bool added (
      root->complements.insert (lazy_shared_ptr<repository> (db, r)).second);

    if (added)
      db.update (root);

    if (verb)
      text << (added ? "added " : updated ? "updated " : "unchanged ") << rn;

    return r;
  }

  int
  rep_add (const rep_add_options& o, cli::scanner& args)
  {
    tracer trace ("rep_add");

    dir_path c (o.directory ());
    l4 ([&]{trace << "configuration: " << c;});

    if (!args.more ())
      fail << "repository location argument expected" <<
        info << "run 'bpkg help rep-add' for more information";

    database db (open (c, trace));
    transaction t (db.begin ());
    session s; // Repository dependencies can have cycles.

    while (args.more ())
    {
      repository_location rl (
        parse_location (args.next (),
                        o.type_specified ()
                        ? optional<repository_type> (o.type ())
                        : nullopt));

      rep_add (t, rl);
    }

    t.commit ();

    return 0;
  }
}
