// file      : bpkg/database.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/database>

#include <odb/schema-catalog.hxx>
#include <odb/sqlite/exceptions.hxx>

#include <bpkg/types>
#include <bpkg/utility>
#include <bpkg/diagnostics>

using namespace std;

namespace bpkg
{
  using namespace odb::sqlite;
  using odb::schema_catalog;

  database
  open (const dir_path& d, tracer& tr, bool create)
  {
    tracer trace ("open");

    path f (d / path ("bpkg.sqlite3"));

    if (!create && !exists (f))
      fail << d << " does not look like a bpkg configuration directory";

    try
    {
      // We don't need the thread pool.
      //
      unique_ptr<connection_factory> cf (new single_connection_factory);

      database db (f.string (),
                   SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0),
                   true,                  // Enable FKs.
                   "",                    // Default VFS.
                   move (cf));

      db.tracer (trace);

      // Lock the database for as long as the connection is active. First
      // we set locking_mode to EXCLUSIVE which instructs SQLite not to
      // release any locks until the connection is closed. Then we force
      // SQLite to acquire the write lock by starting exclusive transaction.
      // See the locking_mode pragma documentation for details. This will
      // also fail if the database is inaccessible (e.g., file does not
      // exist, already used by another process, etc).
      //
      try
      {
        db.connection ()->execute ("PRAGMA locking_mode = EXCLUSIVE");
        transaction t (db.begin_exclusive ());

        if (create)
        {
          // Create the new schema.
          //
          if (db.schema_version () != 0)
            fail << f << ": already has database schema";

          schema_catalog::create_schema (db);
        }
        else
        {
          // Migrate the database if necessary.
          //
          schema_catalog::migrate (db);
        }

        t.commit ();
      }
      catch (odb::timeout&)
      {
        fail << "configuration " << d << " is already used by another process";
      }

      db.tracer (tr); // Switch to the caller's tracer.
      return db;
    }
    catch (const database_exception& e)
    {
      error << f << ": " << e.message ();
      throw failed ();
    }
  }
}