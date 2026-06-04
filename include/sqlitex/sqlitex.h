#pragma once

#include "sqlitex_export.h"

// Core
#include "sqlitex/error.h"
#include "sqlitex/value.h"
#include "sqlitex/database.h"
#include "sqlitex/statement.h"

// Session + changeset
#ifdef SQLITEX_HAS_SESSION
#include "sqlitex/session.h"
#include "sqlitex/changeset.h"
#include "sqlitex/changeset_iterator.h"
#include "sqlitex/changegroup.h"
#endif

// Blob I/O
#include "sqlitex/blobio.h"

// Custom SQL functions and virtual tables
#include "sqlitex/function.h"
#include "sqlitex/virtual_table.h"

// bcvfs (full functionality when SQLITEX_HAS_BCVFS is defined)
#include "sqlitex/bcvfs.h"
#include "sqlitex/prefetch.h"
