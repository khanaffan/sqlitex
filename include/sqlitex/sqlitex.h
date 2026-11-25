#pragma once

#include "sqlitex_export.h"

// Core
#include "sqlitex/error.h"
#include "sqlitex/sqlite.h"
#include "sqlitex/value.h"
#include "sqlitex/database.h"
#include "sqlitex/statement.h"
#include "sqlitex/backup.h"
#include "sqlitex/snapshot.h"

// Session + changeset
#ifdef SQLITEX_HAS_SESSION
#include "sqlitex/session.h"
#include "sqlitex/changeset.h"
#include "sqlitex/changeset_iterator.h"
#include "sqlitex/changegroup.h"
#include "sqlitex/rebaser.h"
#endif

// Blob I/O
#include "sqlitex/blobio.h"

// Scoped authorizer policies
#include "sqlitex/policy.h"

// Custom SQL functions, virtual tables, FTS5, and R-Tree
#include "sqlitex/function.h"
#include "sqlitex/virtual_table.h"
#include "sqlitex/fts5.h"
#include "sqlitex/rtree.h"

// CloudSQLite / bcVFS (full functionality when SQLITEX_HAS_BCVFS is defined)
#include "sqlitex/bcvfs.h"
#include "sqlitex/prefetch.h"
#include "sqlitex/bcv_util.h"
#include "sqlitex/bcv_module.h"
