/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Internal interface definitions for sql.
 *
 */
#ifndef sqlINT_H
#define sqlINT_H

#define IdChar(C)  ((sqlCtypeMap[(unsigned char)C]&0x46)!=0)

/* Special Comments:
 *
 * Some comments have special meaning to the tools that measure test
 * coverage:
 *
 *    NO_TEST                     - The branches on this line are not
 *                                  measured by branch coverage.  This is
 *                                  used on lines of code that actually
 *                                  implement parts of coverage testing.
 *
 *    OPTIMIZATION-IF-TRUE        - This branch is allowed to alway be false
 *                                  and the correct answer is still obtained,
 *                                  though perhaps more slowly.
 *
 *    OPTIMIZATION-IF-FALSE       - This branch is allowed to alway be true
 *                                  and the correct answer is still obtained,
 *                                  though perhaps more slowly.
 *
 *    PREVENTS-HARMLESS-OVERREAD  - This branch prevents a buffer overread
 *                                  that would be harmless and undetectable
 *                                  if it did occur.
 *
 * In all cases, the special comment must be enclosed in the usual
 * slash-asterisk...asterisk-slash comment marks, with no spaces between the
 * asterisks and the comment text.
 */

#include <stdbool.h>

#include "box/field_def.h"
#include "box/sql.h"
#include "box/txn.h"
#include "trivia/util.h"

/*
 * These #defines should enable >2GB file support on POSIX if the
 * underlying operating system supports it.  If the OS lacks
 * large file support, these should be no-ops.
 *
 * Ticket #2739:  The _LARGEFILE_SOURCE macro must appear before any
 * system #includes.  Hence, this block of code must be the very first
 * code in all source files.
 *
 * Large file support can be disabled using the -Dsql_DISABLE_LFS switch
 * on the compiler command line.  This is necessary if you are compiling
 * on a recent machine (ex: Red Hat 7.2) but you want your code to work
 * on an older machine (ex: Red Hat 6.0).  If you compile on Red Hat 7.2
 * without this option, LFS is enable.  But LFS does not exist in the kernel
 * in Red Hat 6.0, so the code won't work.  Hence, for maximum binary
 * portability you should omit LFS.
 *
 * The previous paragraph was written in 2005.  (This paragraph is written
 * on 2008-11-28.) These days, all Linux kernels support large files, so
 * you should probably leave LFS enabled.  But some embedded platforms might
 * lack LFS in which case the sql_DISABLE_LFS macro might still be useful.
 *
 * Similar is true for Mac OS X.  LFS is only supported on Mac OS X 9 and later.
 */
#ifndef SQL_DISABLE_LFS
#define _LARGE_FILE       1
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#define _LARGEFILE_SOURCE 1
#endif

/* What version of GCC is being used.  0 means GCC is not being used */
#ifdef __GNUC__
#define GCC_VERSION (__GNUC__*1000000+__GNUC_MINOR__*1000+__GNUC_PATCHLEVEL__)
#else
#define GCC_VERSION 0
#endif

/* Needed for various definitions... */
#if defined(__GNUC__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "sqlLimit.h"

/*
 * Include standard header files as necessary
 */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/*
 * The following macros are used to cast pointers to integers and
 * integers to pointers.  The way you do this varies from one compiler
 * to the next, so we have developed the following set of #if statements
 * to generate appropriate macros for a wide range of compilers.
 *
 * The correct "ANSI" way to do this is to use the intptr_t type.
 * Unfortunately, that typedef is not available on all compilers, or
 * if it is available, it requires an #include of specific headers
 * that vary from one machine to the next.
 *
 * Ticket #3860:  The llvm-gcc-4.2 compiler from Apple chokes on
 * the ((void*)&((char*)0)[X]) construct.
 */
#if defined(__PTRDIFF_TYPE__)	/* This case should work for GCC */
#define SQL_INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
#define SQL_PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)	/* Works for compilers other than LLVM */
#define SQL_INT_TO_PTR(X)  ((void*)&((char*)0)[X])
#define SQL_PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#elif defined(HAVE_STDINT_H)	/* Use this case if we have ANSI headers */
#define SQL_INT_TO_PTR(X)  ((void*)(intptr_t)(X))
#define SQL_PTR_TO_INT(X)  ((int)(intptr_t)(X))
#else				/* Generates a warning - but it always works */
#define SQL_INT_TO_PTR(X)  ((void*)(X))
#define SQL_PTR_TO_INT(X)  ((int)(X))
#endif

/*
 * A macro to hint to the compiler that a function should not be
 * inlined.
 */
#if defined(__GNUC__)
#define SQL_NOINLINE  __attribute__((noinline))
#else
#define SQL_NOINLINE
#endif

/*
 * Powersafe overwrite is on by default.  But can be turned off using
 * the -Dsql_POWERSAFE_OVERWRITE=0 command-line option.
 */
#ifndef SQL_POWERSAFE_OVERWRITE
#define SQL_POWERSAFE_OVERWRITE 1
#endif

/*
 * EVIDENCE-OF: R-25715-37072 Memory allocation statistics are enabled by
 * default unless sql is compiled with sql_DEFAULT_MEMSTATUS=0 in
 * which case memory allocation statistics are disabled by default.
 */
#if !defined(SQL_DEFAULT_MEMSTATUS)
#define SQL_DEFAULT_MEMSTATUS 1
#endif

#if defined(SQL_SYSTEM_MALLOC) \
  + defined(SQL_ZERO_MALLOC) \
  + defined(SQL_MEMDEBUG)>1
#error "Two or more of the following compile-time configuration options\
 are defined but at most one is allowed:\
 SQL_SYSTEM_MALLOC, SQL_MEMDEBUG,\
 SQL_ZERO_MALLOC"
#endif
#if defined(SQL_SYSTEM_MALLOC) \
  + defined(SQL_ZERO_MALLOC) \
  + defined(SQL_MEMDEBUG)==0
#define SQL_SYSTEM_MALLOC 1
#endif

/*
 * If sql_MALLOC_SOFT_LIMIT is not zero, then try to keep the
 * sizes of memory allocations below this value where possible.
 */
#if !defined(SQL_MALLOC_SOFT_LIMIT)
#define SQL_MALLOC_SOFT_LIMIT 1024
#endif

/*
 * Enable sql_ENABLE_EXPLAIN_COMMENTS if sql_DEBUG is turned on.
 */
#if !defined(SQL_ENABLE_EXPLAIN_COMMENTS) && defined(SQL_DEBUG)
#define SQL_ENABLE_EXPLAIN_COMMENTS 1
#endif

/*
 * The testcase() macro is used to aid in coverage testing.  When
 * doing coverage testing, the condition inside the argument to
 * testcase() must be evaluated both true and false in order to
 * get full branch coverage.  The testcase() macro is inserted
 * to help ensure adequate test coverage in places where simple
 * condition/decision coverage is inadequate.  For example, testcase()
 * can be used to make sure boundary values are tested.  For
 * bitmask tests, testcase() can be used to make sure each bit
 * is significant and used at least once.  On switch statements
 * where multiple cases go to the same block of code, testcase()
 * can insure that all cases are evaluated.
 *
 */
#ifdef SQL_COVERAGE_TEST
void sqlCoverage(int);
#define testcase(X)  if( X ){ sqlCoverage(__LINE__); }
#else
#define testcase(X)
#endif

/*
 * The TESTONLY macro is used to enclose variable declarations or
 * other bits of code that are needed to support the arguments
 * within testcase() and assert() macros.
 */
#if !defined(NDEBUG) || defined(SQL_COVERAGE_TEST)
#define TESTONLY(X)  X
#else
#define TESTONLY(X)
#endif

/*
 * Sometimes we need a small amount of code such as a variable initialization
 * to setup for a later assert() statement.  We do not want this code to
 * appear when assert() is disabled.  The following macro is therefore
 * used to contain that setup code.  The "VVA" acronym stands for
 * "Verification, Validation, and Accreditation".  In other words, the
 * code within VVA_ONLY() will only run during verification processes.
 */
#ifndef NDEBUG
#define VVA_ONLY(X)  X
#else
#define VVA_ONLY(X)
#endif

/*
 * The ALWAYS and NEVER macros surround boolean expressions which
 * are intended to always be true or false, respectively.  Such
 * expressions could be omitted from the code completely.  But they
 * are included in a few cases in order to enhance the resilience
 * of sql to unexpected behavior - to make the code "self-healing"
 * or "ductile" rather than being "brittle" and crashing at the first
 * hint of unplanned behavior.
 *
 * In other words, ALWAYS and NEVER are added for defensive code.
 *
 * When doing coverage testing ALWAYS and NEVER are hard-coded to
 * be true and false so that the unreachable code they specify will
 * not be counted as untested code.
 */
#if defined(SQL_COVERAGE_TEST) || defined(SQL_MUTATION_TEST)
#define ALWAYS(X)      (1)
#define NEVER(X)       (0)
#elif !defined(NDEBUG)
#define ALWAYS(X)      ((X)?1:(assert(0),0))
#define NEVER(X)       ((X)?(assert(0),1):0)
#else
#define ALWAYS(X)      (X)
#define NEVER(X)       (X)
#endif

/*
 * Return true (non-zero) if the input is an integer that is too large
 * to fit in 32-bits.  This macro is used inside of various testcase()
 * macros to verify that we have tested sql for large-file support.
 */
#define IS_BIG_INT(X)  (((X)&~(i64)0xffffffff)!=0)

#define SQL_LIKE_DOESNT_MATCH_BLOBS

#include "hash.h"
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

typedef long long int sql_int64;
typedef unsigned long long int sql_uint64;
typedef sql_int64 sql_int64;
typedef sql_uint64 sql_uint64;
typedef struct sql_stmt sql_stmt;

typedef struct sql_context sql_context;
typedef struct sql sql;
typedef struct Mem sql_value;
typedef struct sql_file sql_file;
struct sql_file {
	const struct sql_io_methods *pMethods;	/* Methods for an open file */
};

typedef int (*sql_callback) (void *, int, char **, char **);

typedef struct sql_vfs sql_vfs;
struct sql_vfs {
	int iVersion;	/* Structure version number (currently 3) */
	int szOsFile;	/* Size of subclassed sql_file */
	int mxPathname;	/* Maximum file pathname length */
	sql_vfs *pNext;	/* Next registered VFS */
	const char *zName;	/* Name of this virtual file system */
	void *pAppData;	/* Pointer to application-specific data */
	int (*xOpen) (sql_vfs *, const char *zName, sql_file *,
		      int flags, int *pOutFlags);
	int (*xDelete) (sql_vfs *, const char *zName, int syncDir);
	int (*xRandomness) (sql_vfs *, int nByte, char *zOut);
	int (*xCurrentTime) (sql_vfs *, double *);
	int (*xGetLastError) (sql_vfs *, int, char *);
	/*
	** The methods above are in version 1 of the sql_vfs object
	** definition.  Those that follow are added in version 2 or later
	*/
	int (*xCurrentTimeInt64) (sql_vfs *, sql_int64 *);
	/*
	** The methods above are in versions 1 through 3 of the sql_vfs object.
	** New fields may be appended in future versions.  The iVersion
	** value will increment whenever this happens.
	*/
};

#define SQL_LIMIT_LENGTH                    0
#define SQL_LIMIT_SQL_LENGTH                1
#define SQL_LIMIT_COLUMN                    2
#define SQL_LIMIT_EXPR_DEPTH                3
#define SQL_LIMIT_COMPOUND_SELECT           4
#define SQL_LIMIT_VDBE_OP                   5
#define SQL_LIMIT_FUNCTION_ARG              6
#define SQL_LIMIT_ATTACHED                  7
#define SQL_LIMIT_LIKE_PATTERN_LENGTH       8
#define SQL_LIMIT_TRIGGER_DEPTH             9
#define SQL_LIMIT_WORKER_THREADS           10

enum sql_ret_code {
	/** Result of a routine is ok. */
	SQL_OK = 0,
	/** Common error code. */
	SQL_ERROR,
	/** Access permission denied. */
	SQL_PERM,
	/** Callback routine requested an abort. */
	SQL_ABORT,
	/** The database file is locked. */
	SQL_BUSY,
	/** A table in the database is locked. */
	SQL_LOCKED,
	/** A malloc() failed. */
	SQL_NOMEM,
	/** Operation terminated by sql_interrupt(). */
	SQL_INTERRUPT,
	/** Some kind of disk I/O error occurred. */
	SQL_IOERR,
	/** Unknown opcode in sql_file_control(). */
	SQL_NOTFOUND,
	/** Insertion failed because database is full. */
	SQL_FULL,
	/** Unable to open the database file. */
	SQL_CANTOPEN,
	/** The database schema changed. */
	SQL_SCHEMA,
	/** String or BLOB exceeds size limit. */
	SQL_TOOBIG,
	/** Abort due to constraint violation. */
	SQL_CONSTRAINT,
	/** Data type mismatch. */
	SQL_MISMATCH,
	/** Library used incorrectly. */
	SQL_MISUSE,
	/** 2nd parameter to sql_bind out of range. */
	SQL_RANGE,
	SQL_TARANTOOL_ITERATOR_FAIL,
	SQL_TARANTOOL_INSERT_FAIL,
	SQL_TARANTOOL_DELETE_FAIL,
	SQL_TARANTOOL_ERROR,
	/** Warnings from sql_log(). */
	SQL_WARNING,
	/** sql_step() has another row ready. */
	SQL_ROW,
	/** sql_step() has finished executing. */
	SQL_DONE,
};

void *
sql_malloc(int);

void *
sql_malloc64(sql_uint64);

void *
sql_realloc(void *, int);

void *
sql_realloc64(void *, sql_uint64);

void
sql_free(void *);

sql_uint64
sql_msize(void *);

int
sql_stricmp(const char *, const char *);

int
sql_strnicmp(const char *, const char *, int);

 const void *
sql_value_blob(sql_value *);

int
sql_value_bytes(sql_value *);

double
sql_value_double(sql_value *);

int
sql_value_int(sql_value *);

/**
 * Get row column subtype.
 * @param stmt row data to process.
 * @param i column index.
 * @retval SQL subtype if any, 0 else.
 */
enum sql_subtype
sql_column_subtype(struct sql_stmt *stmt, int i);

sql_int64
sql_value_int64(sql_value *);

sql_uint64
sql_value_uint64(sql_value *);

const unsigned char *
sql_value_text(sql_value *);

int
sql_value_type(sql_value *);

int
sql_value_numeric_type(sql_value *);

sql *
sql_context_db_handle(sql_context *);


void
sql_result_blob(sql_context *, const void *,
		    int, void (*)(void *));

void
sql_result_blob64(sql_context *, const void *,
		      sql_uint64, void (*)(void *));

void
sql_result_double(sql_context *, double);

void
sql_result_error(sql_context *, const char *,
		     int);

void
sql_result_error_toobig(sql_context *);

void
sql_result_error_nomem(sql_context *);

void
sql_result_error_code(sql_context *, int);

void
sql_result_int(sql_context *, int);

void
sql_result_int64(sql_context *, sql_int64);

void
sql_result_uint64(sql_context *, sql_uint64);

void
sql_result_null(sql_context *);

void
sql_result_text(sql_context *, const char *,
		    int, void (*)(void *));

void
sql_result_text64(sql_context *, const char *,
		      sql_uint64, void (*)(void *));

void
sql_result_value(sql_context *,
		     sql_value *);

void
sql_result_zeroblob(sql_context *, int n);

int
sql_result_zeroblob64(sql_context *,
			  sql_uint64 n);

char *
sql_mprintf(const char *, ...);
char *
sql_vmprintf(const char *, va_list);
char *
sql_snprintf(int, char *, const char *, ...);
char *
sql_vsnprintf(int, char *, const char *, va_list);

/**
 * Wildcard characters used in REGEXP-like operators.
 */
#define MATCH_ONE_WILDCARD '_'
#define MATCH_ALL_WILDCARD '%'

int
sql_strlike_cs(const char *zLike, const char *zStr, unsigned int cEsc);

int
sql_strlike_ci(const char *zLike, const char *zStr, unsigned int cEsc);

typedef void (*sql_destructor_type) (void *);
#define SQL_STATIC      ((sql_destructor_type)0)
#define SQL_TRANSIENT   ((sql_destructor_type)-1)

int
sql_prepare(sql * db,	/* Database handle */
		const char *zSql,	/* SQL statement, UTF-8 encoded */
		int nByte,	/* Maximum length of zSql in bytes. */
		sql_stmt ** ppStmt,	/* OUT: Statement handle */
		const char **pzTail	/* OUT: Pointer to unused portion of zSql */
	);

int
sql_prepare_v2(sql * db,	/* Database handle */
		   const char *zSql,	/* SQL statement, UTF-8 encoded */
		   int nByte,	/* Maximum length of zSql in bytes. */
		   sql_stmt ** ppStmt,	/* OUT: Statement handle */
		   const char **pzTail	/* OUT: Pointer to unused portion of zSql */
	);

int
sql_step(sql_stmt *);

const void *
sql_column_blob(sql_stmt *, int iCol);

int
sql_column_bytes(sql_stmt *, int iCol);

int
sql_column_bytes16(sql_stmt *, int iCol);

double
sql_column_double(sql_stmt *, int iCol);

int
sql_column_int(sql_stmt *, int iCol);

sql_int64
sql_column_int64(sql_stmt *, int iCol);

sql_uint64
sql_column_uint64(sql_stmt *, int iCol);

const unsigned char *
sql_column_text(sql_stmt *,
		    int iCol);

int
sql_column_type(sql_stmt *, int iCol);

sql_value *
sql_column_value(sql_stmt *,
		     int iCol);

int
sql_finalize(sql_stmt * pStmt);

int
sql_exec(sql *,	/* An open database */
	     const char *sql,	/* SQL to be evaluated */
	     int (*callback) (void *, int, char **, char **),	/* Callback function */
	     void *,	/* 1st argument to callback */
	     char **errmsg	/* Error msg written here */
	);
#define SQL_IOERR_READ              (SQL_IOERR | (1<<8))
#define SQL_IOERR_SHORT_READ        (SQL_IOERR | (2<<8))
#define SQL_IOERR_WRITE             (SQL_IOERR | (3<<8))
#define SQL_IOERR_FSYNC             (SQL_IOERR | (4<<8))
#define SQL_IOERR_DIR_FSYNC         (SQL_IOERR | (5<<8))
#define SQL_IOERR_TRUNCATE          (SQL_IOERR | (6<<8))
#define SQL_IOERR_FSTAT             (SQL_IOERR | (7<<8))
#define SQL_IOERR_UNLOCK            (SQL_IOERR | (8<<8))
#define SQL_IOERR_RDLOCK            (SQL_IOERR | (9<<8))
#define SQL_IOERR_DELETE            (SQL_IOERR | (10<<8))
#define SQL_IOERR_BLOCKED           (SQL_IOERR | (11<<8))
#define SQL_IOERR_NOMEM             (SQL_IOERR | (12<<8))
#define SQL_IOERR_ACCESS            (SQL_IOERR | (13<<8))
#define SQL_IOERR_CHECKRESERVEDLOCK (SQL_IOERR | (14<<8))
#define SQL_IOERR_LOCK              (SQL_IOERR | (15<<8))
#define SQL_IOERR_CLOSE             (SQL_IOERR | (16<<8))
#define SQL_IOERR_DIR_CLOSE         (SQL_IOERR | (17<<8))
#define SQL_IOERR_SHMOPEN           (SQL_IOERR | (18<<8))
#define SQL_IOERR_SHMSIZE           (SQL_IOERR | (19<<8))
#define SQL_IOERR_SHMLOCK           (SQL_IOERR | (20<<8))
#define SQL_IOERR_SHMMAP            (SQL_IOERR | (21<<8))
#define SQL_IOERR_SEEK              (SQL_IOERR | (22<<8))
#define SQL_IOERR_DELETE_NOENT      (SQL_IOERR | (23<<8))
#define SQL_IOERR_MMAP              (SQL_IOERR | (24<<8))
#define SQL_IOERR_GETTEMPPATH       (SQL_IOERR | (25<<8))
#define SQL_IOERR_CONVPATH          (SQL_IOERR | (26<<8))
#define SQL_IOERR_VNODE             (SQL_IOERR | (27<<8))
#define SQL_CONSTRAINT_CHECK        (SQL_CONSTRAINT | (1<<8))
#define SQL_CONSTRAINT_FOREIGNKEY   (SQL_CONSTRAINT | (3<<8))
#define SQL_CONSTRAINT_FUNCTION     (SQL_CONSTRAINT | (4<<8))
#define SQL_CONSTRAINT_NOTNULL      (SQL_CONSTRAINT | (5<<8))
#define SQL_CONSTRAINT_PRIMARYKEY   (SQL_CONSTRAINT | (6<<8))
#define SQL_CONSTRAINT_TRIGGER      (SQL_CONSTRAINT | (7<<8))
#define SQL_CONSTRAINT_UNIQUE       (SQL_CONSTRAINT | (8<<8))

enum sql_type {
	SQL_INTEGER = 1,
	SQL_FLOAT = 2,
	SQL_TEXT = 3,
	SQL_BLOB = 4,
	SQL_NULL = 5,
	SQL_UNSIGNED = 6
};

/**
 * Subtype of a main type. Allows to do some subtype specific
 * things: serialization, unpacking etc.
 */
enum sql_subtype {
	SQL_SUBTYPE_NO = 0,
	SQL_SUBTYPE_MSGPACK = 77,
};

void
sql_randomness(int N, void *P);

/**
 * Return the number of affected rows in the last SQL statement.
 */
void
sql_row_count(struct sql_context *context, MAYBE_UNUSED int unused1,
	      MAYBE_UNUSED sql_value **unused2);

void *
sql_user_data(sql_context *);

void
sql_log(int iErrCode, const char *zFormat, ...);

void *
sql_aggregate_context(sql_context *,
			  int nBytes);


int
sql_column_count(sql_stmt * pStmt);

const char *
sql_column_name(sql_stmt *, int N);

const char *
sql_column_datatype(sql_stmt *, int N);

const char *
sql_errmsg(sql *);

int
sql_initialize(void);

int
sql_os_end(void);

#define SQL_CONFIG_SCRATCH       6	/* void*, int sz, int N */
#define SQL_CONFIG_MEMSTATUS     9	/* boolean */
#define SQL_CONFIG_LOOKASIDE    13	/* int int */
#define SQL_CONFIG_LOG          16	/* xFunc, void* */
#define SQL_CONFIG_URI          17	/* int */
#define SQL_CONFIG_COVERING_INDEX_SCAN 20	/* int */
#define SQL_CONFIG_SQLLOG       21	/* xSqllog, void* */
#define SQL_CONFIG_MMAP_SIZE    22	/* sql_int64, sql_int64 */
#define SQL_CONFIG_PMASZ               24	/* unsigned int szPma */
#define SQL_CONFIG_STMTJRNL_SPILL      25	/* int nByte */

#define SQL_DBCONFIG_LOOKASIDE             1001	/* void* int int */
#define SQL_DBCONFIG_ENABLE_FKEY           1002	/* int int* */
#define SQL_DBCONFIG_ENABLE_TRIGGER        1003	/* int int* */
#define SQL_DBCONFIG_NO_CKPT_ON_CLOSE      1006	/* int int* */

#define SQL_TRACE_STMT       0x01
#define SQL_TRACE_PROFILE    0x02
#define SQL_TRACE_ROW        0x04
#define SQL_TRACE_CLOSE      0x08

#define SQL_DETERMINISTIC    0x800

#define SQL_STATUS_MEMORY_USED          0
#define SQL_STATUS_PAGECACHE_USED       1
#define SQL_STATUS_PAGECACHE_OVERFLOW   2
#define SQL_STATUS_SCRATCH_USED         3
#define SQL_STATUS_SCRATCH_OVERFLOW     4
#define SQL_STATUS_MALLOC_SIZE          5
#define SQL_STATUS_PARSER_STACK         6
#define SQL_STATUS_PAGECACHE_SIZE       7
#define SQL_STATUS_SCRATCH_SIZE         8
#define SQL_STATUS_MALLOC_COUNT         9

sql_int64
sql_memory_used(void);

int
sql_create_function_v2(sql * db,
			   const char *zFunctionName,
			   enum field_type type,
			   int nArg,
			   int flags,
			   void *pApp,
			   void (*xFunc) (sql_context *,
					  int,
					  sql_value **),
			   void (*xStep) (sql_context *,
					  int,
					  sql_value **),
			   void (*xFinal)
			   (sql_context *),
			   void (*xDestroy) (void *)
	);

#define SQL_OPEN_READONLY         0x00000001	/* Ok for sql_open_v2() */
#define SQL_OPEN_READWRITE        0x00000002	/* Ok for sql_open_v2() */
#define SQL_OPEN_CREATE           0x00000004	/* Ok for sql_open_v2() */
#define SQL_OPEN_DELETEONCLOSE    0x00000008	/* VFS only */
#define SQL_OPEN_EXCLUSIVE        0x00000010	/* VFS only */
#define SQL_OPEN_AUTOPROXY        0x00000020	/* VFS only */
#define SQL_OPEN_URI              0x00000040	/* Ok for sql_open_v2() */
#define SQL_OPEN_MEMORY           0x00000080	/* Ok for sql_open_v2() */
#define SQL_OPEN_MAIN_DB          0x00000100	/* VFS only */
#define SQL_OPEN_TEMP_DB          0x00000200	/* VFS only */
#define SQL_OPEN_SHAREDCACHE      0x00020000	/* Ok for sql_open_v2() */
#define SQL_OPEN_PRIVATECACHE     0x00040000	/* Ok for sql_open_v2() */

sql_vfs *
sql_vfs_find(const char *zVfsName);

#define SQL_TESTCTRL_FIRST                    5
#define SQL_TESTCTRL_PRNG_SAVE                5
#define SQL_TESTCTRL_PRNG_RESTORE             6
#define SQL_TESTCTRL_PRNG_RESET               7
#define SQL_TESTCTRL_BITVEC_TEST              8
#define SQL_TESTCTRL_FAULT_INSTALL            9
#define SQL_TESTCTRL_BENIGN_MALLOC_HOOKS     10
#define SQL_TESTCTRL_PENDING_BYTE            11
#define SQL_TESTCTRL_ASSERT                  12
#define SQL_TESTCTRL_ALWAYS                  13
#define SQL_TESTCTRL_RESERVE                 14
#define SQL_TESTCTRL_OPTIMIZATIONS           15
#define SQL_TESTCTRL_ISKEYWORD               16
#define SQL_TESTCTRL_SCRATCHMALLOC           17
#define SQL_TESTCTRL_LOCALTIME_FAULT         18
#define SQL_TESTCTRL_EXPLAIN_STMT            19	/* NOT USED */
#define SQL_TESTCTRL_ONCE_RESET_THRESHOLD    19
#define SQL_TESTCTRL_NEVER_CORRUPT           20
#define SQL_TESTCTRL_VDBE_COVERAGE           21
#define SQL_TESTCTRL_BYTEORDER               22
#define SQL_TESTCTRL_ISINIT                  23
#define SQL_TESTCTRL_SORTER_MMAP             24
#define SQL_TESTCTRL_LAST                    24

int
sql_status64(int op, sql_int64 * pCurrent,
		 sql_int64 * pHighwater,
		 int resetFlag);

int
sql_config(int, ...);


typedef struct sql_io_methods sql_io_methods;
struct sql_io_methods {
	int iVersion;
	int (*xClose) (sql_file *);
	int (*xRead) (sql_file *, void *, int iAmt,
		      sql_int64 iOfst);
	int (*xWrite) (sql_file *, const void *, int iAmt,
		       sql_int64 iOfst);
	int (*xFileControl) (sql_file *, int op, void *pArg);
	/* Methods above are valid for version 2 */
	int (*xFetch) (sql_file *, sql_int64 iOfst, int iAmt,
		       void **pp);
	int (*xUnfetch) (sql_file *, sql_int64 iOfst, void *p);
	/* Methods above are valid for version 3 */
	/* Additional methods may be added in future releases */
};

#define SQL_FCNTL_LOCKSTATE               1
#define SQL_FCNTL_GET_LOCKPROXYFILE       2
#define SQL_FCNTL_SET_LOCKPROXYFILE       3
#define SQL_FCNTL_LAST_ERRNO              4
#define SQL_FCNTL_SIZE_HINT               5
#define SQL_FCNTL_CHUNK_SIZE              6
#define SQL_FCNTL_FILE_POINTER            7
#define SQL_FCNTL_SYNC_OMITTED            8
#define SQL_FCNTL_OVERWRITE              10
#define SQL_FCNTL_VFSNAME                11
#define SQL_FCNTL_POWERSAFE_OVERWRITE    12
#define SQL_FCNTL_PRAGMA                 13
#define SQL_FCNTL_TEMPFILENAME           15
#define SQL_FCNTL_MMAP_SIZE              16
#define SQL_FCNTL_TRACE                  17
#define SQL_FCNTL_HAS_MOVED              18
#define SQL_FCNTL_SYNC                   19

int
sql_os_init(void);

sql_int64
sql_soft_heap_limit64(sql_int64 N);

int
sql_limit(sql *, int id, int newVal);

#define SQL_SYNC_NORMAL        0x00002
#define SQL_SYNC_FULL          0x00003
#define SQL_SYNC_DATAONLY      0x00010

extern char *
sql_temp_directory;

const char *
sql_uri_parameter(const char *zFilename,
		      const char *zParam);

#define SQL_ACCESS_EXISTS    0
#define SQL_ACCESS_READWRITE 1	/* Used by PRAGMA temp_store_directory */
#define SQL_ACCESS_READ      2	/* Unused */

#define SQL_DBSTATUS_LOOKASIDE_USED       0
#define SQL_DBSTATUS_CACHE_USED           1
#define SQL_DBSTATUS_SCHEMA_USED          2
#define SQL_DBSTATUS_STMT_USED            3
#define SQL_DBSTATUS_LOOKASIDE_HIT        4
#define SQL_DBSTATUS_LOOKASIDE_MISS_SIZE  5
#define SQL_DBSTATUS_LOOKASIDE_MISS_FULL  6
#define SQL_DBSTATUS_CACHE_HIT            7
#define SQL_DBSTATUS_CACHE_MISS           8
#define SQL_DBSTATUS_CACHE_WRITE          9
#define SQL_DBSTATUS_DEFERRED_FKS        10
#define SQL_DBSTATUS_CACHE_USED_SHARED   11
#define SQL_DBSTATUS_MAX                 11	/* Largest defined DBSTATUS */

const char *
sql_sql(sql_stmt * pStmt);

int
sql_vfs_register(sql_vfs *, int makeDflt);

#define SQL_STMTSTATUS_FULLSCAN_STEP     1
#define SQL_STMTSTATUS_SORT              2
#define SQL_STMTSTATUS_AUTOINDEX         3
#define SQL_STMTSTATUS_VM_STEP           4

void
sql_interrupt(sql *);

int
sql_bind_blob(sql_stmt *, int, const void *,
		  int n, void (*)(void *));

int
sql_bind_blob64(sql_stmt *, int, const void *,
		    sql_uint64, void (*)(void *));

int
sql_bind_double(sql_stmt *, int, double);

int
sql_bind_int(sql_stmt *, int, int);

int
sql_bind_int64(sql_stmt *, int, sql_int64);

int
sql_bind_uint64(sql_stmt *, int, sql_uint64);

int
sql_bind_null(sql_stmt *, int);

int
sql_bind_text(sql_stmt *, int, const char *, int,
		  void (*)(void *));

int
sql_bind_text64(sql_stmt *, int, const char *,
		    sql_uint64, void (*)(void *));
int
sql_bind_value(sql_stmt *, int,
		   const sql_value *);

int
sql_bind_zeroblob(sql_stmt *, int, int n);

int
sql_bind_zeroblob64(sql_stmt *, int,
			sql_uint64);

int
sql_stmt_busy(sql_stmt *);

int
sql_init_db(sql **db);

int
sql_close(sql *);


/**
 * Get number of the named parameter in the prepared sql
 * statement.
 * @param pStmt Prepared statement.
 * @param zName Parameter name.
 * @param nName Parameter name length.
 *
 * @retval > 0 Number of the parameter.
 * @retval   0 Parameter is not found.
 */
int
sql_bind_parameter_lindex(sql_stmt * pStmt, const char *zName,
			      int nName);

/*
 * If compiling for a processor that lacks floating point support,
 * substitute integer for floating-point
 */
#ifdef SQL_OMIT_FLOATING_POINT
#define double sql_int64
#define float sql_int64
#define LONGDOUBLE_TYPE sql_int64
#ifndef SQL_BIG_DBL
#define SQL_BIG_DBL (((sql_int64)1)<<50)
#endif
#define SQL_OMIT_DATETIME_FUNCS 1
#define SQL_OMIT_TRACE 1
#undef SQL_MIXED_ENDIAN_64BIT_FLOAT
#undef SQL_HAVE_ISNAN
#endif
#ifndef SQL_BIG_DBL
#define SQL_BIG_DBL (1e99)
#endif

/*
 * OMIT_TEMPDB is set to 1 if sql_OMIT_TEMPDB is defined, or 0
 * afterward. Having this macro allows us to cause the C compiler
 * to omit code used by TEMP tables without messy #ifndef statements.
 */
#ifdef SQL_OMIT_TEMPDB
#define OMIT_TEMPDB 1
#else
#define OMIT_TEMPDB 0
#endif

/*
 * Determine whether triggers are recursive by default.  This can be
 * changed at run-time using a pragma.
 */
#ifndef SQL_DEFAULT_RECURSIVE_TRIGGERS
#define SQL_DEFAULT_RECURSIVE_TRIGGERS 0
#endif

/*
 * Provide a default value for sql_TEMP_STORE in case it is not specified
 * on the command-line
 */
#ifndef SQL_TEMP_STORE
#define SQL_TEMP_STORE 1
#define SQL_TEMP_STORE_xc 1	/* Exclude from ctime.c */
#endif

/*
 * If no value has been provided for sql_MAX_WORKER_THREADS, or if
 * sql_TEMP_STORE is set to 3 (never use temporary files), set it
 * to zero.
 */
#if SQL_TEMP_STORE==3
#undef SQL_MAX_WORKER_THREADS
#define SQL_MAX_WORKER_THREADS 0
#endif
#ifndef SQL_MAX_WORKER_THREADS
#define SQL_MAX_WORKER_THREADS 8
#endif
#ifndef SQL_DEFAULT_WORKER_THREADS
#define SQL_DEFAULT_WORKER_THREADS 0
#endif
#if SQL_DEFAULT_WORKER_THREADS>SQL_MAX_WORKER_THREADS
#undef SQL_MAX_WORKER_THREADS
#define SQL_MAX_WORKER_THREADS SQL_DEFAULT_WORKER_THREADS
#endif

/**
 * Default count of allowed compound selects.
 *
 * Tarantool: gh-2548, gh-3382: Fiber stack is 64KB by default,
 * so maximum number of entities should be less than 30 or stack
 * guard will be triggered (triggered with clang).
*/
#ifndef SQL_DEFAULT_COMPOUND_SELECT
#define SQL_DEFAULT_COMPOUND_SELECT 30
#endif

/*
 * The default initial allocation for the pagecache when using separate
 * pagecaches for each database connection.  A positive number is the
 * number of pages.  A negative number N translations means that a buffer
 * of -1024*N bytes is allocated and used for as many pages as it will hold.
 */
#ifndef SQL_DEFAULT_PCACHE_INITSZ
#define SQL_DEFAULT_PCACHE_INITSZ 100
#endif

/*
 * GCC does not define the offsetof() macro so we'll have to do it
 * ourselves.
 */
#ifndef offsetof
#define offsetof(STRUCTURE,FIELD) ((int)((char*)&((STRUCTURE*)0)->FIELD))
#endif

/*
 * Macros to compute minimum and maximum of two numbers.
 */
#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif
#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif

/*
 * Integers of known sizes.  These typedefs might change for architectures
 * where the sizes very.  Preprocessor macros are available so that the
 * types can be conveniently redefined at compile-type.  Like this:
 *
 *         cc '-DUINTPTR_TYPE=long long int' ...
 */
#ifndef UINT32_TYPE
#ifdef HAVE_UINT32_T
#define UINT32_TYPE uint32_t
#else
#define UINT32_TYPE unsigned int
#endif
#endif
#ifndef UINT16_TYPE
#ifdef HAVE_UINT16_T
#define UINT16_TYPE uint16_t
#else
#define UINT16_TYPE unsigned short int
#endif
#endif
#ifndef INT16_TYPE
#ifdef HAVE_INT16_T
#define INT16_TYPE int16_t
#else
#define INT16_TYPE short int
#endif
#endif
#ifndef UINT8_TYPE
#ifdef HAVE_UINT8_T
#define UINT8_TYPE uint8_t
#else
#define UINT8_TYPE unsigned char
#endif
#endif
#ifndef INT8_TYPE
#ifdef HAVE_INT8_T
#define INT8_TYPE int8_t
#else
#define INT8_TYPE signed char
#endif
#endif
#ifndef LONGDOUBLE_TYPE
#define LONGDOUBLE_TYPE long double
#endif
typedef sql_int64 i64;	/* 8-byte signed integer */
typedef sql_uint64 u64;	/* 8-byte unsigned integer */
typedef UINT32_TYPE u32;	/* 4-byte unsigned integer */
typedef UINT16_TYPE u16;	/* 2-byte unsigned integer */
typedef INT16_TYPE i16;		/* 2-byte signed integer */
typedef UINT8_TYPE u8;		/* 1-byte unsigned integer */
typedef INT8_TYPE i8;		/* 1-byte signed integer */

/*
 * sql_MAX_U32 is a u64 constant that is the maximum u64 value
 * that can be stored in a u32 without loss of data.  The value
 * is 0x00000000ffffffff.  But because of quirks of some compilers, we
 * have to specify the value in the less intuitive manner shown:
 */
#define SQL_MAX_U32  ((((u64)1)<<32)-1)

/*
 * The datatype used to store estimates of the number of rows in a
 * table or index.  This is an unsigned integer type.  For 99.9% of
 * the world, a 32-bit integer is sufficient.  But a 64-bit integer
 * can be used at compile-time if desired.
 */
#ifdef SQL_64BIT_STATS
typedef u64 tRowcnt;		/* 64-bit only if requested at compile-time */
#else
typedef u32 tRowcnt;		/* 32-bit is the default */
#endif

/*
 * Estimated quantities used for query planning are stored as 16-bit
 * logarithms.  For quantity X, the value stored is 10*log2(X).  This
 * gives a possible range of values of approximately 1.0e986 to 1e-986.
 * But the allowed values are "grainy".  Not every value is representable.
 * For example, quantities 16 and 17 are both represented by a LogEst
 * of 40.  However, since LogEst quantities are suppose to be estimates,
 * not exact values, this imprecision is not a problem.
 *
 * "LogEst" is short for "Logarithmic Estimate".
 *
 * Examples:
 *      1 -> 0              20 -> 43          10000 -> 132
 *      2 -> 10             25 -> 46          25000 -> 146
 *      3 -> 16            100 -> 66        1000000 -> 199
 *      4 -> 20           1000 -> 99        1048576 -> 200
 *     10 -> 33           1024 -> 100    4294967296 -> 320
 *
 * The LogEst can be negative to indicate fractional values.
 * Examples:
 *
 *    0.5 -> -10           0.1 -> -33        0.0625 -> -40
 */
typedef INT16_TYPE LogEst;

/*
 * Set the sql_PTRSIZE macro to the number of bytes in a pointer
 */
#ifndef SQL_PTRSIZE
#if defined(__SIZEOF_POINTER__)
#define SQL_PTRSIZE __SIZEOF_POINTER__
#elif defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
       defined(_M_ARM)   || defined(__arm__)    || defined(__x86)
#define SQL_PTRSIZE 4
#else
#define SQL_PTRSIZE 8
#endif
#endif

/* The uptr type is an unsigned integer large enough to hold a pointer
 */
#if defined(HAVE_STDINT_H)
typedef uintptr_t uptr;
#elif SQL_PTRSIZE==4
typedef u32 uptr;
#else
typedef u64 uptr;
#endif

/*
 * The sql_WITHIN(P,S,E) macro checks to see if pointer P points to
 * something between S (inclusive) and E (exclusive).
 *
 * In other words, S is a buffer and E is a pointer to the first byte after
 * the end of buffer S.  This macro returns true if P points to something
 * contained within the buffer S.
 */
#define SQL_WITHIN(P,S,E) (((uptr)(P)>=(uptr)(S))&&((uptr)(P)<(uptr)(E)))

/*
 * Macros to determine whether the machine is big or little endian,
 * and whether or not that determination is run-time or compile-time.
 *
 * For best performance, an attempt is made to guess at the byte-order
 * using C-preprocessor macros.  If that is unsuccessful, or if
 * -Dsql_RUNTIME_BYTEORDER=1 is set, then byte-order is determined
 * at run-time.
 */
#if (defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)  ||    \
     defined(_M_AMD64) || defined(_M_ARM)     || defined(__x86)   ||    \
     defined(__arm__)) && !defined(SQL_RUNTIME_BYTEORDER)
#define SQL_BYTEORDER    1234
#define SQL_BIGENDIAN    0
#define SQL_LITTLEENDIAN 1
#endif
#if (defined(sparc)    || defined(__ppc__))  \
    && !defined(SQL_RUNTIME_BYTEORDER)
#define SQL_BYTEORDER    4321
#define SQL_BIGENDIAN    1
#define SQL_LITTLEENDIAN 0
#endif
#if !defined(SQL_BYTEORDER)
#ifdef SQL_AMALGAMATION
const int sqlone = 1;
#else
extern const int sqlone;
#endif
#define SQL_BYTEORDER    0	/* 0 means "unknown at compile-time" */
#define SQL_BIGENDIAN    (*(char *)(&sqlone)==0)
#define SQL_LITTLEENDIAN (*(char *)(&sqlone)==1)
#define SQL_UTF16NATIVE  (SQL_BIGENDIAN?SQL_UTF16BE:SQL_UTF16LE)
#endif

/*
 * Constants for the largest and smallest possible 64-bit signed integers.
 * These macros are designed to work correctly on both 32-bit and 64-bit
 * compilers.
 */
#define LARGEST_INT64  (0xffffffff|(((i64)0x7fffffff)<<32))
#define SMALLEST_INT64 (((i64)-1) - LARGEST_INT64)

/*
 * Round up a number to the next larger multiple of 8.  This is used
 * to force 8-byte alignment on 64-bit architectures.
 */
#define ROUND8(x)     (((x)+7)&~7)

/*
 * Round down to the nearest multiple of 8
 */
#define ROUNDDOWN8(x) ((x)&~7)

/*
 * Assert that the pointer X is aligned to an 8-byte boundary.  This
 * macro is used only within assert() to verify that the code gets
 * all alignment restrictions correct.
 *
 * Except, if sql_4_BYTE_ALIGNED_MALLOC is defined, then the
 * underlying malloc() implementation might return us 4-byte aligned
 * pointers.  In that case, only verify 4-byte alignment.
 */
#ifdef SQL_4_BYTE_ALIGNED_MALLOC
#define EIGHT_BYTE_ALIGNMENT(X)   ((((char*)(X) - (char*)0)&3)==0)
#else
#define EIGHT_BYTE_ALIGNMENT(X)   ((((char*)(X) - (char*)0)&7)==0)
#endif

/*
 * Default maximum size of memory used by memory-mapped I/O in the VFS
 */
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifndef SQL_MAX_MMAP_SIZE
#if defined(__linux__) \
  || (defined(__APPLE__) && defined(__MACH__))
#define SQL_MAX_MMAP_SIZE 0x7fff0000	/* 2147418112 */
#else
#define SQL_MAX_MMAP_SIZE 0
#endif
#define SQL_MAX_MMAP_SIZE_xc 1	/* exclude from ctime.c */
#endif

/*
 * The default MMAP_SIZE is zero on all platforms.  Or, even if a larger
 * default MMAP_SIZE is specified at compile-time, make sure that it does
 * not exceed the maximum mmap size.
 */
#ifndef SQL_DEFAULT_MMAP_SIZE
#define SQL_DEFAULT_MMAP_SIZE 0
#define SQL_DEFAULT_MMAP_SIZE_xc 1	/* Exclude from ctime.c */
#endif
#if SQL_DEFAULT_MMAP_SIZE>SQL_MAX_MMAP_SIZE
#undef SQL_DEFAULT_MMAP_SIZE
#define SQL_DEFAULT_MMAP_SIZE SQL_MAX_MMAP_SIZE
#endif

/*
 * A convenience macro that returns the number of elements in
 * an array.
 */
#define ArraySize(X)    ((int)(sizeof(X)/sizeof(X[0])))

/*
 * Determine if the argument is a power of two
 */
#define IsPowerOfTwo(X) (((X)&((X)-1))==0)

/*
 * The following value as a destructor means to use sqlDbFree().
 * The sqlDbFree() routine requires two parameters instead of the
 * one parameter that destructors normally want.  So we have to introduce
 * this magic value that the code knows to handle differently.  Any
 * pointer will work here as long as it is distinct from sql_STATIC
 * and sql_TRANSIENT.
 */
#define SQL_DYNAMIC   ((sql_destructor_type)sqlMallocSize)

/*
 * When sql_OMIT_WSD is defined, it means that the target platform does
 * not support Writable Static Data (WSD) such as global and static variables.
 * All variables must either be on the stack or dynamically allocated from
 * the heap.  When WSD is unsupported, the variable declarations scattered
 * throughout the sql code must become constants instead.  The sql_WSD
 * macro is used for this purpose.  And instead of referencing the variable
 * directly, we use its constant as a key to lookup the run-time allocated
 * buffer that holds real variable.  The constant is also the initializer
 * for the run-time allocated buffer.
 *
 * In the usual case where WSD is supported, the sql_WSD and GLOBAL
 * macros become no-ops and have zero performance impact.
 */
#ifdef SQL_OMIT_WSD
#define SQL_WSD const
#define GLOBAL(t,v) (*(t*)sql_wsd_find((void*)&(v), sizeof(v)))
#define sqlGlobalConfig GLOBAL(struct sqlConfig, sqlConfig)
int sql_wsd_init(int N, int J);
void *sql_wsd_find(void *K, int L);
#else
#define SQL_WSD
#define GLOBAL(t,v) v
#define sqlGlobalConfig sqlConfig
#endif

/*
 * The following macros are used to suppress compiler warnings and to
 * make it clear to human readers when a function parameter is deliberately
 * left unused within the body of a function. This usually happens when
 * a function is called via a function pointer. For example the
 * implementation of an SQL aggregate step callback may not use the
 * parameter indicating the number of arguments passed to the aggregate,
 * if it knows that this is enforced elsewhere.
 *
 * When a function parameter is not used at all within the body of a function,
 * it is generally named "NotUsed" or "NotUsed2" to make things even clearer.
 * However, these macros may also be used to suppress warnings related to
 * parameters that may or may not be used depending on compilation options.
 * For example those parameters only used in assert() statements. In these
 * cases the parameters are named as per the usual conventions.
 */
#define UNUSED_PARAMETER(x) (void)(x)
#define UNUSED_PARAMETER2(x,y) UNUSED_PARAMETER(x),UNUSED_PARAMETER(y)

/*
 * Forward references to structures
 */
typedef struct AggInfo AggInfo;
typedef struct Bitvec Bitvec;
typedef struct Column Column;
typedef struct Expr Expr;
typedef struct ExprList ExprList;
typedef struct ExprSpan ExprSpan;
typedef struct FuncDestructor FuncDestructor;
typedef struct FuncDef FuncDef;
typedef struct FuncDefHash FuncDefHash;
typedef struct IdList IdList;
typedef struct KeyClass KeyClass;
typedef struct Lookaside Lookaside;
typedef struct LookasideSlot LookasideSlot;
typedef struct NameContext NameContext;
typedef struct Parse Parse;
typedef struct PrintfArguments PrintfArguments;
typedef struct RowSet RowSet;
typedef struct Savepoint Savepoint;
typedef struct Select Select;
typedef struct sqlThread sqlThread;
typedef struct SelectDest SelectDest;
typedef struct SrcList SrcList;
typedef struct StrAccum StrAccum;
typedef struct Token Token;
typedef struct TreeView TreeView;
typedef struct TriggerPrg TriggerPrg;
typedef struct TriggerStep TriggerStep;
typedef struct UnpackedRecord UnpackedRecord;
typedef struct Walker Walker;
typedef struct WhereInfo WhereInfo;
typedef struct With With;

/* A VList object records a mapping between parameters/variables/wildcards
 * in the SQL statement (such as $abc, @pqr, or :xyz) and the integer
 * variable number associated with that parameter.  See the format description
 * on the sqlVListAdd() routine for more information.  A VList is really
 * just an array of integers.
 */
typedef int VList;

/*
 * Defer sourcing vdbe.h and cursor.h until after the "u8" and
 * "BusyHandler" typedefs. vdbe.h also requires a few of the opaque
 * pointer types (i.e. FuncDef) defined above.
 */
#include "cursor.h"
#include "vdbe.h"
#include "os.h"

/*
 * The number of different kinds of things that can be limited
 * using the sql_limit() interface.
 */
#define SQL_N_LIMIT (SQL_LIMIT_WORKER_THREADS+1)

/*
 * Lookaside malloc is a set of fixed-size buffers that can be used
 * to satisfy small transient memory allocation requests for objects
 * associated with a particular database connection.  The use of
 * lookaside malloc provides a significant performance enhancement
 * (approx 10%) by avoiding numerous malloc/free requests while parsing
 * SQL statements.
 *
 * The Lookaside structure holds configuration information about the
 * lookaside malloc subsystem.  Each available memory allocation in
 * the lookaside subsystem is stored on a linked list of LookasideSlot
 * objects.
 *
 * Lookaside allocations are only allowed for objects that are associated
 * with a particular database connection.  Hence, schema information cannot
 * be stored in lookaside because in shared cache mode the schema information
 * is shared by multiple database connections.  Therefore, while parsing
 * schema information, the Lookaside.bEnabled flag is cleared so that
 * lookaside allocations are not used to construct the schema objects.
 */
struct Lookaside {
	u32 bDisable;		/* Only operate the lookaside when zero */
	u16 sz;			/* Size of each buffer in bytes */
	u8 bMalloced;		/* True if pStart obtained from sql_malloc() */
	int nOut;		/* Number of buffers currently checked out */
	int mxOut;		/* Highwater mark for nOut */
	int anStat[3];		/* 0: hits.  1: size misses.  2: full misses */
	LookasideSlot *pFree;	/* List of available buffers */
	void *pStart;		/* First byte of available memory space */
	void *pEnd;		/* First byte past end of available space */
};
struct LookasideSlot {
	LookasideSlot *pNext;	/* Next buffer in the list of free buffers */
};

/*
 * A hash table for built-in function definitions.  (Application-defined
 * functions use a regular table table from hash.h.)
 *
 * Hash each FuncDef structure into one of the FuncDefHash.a[] slots.
 * Collisions are on the FuncDef.u.pHash chain.
 */
#define SQL_FUNC_HASH_SZ 23
struct FuncDefHash {
	FuncDef *a[SQL_FUNC_HASH_SZ];	/* Hash table for functions */
};

/*
 * Each database connection is an instance of the following structure.
 */
struct sql {
	sql_vfs *pVfs;	/* OS Interface */
	struct Vdbe *pVdbe;	/* List of active virtual machines */
	struct coll *pDfltColl;	/* The default collating sequence (BINARY) */
	i64 szMmap;		/* Default mmap_size setting */
	int errCode;		/* Most recent error code (sql_*) */
	int errMask;		/* & result codes with this before returning */
	int iSysErrno;		/* Errno value from last system error */
	u16 dbOptFlags;		/* Flags to enable/disable optimizations */
	u8 enc;			/* Text encoding */
	u8 temp_store;		/* 1: file 2: memory 0: default */
	u8 mallocFailed;	/* True if we have seen a malloc failure */
	u8 bBenignMalloc;	/* Do not require OOMs if true */
	u8 dfltLockMode;	/* Default locking-mode for attached dbs */
	u8 mTrace;		/* zero or more sql_TRACE flags */
	u32 magic;		/* Magic number for detect library misuse */
	/** Value returned by sql_row_count(). */
	int nChange;
	int aLimit[SQL_N_LIMIT];	/* Limits */
	int nMaxSorterMmap;	/* Maximum size of regions mapped by sorter */
	struct sqlInitInfo {	/* Information used during initialization */
		uint32_t space_id;
		uint32_t index_id;
		u8 busy;	/* TRUE if currently initializing */
		u8 orphanTrigger;	/* Last statement is orphaned TEMP trigger */
		u8 imposterTable;	/* Building an imposter table */
	} init;
	int nVdbeActive;	/* Number of VDBEs currently running */
	int nVdbeExec;		/* Number of nested calls to VdbeExec() */
	int (*xTrace) (u32, void *, void *, void *);	/* Trace function */
	void *pTraceArg;	/* Argument to the trace function */
	void (*xProfile) (void *, const char *, u64);	/* Profiling function */
	void *pProfileArg;	/* Argument to profile function */
	void *pCommitArg;	/* Argument to xCommitCallback() */
	int (*xCommitCallback) (void *);	/* Invoked at every commit. */
	void *pRollbackArg;	/* Argument to xRollbackCallback() */
	void (*xRollbackCallback) (void *);	/* Invoked at every commit. */
	void *pUpdateArg;
	void (*xUpdateCallback) (void *, int, const char *, const char *,
				 sql_int64);
	sql_value *pErr;	/* Most recent error message */
	union {
		volatile int isInterrupted;	/* True if sql_interrupt has been called */
		double notUsed1;	/* Spacer */
	} u1;
	Lookaside lookaside;	/* Lookaside malloc configuration */
#ifndef SQL_OMIT_PROGRESS_CALLBACK
	int (*xProgress) (void *);	/* The progress callback */
	void *pProgressArg;	/* Argument to the progress callback */
	unsigned nProgressOps;	/* Number of opcodes for progress callback */
#endif
	Hash aFunc;		/* Hash table of connection functions */
	int *pnBytesFreed;	/* If not NULL, increment this in DbFree() */
};

/*
 * Possible values for the sql.flags.
 */
#define SQL_VdbeTrace      0x00000001	/* True to trace VDBE execution */
/* Debug print info about SQL query as it parsed */
#define PARSER_TRACE_FLAG  0x00000002
#define SQL_FullColNames   0x00000004	/* Show full column names on SELECT */
/* True if LIKE is case sensitive. */
#define LIKE_CASE_SENS_FLAG 0x00000008
#define SQL_ShortColNames  0x00000040	/* Show short columns names */
#define SQL_CountRows      0x00000080	/* Count rows changed by INSERT, */
					  /*   DELETE, or UPDATE and return */
					  /*   the count using a callback. */
#define SQL_NullCallback   0x00000100	/* Invoke the callback once if the */
					  /*   result set is empty */
#define SQL_SqlTrace       0x00000200	/* Debug print SQL as it executes */
#define SQL_SelectTrace    0x00000800       /* Debug info about select statement */
#define SQL_WhereTrace     0x00008000       /* Debug info about optimizer's work */
#define SQL_VdbeListing    0x00000400	/* Debug listings of VDBE programs */
#define SQL_VdbeAddopTrace 0x00001000	/* Trace sqlVdbeAddOp() calls */
#define SQL_ReverseOrder   0x00020000	/* Reverse unordered SELECTs */
#define SQL_RecTriggers    0x00040000	/* Enable recursive triggers */
#define SQL_AutoIndex      0x00100000	/* Enable automatic indexes */
#define SQL_PreferBuiltin  0x00200000	/* Preference to built-in funcs */
#define SQL_EnableTrigger  0x01000000	/* True to enable triggers */
#define SQL_DeferFKs       0x02000000	/* Defer all FK constraints */
#define SQL_VdbeEQP        0x08000000	/* Debug EXPLAIN QUERY PLAN */

/*
 * Bits of the sql.dbOptFlags field that are used by the
 * sql_test_control(sql_TESTCTRL_OPTIMIZATIONS,...) interface to
 * selectively disable various optimizations.
 */
#define SQL_QueryFlattener 0x0001	/* Query flattening */
#define SQL_ColumnCache    0x0002	/* Column cache */
#define SQL_GroupByOrder   0x0004	/* GROUPBY cover of ORDERBY */
#define SQL_FactorOutConst 0x0008	/* Constant factoring */
/*                not used    0x0010   // Was: sql_IdxRealAsInt */
#define SQL_DistinctOpt    0x0020	/* DISTINCT using indexes */
#define SQL_CoverIdxScan   0x0040	/* Covering index scans */
#define SQL_OrderByIdxJoin 0x0080	/* ORDER BY of joins via index */
#define SQL_SubqCoroutine  0x0100	/* Evaluate subqueries as coroutines */
#define SQL_Transitive     0x0200	/* Transitive constraints */
#define SQL_OmitNoopJoin   0x0400	/* Omit unused tables in joins */
#define SQL_AllOpts        0xffff	/* All optimizations */

/*
 * Macros for testing whether or not optimizations are enabled or disabled.
 */
#define OptimizationDisabled(db, mask)  (((db)->dbOptFlags&(mask))!=0)
#define OptimizationEnabled(db, mask)   (((db)->dbOptFlags&(mask))==0)

/*
 * Return true if it OK to factor constant expressions into the initialization
 * code. The argument is a Parse object for the code generator.
 */
#define ConstFactorOk(P) ((P)->okConstFactor)

/*
 * Possible values for the sql.magic field.
 * The numbers are obtained at random and have no special meaning, other
 * than being distinct from one another.
 */
#define SQL_MAGIC_OPEN     0xa029a697	/* Database is open */
#define SQL_MAGIC_CLOSED   0x9f3c2d33	/* Database is closed */
#define SQL_MAGIC_SICK     0x4b771290	/* Error and awaiting close */
#define SQL_MAGIC_BUSY     0xf03b7906	/* Database currently in use */
#define SQL_MAGIC_ERROR    0xb5357930	/* An sql_MISUSE error occurred */
#define SQL_MAGIC_ZOMBIE   0x64cffc7f	/* Close with last statement close */

/**
 * SQL type definition. Now it is an alias to type, but in
 * future it will have some attributes like number of chars in
 * VARCHAR(<number of chars>).
 */
struct type_def {
	enum field_type type;
};

/*
 * Each SQL function is defined by an instance of the following
 * structure.  For global built-in functions (ex: substr(), max(), count())
 * a pointer to this structure is held in the sqlBuiltinFunctions object.
 * For per-connection application-defined functions, a pointer to this
 * structure is held in the db->aHash hash table.
 *
 * The u.pHash field is used by the global built-ins.  The u.pDestructor
 * field is used by per-connection app-def functions.
 */
struct FuncDef {
	i8 nArg;		/* Number of arguments.  -1 means unlimited */
	u16 funcFlags;		/* Some combination of sql_FUNC_* */
	void *pUserData;	/* User data parameter */
	FuncDef *pNext;		/* Next function with same name */
	void (*xSFunc) (sql_context *, int, sql_value **);	/* func or agg-step */
	void (*xFinalize) (sql_context *);	/* Agg finalizer */
	const char *zName;	/* SQL name of the function. */
	union {
		FuncDef *pHash;	/* Next with a different name but the same hash */
		FuncDestructor *pDestructor;	/* Reference counted destructor function */
	} u;
	/* Return type. */
	enum field_type ret_type;
	/**
	 * If function returns string, it may require collation
	 * to be applied on its result. For instance, result of
	 * substr() built-in function must have the same collation
	 * as its first argument.
	 */
	bool is_coll_derived;
};

/*
 * This structure encapsulates a user-function destructor callback (as
 * configured using create_function_v2()) and a reference counter. When
 * create_function_v2() is called to create a function with a destructor,
 * a single object of this type is allocated. FuncDestructor.nRef is set to
 * the number of FuncDef objects created (either 1 or 3, depending on whether
 * or not the specified encoding is sql_ANY). The FuncDef.pDestructor
 * member of each of the new FuncDef objects is set to point to the allocated
 * FuncDestructor.
 *
 * Thereafter, when one of the FuncDef objects is deleted, the reference
 * count on this object is decremented. When it reaches 0, the destructor
 * is invoked and the FuncDestructor structure freed.
 */
struct FuncDestructor {
	int nRef;
	void (*xDestroy) (void *);
	void *pUserData;
};

/*
 * Possible values for FuncDef.flags.  Note that the _LENGTH and _TYPEOF
 * values must correspond to OPFLAG_LENGTHARG and OPFLAG_TYPEOFARG.  And
 * sql_FUNC_CONSTANT must be the same as sql_DETERMINISTIC.  There
 * are assert() statements in the code to verify this.
 *
 * Value constraints (enforced via assert()):
 *     SQL_FUNC_MINMAX    ==  NC_MinMaxAgg      == SF_MinMaxAgg
 *     SQL_FUNC_LENGTH    ==  OPFLAG_LENGTHARG
 *     SQL_FUNC_TYPEOF    ==  OPFLAG_TYPEOFARG
 *     SQL_FUNC_CONSTANT  ==  sql_DETERMINISTIC from the API
 */
#define SQL_FUNC_LIKE     0x0004	/* Candidate for the LIKE optimization */
#define SQL_FUNC_CASE     0x0008	/* Case-sensitive LIKE-type function */
#define SQL_FUNC_EPHEM    0x0010	/* Ephemeral.  Delete with VDBE */
#define SQL_FUNC_NEEDCOLL 0x0020	/* sqlGetFuncCollSeq() might be called */
#define SQL_FUNC_LENGTH   0x0040	/* Built-in length() function */
#define SQL_FUNC_TYPEOF   0x0080	/* Built-in typeof() function */
#define SQL_FUNC_COUNT    0x0100	/* Built-in count(*) aggregate */
#define SQL_FUNC_COALESCE 0x0200	/* Built-in coalesce() or ifnull() */
#define SQL_FUNC_UNLIKELY 0x0400	/* Built-in unlikely() function */
#define SQL_FUNC_CONSTANT 0x0800	/* Constant inputs give a constant output */
#define SQL_FUNC_MINMAX   0x1000	/* True for min() and max() aggregates */
#define SQL_FUNC_SLOCHNG  0x2000	/* "Slow Change". Value constant during a
					 * single query - might change over time
					 */

/*
 * The following three macros, FUNCTION(), LIKEFUNC() and AGGREGATE() are
 * used to create the initializers for the FuncDef structures.
 *
 *   FUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Used to create a scalar function definition of a function zName
 *     implemented by C function xFunc that accepts nArg arguments. The
 *     value passed as iArg is cast to a (void*) and made available
 *     as the user-data (sql_user_data()) for the function. If
 *     argument bNC is true, then the sql_FUNC_NEEDCOLL flag is set.
 *
 *   FUNCTION_COLL
 *     Like FUNCTION except it assumes that function returns
 *     STRING which collation should be derived from first
 *     argument (trim, substr etc).
 *
 *   VFUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Like FUNCTION except it omits the sql_FUNC_CONSTANT flag.
 *
 *   DFUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Like FUNCTION except it omits the sql_FUNC_CONSTANT flag and
 *     adds the sql_FUNC_SLOCHNG flag.  Used for date & time functions,
 *     but not during a single query.
 *
 *   AGGREGATE(zName, nArg, iArg, bNC, xStep, xFinal)
 *     Used to create an aggregate function definition implemented by
 *     the C functions xStep and xFinal. The first four parameters
 *     are interpreted in the same way as the first 4 parameters to
 *     FUNCTION().
 *
 *   LIKEFUNC(zName, nArg, pArg, flags)
 *     Used to create a scalar function definition of a function zName
 *     that accepts nArg arguments and is implemented by a call to C
 *     function likeFunc. Argument pArg is cast to a (void *) and made
 *     available as the function user-data (sql_user_data()). The
 *     FuncDef.flags variable is set to the value passed as the flags
 *     parameter.
 */
#define FUNCTION(zName, nArg, iArg, bNC, xFunc, type) \
  {nArg, SQL_FUNC_CONSTANT|(bNC*SQL_FUNC_NEEDCOLL), \
   SQL_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0}, type, false}
#define FUNCTION_COLL(zName, nArg, iArg, bNC, xFunc) \
  {nArg, SQL_FUNC_CONSTANT|(bNC*SQL_FUNC_NEEDCOLL), \
   SQL_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0}, FIELD_TYPE_STRING, true}
#define VFUNCTION(zName, nArg, iArg, bNC, xFunc, type) \
  {nArg, (bNC*SQL_FUNC_NEEDCOLL), \
   SQL_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0}, type, false }
#define DFUNCTION(zName, nArg, iArg, bNC, xFunc, type) \
  {nArg, SQL_FUNC_SLOCHNG|(bNC*SQL_FUNC_NEEDCOLL), \
   SQL_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0}, type, false }
#define FUNCTION2(zName, nArg, iArg, bNC, xFunc, extraFlags, type) \
  {nArg,SQL_FUNC_CONSTANT|(bNC*SQL_FUNC_NEEDCOLL)|extraFlags,\
   SQL_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0}, type, false }
#define STR_FUNCTION(zName, nArg, pArg, bNC, xFunc) \
  {nArg, SQL_FUNC_SLOCHNG|(bNC*SQL_FUNC_NEEDCOLL), \
   pArg, 0, xFunc, 0, #zName, {SQL_AFF_STRING, {0}}, false}
#define LIKEFUNC(zName, nArg, arg, flags, type) \
  {nArg, SQL_FUNC_CONSTANT|flags, \
   (void *)(SQL_INT_TO_PTR(arg)), 0, likeFunc, 0, #zName, {0}, type, false }
#define AGGREGATE(zName, nArg, arg, nc, xStep, xFinal, type) \
  {nArg, (nc*SQL_FUNC_NEEDCOLL), \
   SQL_INT_TO_PTR(arg), 0, xStep,xFinal,#zName, {0}, type, false}
#define AGGREGATE2(zName, nArg, arg, nc, xStep, xFinal, extraFlags, type) \
  {nArg, (nc*SQL_FUNC_NEEDCOLL)|extraFlags, \
   SQL_INT_TO_PTR(arg), 0, xStep,xFinal,#zName, {0}, type, false}

/*
 * All current savepoints are stored in a linked list starting at
 * sql.pSavepoint. The first element in the list is the most recently
 * opened savepoint. Savepoints are added to the list by the vdbe
 * OP_Savepoint instruction.
 */
struct Savepoint {
	box_txn_savepoint_t *tnt_savepoint; /* Tarantool's savepoint struct */
	char *zName;		/* Savepoint name (nul-terminated) */
	Savepoint *pNext;	/* Parent savepoint (if any) */
};

/*
 * The following are used as the second parameter to sqlSavepoint(),
 * and as the P1 argument to the OP_Savepoint instruction.
 */
#define SAVEPOINT_BEGIN      0
#define SAVEPOINT_RELEASE    1
#define SAVEPOINT_ROLLBACK   2

#define sql_type_is_numeric(X)  ((X) == FIELD_TYPE_INTEGER || \
				 (X) == FIELD_TYPE_NUMBER || \
				 (X) == FIELD_TYPE_UNSIGNED)

/*
 * Additional bit values that can be ORed with an type without
 * changing the type.
 *
 * The sql_NOTNULL flag is a combination of NULLEQ and JUMPIFNULL.
 * It causes an assert() to fire if either operand to a comparison
 * operator is NULL.  It is added to certain comparison operators to
 * prove that the operands are always NOT NULL.
 */
#define SQL_JUMPIFNULL   0x10	/* jumps if either operand is NULL */
#define SQL_STOREP2      0x20	/* Store result in reg[P2] rather than jump */
#define SQL_KEEPNULL     0x40	/* Used by vector == or <> */
#define SQL_NULLEQ       0x80	/* NULL=NULL */
#define SQL_NOTNULL      0x90	/* Assert that operands are never NULL */

/**
 * Return logarithm of tuple count in space.
 *
 * @param space Space to be examined.
 * @retval Logarithm of tuple count in space, or default values,
 *         if there is no corresponding space for given table.
 */
LogEst
sql_space_tuple_log_count(struct space *space);

/*
 * Each foreign key constraint is an instance of the following structure.
 *
 * A foreign key is associated with two tables.  The "from" table is
 * the table that contains the REFERENCES clause that creates the foreign
 * key.  The "to" table is the table that is named in the REFERENCES clause.
 * Consider this example:
 *
 *     CREATE TABLE ex1(
 *       a INTEGER PRIMARY KEY,
 *       b INTEGER CONSTRAINT fk1 REFERENCES ex2(x)
 *     );
 *
 * For foreign key "fk1", the from-table is "ex1" and the to-table is "ex2".
 * Equivalent names:
 *
 *     from-table == child-table
 *       to-table == parent-table
 *
 * Each REFERENCES clause generates an instance of the following structure
 * which is attached to the from-table.  The to-table need not exist when
 * the from-table is created.  The existence of the to-table is not checked.
 */

/*
 * This object holds a record which has been parsed out into individual
 * fields, for the purposes of doing a comparison.
 *
 * A record is an object that contains one or more fields of data.
 * Records are used to store the content of a table row and to store
 * the key of an index.  A blob encoding of a record is created by
 * the OP_MakeRecord opcode of the VDBE and is disassembled by the
 * OP_Column opcode.
 *
 * An instance of this object serves as a "key" for doing a search on
 * an index b+tree. The goal of the search is to find the entry that
 * is closed to the key described by this object.  This object might hold
 * just a prefix of the key.  The number of fields is given by
 * key_def->part_count.
 *
 * The r1 and r2 fields are the values to return if this key is less than
 * or greater than a key in the btree, respectively.  These are normally
 * -1 and +1 respectively, but might be inverted to +1 and -1 if the b-tree
 * is in DESC order.
 *
 * The key comparison functions actually return default_rc when they find
 * an equals comparison.  default_rc can be -1, 0, or +1.  If there are
 * multiple entries in the b-tree with the same key (when only looking
 * at the first key_def->part_count) then default_rc can be set to -1 to
 * cause the search to find the last match, or +1 to cause the search to
 * find the first match.
 *
 * The key comparison functions will set eqSeen to true if they ever
 * get and equal results when comparing this structure to a b-tree record.
 * When default_rc!=0, the search might end up on the record immediately
 * before the first match or immediately after the last match.  The
 * eqSeen field will indicate whether or not an exact match exists in the
 * b-tree.
 */
struct UnpackedRecord {
	/** Collation and sort-order information. */
	struct key_def *key_def;
	Mem *aMem;		/* Values */
	u16 nField;		/* Number of entries in apMem[] */
	i8 default_rc;		/* Comparison result if keys are equal */
	u8 errCode;		/* Error detected by xRecordCompare (CORRUPT or NOMEM) */
	i8 r1;			/* Value to return if (lhs > rhs) */
	i8 r2;			/* Value to return if (rhs < lhs) */
	u8 eqSeen;		/* True if an equality comparison has been seen */
	u8 opcode;		/* Currently executing opcode that invoked
				 * movetoUnpacked, used by Tarantool storage layer.
				 */
};

/*
 * Possible SQL index types. Note that PK and UNIQUE constraints
 * are implemented as indexes and have their own types:
 * SQL_INDEX_TYPE_CONSTRAINT_PK and
 * SQL_INDEX_TYPE_CONSTRAINT_UNIQUE.
 */
enum sql_index_type {
    SQL_INDEX_TYPE_NON_UNIQUE = 0,
    SQL_INDEX_TYPE_UNIQUE,
    SQL_INDEX_TYPE_CONSTRAINT_UNIQUE,
    SQL_INDEX_TYPE_CONSTRAINT_PK,
};

/**
 * Fetch statistics concerning tuples to be selected:
 * logarithm of number of tuples which has the same key as for
 * the first key parts specified by second argument.
 * Simple example (without logarithms):
 * idx = {{1,2}, {1,2}, {2, 4}, {2, 3}, {3, 5}}
 * stat[1] = (2 + 2 + 1) / 3 = 2 - two keys which start from 1,
 * two - from 2, and one from 3.
 * stat[2] = (2 + 1 + 1 + 1) / 4 = 1 - only one coincidence of
 * keys: {1, 2}; the rest are different.
 * Notice, that stat[0] is an average number of tuples in a whole
 * index. By default it is DEFAULT_TUPLE_LOG_COUNT == 200.
 * If there is no appropriate Tarantool's index,
 * return one of default values.
 *
 * @param idx Index definition.
 * @param field Number of field to be examined.
 * @retval Estimate logarithm of tuples selected by given field.
 */
log_est_t
index_field_tuple_est(const struct index_def *idx, uint32_t field);

#ifdef DEFAULT_TUPLE_COUNT
#undef DEFAULT_TUPLE_COUNT
#endif
#define DEFAULT_TUPLE_COUNT 1048576
/** [10*log_{2}(1048576)] == 200 */
#define DEFAULT_TUPLE_LOG_COUNT 200

/*
 * Each token coming out of the lexer is an instance of
 * this structure.  Tokens are also used as part of an expression.
 *
 * Note if Token.z==0 then Token.dyn and Token.n are undefined and
 * may contain random values.  Do not make any assumptions about Token.dyn
 * and Token.n when Token.z==0.
 */
struct Token {
	const char *z;		/* Text of the token.  Not NULL-terminated! */
	unsigned int n;		/* Number of characters in this token */
	bool isReserved;         /* If reserved keyword or not */
};

/*
 * An instance of this structure contains information needed to generate
 * code for a SELECT that contains aggregate functions.
 *
 * If Expr.op==TK_AGG_COLUMN or TK_AGG_FUNCTION then Expr.pAggInfo is a
 * pointer to this structure.  The Expr.iColumn field is the index in
 * AggInfo.aCol[] or AggInfo.aFunc[] of information needed to generate
 * code for that node.
 *
 * AggInfo.pGroupBy and AggInfo.aFunc.pExpr point to fields within the
 * original Select structure that describes the SELECT statement.  These
 * fields do not need to be freed when deallocating the AggInfo structure.
 */
struct AggInfo {
	u8 directMode;		/* Direct rendering mode means take data directly
				 * from source tables rather than from accumulators
				 */
	u8 useSortingIdx;	/* In direct mode, reference the sorting index rather
				 * than the source table
				 */
	int sortingIdx;		/* Cursor number of the sorting index */
	int sortingIdxPTab;	/* Cursor number of pseudo-table */
	int nSortingColumn;	/* Number of columns in the sorting index */
	int mnReg, mxReg;	/* Range of registers allocated for aCol and aFunc */
	ExprList *pGroupBy;	/* The group by clause */
	struct AggInfo_col {	/* For each column used in source tables */
		/** Pointer to space definition. */
		struct space_def *space_def;
		int iTable;	/* Cursor number of the source table */
		int iColumn;	/* Column number within the source table */
		int iSorterColumn;	/* Column number in the sorting index */
		int iMem;	/* Memory location that acts as accumulator */
		Expr *pExpr;	/* The original expression */
	} *aCol;
	int nColumn;		/* Number of used entries in aCol[] */
	int nAccumulator;	/* Number of columns that show through to the output.
				 * Additional columns are used only as parameters to
				 * aggregate functions
				 */
	struct AggInfo_func {	/* For each aggregate function */
		Expr *pExpr;	/* Expression encoding the function */
		FuncDef *pFunc;	/* The aggregate function implementation */
		int iMem;	/* Memory location that acts as accumulator */
		int iDistinct;	/* Ephemeral table used to enforce DISTINCT */
		/**
		 * Register, holding ephemeral's space pointer.
		 */
		int reg_eph;
	} *aFunc;
	int nFunc;		/* Number of entries in aFunc[] */
};

typedef int ynVar;

/*
 * Each node of an expression in the parse tree is an instance
 * of this structure.
 *
 * Expr.op is the opcode. The integer parser token codes are reused
 * as opcodes here. For example, the parser defines TK_GE to be an integer
 * code representing the ">=" operator. This same integer code is reused
 * to represent the greater-than-or-equal-to operator in the expression
 * tree.
 *
 * If the expression is an SQL literal (TK_INTEGER, TK_FLOAT, TK_BLOB,
 * or TK_STRING), then Expr.token contains the text of the SQL literal. If
 * the expression is a variable (TK_VARIABLE), then Expr.token contains the
 * variable name. Finally, if the expression is an SQL function (TK_FUNCTION),
 * then Expr.token contains the name of the function.
 *
 * Expr.pRight and Expr.pLeft are the left and right subexpressions of a
 * binary operator. Either or both may be NULL.
 *
 * Expr.x.pList is a list of arguments if the expression is an SQL function,
 * a CASE expression or an IN expression of the form "<lhs> IN (<y>, <z>...)".
 * Expr.x.pSelect is used if the expression is a sub-select or an expression of
 * the form "<lhs> IN (SELECT ...)". If the EP_xIsSelect bit is set in the
 * Expr.flags mask, then Expr.x.pSelect is valid. Otherwise, Expr.x.pList is
 * valid.
 *
 * An expression of the form ID or ID.ID refers to a column in a table.
 * For such expressions, Expr.op is set to TK_COLUMN and Expr.iTable is
 * the integer cursor number of a VDBE cursor pointing to that table and
 * Expr.iColumn is the column number for the specific column.  If the
 * expression is used as a result in an aggregate SELECT, then the
 * value is also stored in the Expr.iAgg column in the aggregate so that
 * it can be accessed after all aggregates are computed.
 *
 * If the expression is an unbound variable marker (a question mark
 * character '?' in the original SQL) then the Expr.iTable holds the index
 * number for that variable.
 *
 * If the expression is a subquery then Expr.iColumn holds an integer
 * register number containing the result of the subquery.  If the
 * subquery gives a constant result, then iTable is -1.  If the subquery
 * gives a different answer at different times during statement processing
 * then iTable is the address of a subroutine that computes the subquery.
 *
 * If the Expr is of type OP_Column, and the table it is selecting from
 * is a disk table or the "old.*" pseudo-table, then space_def points to the
 * corresponding table definition.
 *
 * ALLOCATION NOTES:
 *
 * Expr objects can use a lot of memory space in database schema.  To
 * help reduce memory requirements, sometimes an Expr object will be
 * truncated.  And to reduce the number of memory allocations, sometimes
 * two or more Expr objects will be stored in a single memory allocation,
 * together with Expr.zToken strings.
 *
 * If the EP_Reduced and EP_TokenOnly flags are set when
 * an Expr object is truncated.  When EP_Reduced is set, then all
 * the child Expr objects in the Expr.pLeft and Expr.pRight subtrees
 * are contained within the same memory allocation.  Note, however, that
 * the subtrees in Expr.x.pList or Expr.x.pSelect are always separately
 * allocated, regardless of whether or not EP_Reduced is set.
 */
struct Expr {
	u8 op;			/* Operation performed by this node */
	union {
		/** The type of the column. */
		enum field_type type;
		/** Conflict action for RAISE() function. */
		enum on_conflict_action on_conflict_action;
	};
	u32 flags;		/* Various flags.  EP_* See below */
	union {
		char *zToken;	/* Token value. Zero terminated and dequoted */
		int iValue;	/* Non-negative integer value if EP_IntValue */
	} u;

	/* If the EP_TokenOnly flag is set in the Expr.flags mask, then no
	 * space is allocated for the fields below this point. An attempt to
	 * access them will result in a segfault or malfunction.
	 ********************************************************************/

	Expr *pLeft;		/* Left subnode */
	Expr *pRight;		/* Right subnode */
	union {
		ExprList *pList;	/* op = IN, EXISTS, SELECT, CASE, FUNCTION, BETWEEN */
		Select *pSelect;	/* EP_xIsSelect and op = IN, EXISTS, SELECT */
	} x;

	/* If the EP_Reduced flag is set in the Expr.flags mask, then no
	 * space is allocated for the fields below this point. An attempt to
	 * access them will result in a segfault or malfunction.
	 ********************************************************************/

#if SQL_MAX_EXPR_DEPTH>0
	int nHeight;		/* Height of the tree headed by this node */
#endif
	int iTable;		/* TK_COLUMN: cursor number of table holding column
				 * TK_REGISTER: register number
				 * TK_TRIGGER: 1 -> new, 0 -> old
				 * EP_Unlikely:  134217728 times likelihood
				 * TK_SELECT: 1st register of result vector
				 */
	bool is_ephemeral;      /* If iTable was set, this flags if this table i
				 * ephemeral or not.
				 */
	ynVar iColumn;		/* TK_COLUMN: column index.
				 * TK_VARIABLE: variable number (always >= 1).
				 * TK_SELECT_COLUMN: column of the result vector
				 */
	i16 iAgg;		/* Which entry in pAggInfo->aCol[] or ->aFunc[] */
	i16 iRightJoinTable;	/* If EP_FromJoin, the right table of the join */
	u8 op2;			/* TK_REGISTER: original value of Expr.op
				 * TK_COLUMN: the value of p5 for OP_Column
				 * TK_AGG_FUNCTION: nesting depth
				 */
	AggInfo *pAggInfo;	/* Used by TK_AGG_COLUMN and TK_AGG_FUNCTION */
	/** Pointer for table relative definition. */
	struct space_def *space_def;
};

/*
 * The following are the meanings of bits in the Expr.flags field.
 */
#define EP_FromJoin  0x000001	/* Originates in ON/USING clause of outer join */
#define EP_Agg       0x000002	/* Contains one or more aggregate functions */
#define EP_Resolved  0x000004	/* IDs have been resolved to COLUMNs */
#define EP_Error     0x000008	/* Expression contains one or more errors */
#define EP_Distinct  0x000010	/* Aggregate function with DISTINCT keyword */
#define EP_VarSelect 0x000020	/* pSelect is correlated, not constant */
#define EP_DblQuoted 0x000040	/* token.z was originally in "..." */
#define EP_InfixFunc 0x000080	/* True for an infix function: LIKE, etc */
#define EP_Collate   0x000100	/* Tree contains a TK_COLLATE operator */
#define EP_IntValue  0x000400	/* Integer value contained in u.iValue */
#define EP_xIsSelect 0x000800	/* x.pSelect is valid (otherwise x.pList is) */
#define EP_Skip      0x001000	/* COLLATE, AS, or UNLIKELY */
#define EP_Reduced   0x002000	/* Expr struct EXPR_REDUCEDSIZE bytes only */
#define EP_TokenOnly 0x004000	/* Expr struct EXPR_TOKENONLYSIZE bytes only */
#define EP_Static    0x008000	/* Held in memory not obtained from malloc() */
#define EP_MemToken  0x010000	/* Need to sqlDbFree() Expr.zToken */
#define EP_NoReduce  0x020000	/* Cannot EXPRDUP_REDUCE this Expr */
#define EP_Unlikely  0x040000	/* unlikely() or likelihood() function */
#define EP_ConstFunc 0x080000	/* A sql_FUNC_CONSTANT or _SLOCHNG function */
#define EP_CanBeNull 0x100000	/* Can be null despite NOT NULL constraint */
#define EP_Subquery  0x200000	/* Tree contains a TK_SELECT operator */
#define EP_Alias     0x400000	/* Is an alias for a result set column */
#define EP_Leaf      0x800000	/* Expr.pLeft, .pRight, .u.pSelect all NULL */
/** Expression is system-defined. */
#define EP_System    0x1000000

/*
 * Combinations of two or more EP_* flags
 */
#define EP_Propagate (EP_Collate|EP_Subquery)	/* Propagate these bits up tree */

/*
 * These macros can be used to test, set, or clear bits in the
 * Expr.flags field.
 */
#define ExprHasProperty(E,P)     (((E)->flags&(P))!=0)
#define ExprHasAllProperty(E,P)  (((E)->flags&(P))==(P))
#define ExprSetProperty(E,P)     (E)->flags|=(P)
#define ExprClearProperty(E,P)   (E)->flags&=~(P)

/* The ExprSetVVAProperty() macro is used for Verification, Validation,
 * and Accreditation only.  It works like ExprSetProperty() during VVA
 * processes but is a no-op for delivery.
 */
#ifdef SQL_DEBUG
#define ExprSetVVAProperty(E,P)  (E)->flags|=(P)
#else
#define ExprSetVVAProperty(E,P)
#endif

/*
 * Macros to determine the number of bytes required by a normal Expr
 * struct, an Expr struct with the EP_Reduced flag set in Expr.flags
 * and an Expr struct with the EP_TokenOnly flag set.
 */
#define EXPR_FULLSIZE           sizeof(Expr)	/* Full size */
#define EXPR_REDUCEDSIZE        offsetof(Expr,iTable)	/* Common features */
#define EXPR_TOKENONLYSIZE      offsetof(Expr,pLeft)	/* Fewer features */

/*
 * Flags passed to the sqlExprDup() function. See the header comment
 * above sqlExprDup() for details.
 */
#define EXPRDUP_REDUCE         0x0001	/* Used reduced-size Expr nodes */

/*
 * A list of expressions.  Each expression may optionally have a
 * name.  An expr/name combination can be used in several ways, such
 * as the list of "expr AS ID" fields following a "SELECT" or in the
 * list of "ID = expr" items in an UPDATE.  A list of expressions can
 * also be used as the argument to a function, in which case the a.zName
 * field is not used.
 *
 * By default the Expr.zSpan field holds a human-readable description of
 * the expression that is used in the generation of error messages and
 * column labels.  In this case, Expr.zSpan is typically the text of a
 * column expression as it exists in a SELECT statement.  However, if
 * the bSpanIsTab flag is set, then zSpan is overloaded to mean the name
 * of the result column in the form: DATABASE.TABLE.COLUMN.  This later
 * form is used for name resolution with nested FROM clauses.
 */
struct ExprList {
	int nExpr;		/* Number of expressions on the list */
	struct ExprList_item {	/* For each expression in the list */
		Expr *pExpr;	/* The list of expressions */
		char *zName;	/* Token associated with this expression */
		char *zSpan;	/* Original text of the expression */
		enum sort_order sort_order;
		unsigned done:1;	/* A flag to indicate when processing is finished */
		unsigned bSpanIsTab:1;	/* zSpan holds DB.TABLE.COLUMN */
		unsigned reusable:1;	/* Constant expression is reusable */
		union {
			struct {
				u16 iOrderByCol;	/* For ORDER BY, column number in result set */
				u16 iAlias;	/* Index into Parse.aAlias[] for zName */
			} x;
			int iConstExprReg;	/* Register in which Expr value is cached */
		} u;
	} *a;			/* Alloc a power of two greater or equal to nExpr */
};

/*
 * An instance of this structure is used by the parser to record both
 * the parse tree for an expression and the span of input text for an
 * expression.
 */
struct ExprSpan {
	Expr *pExpr;		/* The expression parse tree */
	const char *zStart;	/* First character of input text */
	const char *zEnd;	/* One character past the end of input text */
};

/*
 * An instance of this structure can hold a simple list of identifiers,
 * such as the list "a,b,c" in the following statements:
 *
 *      INSERT INTO t(a,b,c) VALUES ...;
 *      CREATE INDEX idx ON t(a,b,c);
 *      CREATE TRIGGER trig BEFORE UPDATE ON t(a,b,c) ...;
 *
 * The IdList.a.idx field is used when the IdList represents the list of
 * column names after a table name in an INSERT statement.  In the statement
 *
 *     INSERT INTO t(a,b,c) ...
 *
 * If "a" is the k-th column of table "t", then IdList.a[0].idx==k.
 */
struct IdList {
	struct IdList_item {
		char *zName;	/* Name of the identifier */
		int idx;	/* Index in some Table.aCol[] of a column named zName */
	} *a;
	int nId;		/* Number of identifiers on the list */
};

/*
 * The bitmask datatype defined below is used for various optimizations.
 *
 * Changing this from a 64-bit to a 32-bit type limits the number of
 * tables in a join to 32 instead of 64.  But it also reduces the size
 * of the library by 738 bytes on ix86.
 */
#ifdef SQL_BITMASK_TYPE
typedef SQL_BITMASK_TYPE Bitmask;
#else
typedef u64 Bitmask;
#endif

/*
 * The number of bits in a Bitmask.  "BMS" means "BitMask Size".
 */
#define BMS  ((int)(sizeof(Bitmask)*8))

/*
 * A bit in a Bitmask
 */
#define MASKBIT(n)   (((Bitmask)1)<<(n))
#define MASKBIT32(n) (((unsigned int)1)<<(n))
#define ALLBITS      ((Bitmask)-1)

/*
 * The following structure describes the FROM clause of a SELECT statement.
 * Each table or subquery in the FROM clause is a separate element of
 * the SrcList.a[] array.
 *
 * With the addition of multiple database support, the following structure
 * can also be used to describe a particular table such as the table that
 * is modified by an INSERT, DELETE, or UPDATE statement.  In standard SQL,
 * such a table must be a simple name: ID.  But in sql, the table can
 * now be identified by a database name, a dot, then the table name: ID.ID.
 *
 * The jointype starts out showing the join type between the current table
 * and the next table on the list.  The parser builds the list this way.
 * But sqlSrcListShiftJoinType() later shifts the jointypes so that each
 * jointype expresses the join between the table and the previous table.
 *
 * In the colUsed field, the high-order bit (bit 63) is set if the table
 * contains more than 63 columns and the 64-th or later column is used.
 */
struct SrcList {
	int nSrc;		/* Number of tables or subqueries in the FROM clause */
	u32 nAlloc;		/* Number of entries allocated in a[] below */
	struct SrcList_item {
		char *zName;	/* Name of the table */
		char *zAlias;	/* The "B" part of a "A AS B" phrase.  zName is the "A" */
		/** A space corresponding to zName */
		struct space *space;
		Select *pSelect;	/* A SELECT statement used in place of a table name */
		int addrFillSub;	/* Address of subroutine to manifest a subquery */
		int regReturn;	/* Register holding return address of addrFillSub */
		int regResult;	/* Registers holding results of a co-routine */
		struct {
			u8 jointype;	/* Type of join between this table and the previous */
			unsigned notIndexed:1;	/* True if there is a NOT INDEXED clause */
			unsigned isIndexedBy:1;	/* True if there is an INDEXED BY clause */
			unsigned isTabFunc:1;	/* True if table-valued-function syntax */
			unsigned isCorrelated:1;	/* True if sub-query is correlated */
			unsigned viaCoroutine:1;	/* Implemented as a co-routine */
			unsigned isRecursive:1;	/* True for recursive reference in WITH */
		} fg;
		u8 iSelectId;	/* If pSelect!=0, the id of the sub-select in EQP */
		int iCursor;	/* The VDBE cursor number used to access this table */
		Expr *pOn;	/* The ON clause of a join */
		IdList *pUsing;	/* The USING clause of a join */
		Bitmask colUsed;	/* Bit N (1<<N) set if column N of space is used */
		union {
			char *zIndexedBy;	/* Identifier from "INDEXED BY <zIndex>" clause */
			ExprList *pFuncArg;	/* Arguments to table-valued-function */
		} u1;
		struct index_def *pIBIndex;
	} a[1];			/* One entry for each identifier on the list */
};

/*
 * Permitted values of the SrcList.a.jointype field
 */
#define JT_INNER     0x0001	/* Any kind of inner or cross join */
#define JT_CROSS     0x0002	/* Explicit use of the CROSS keyword */
#define JT_NATURAL   0x0004	/* True for a "natural" join */
#define JT_LEFT      0x0008	/* Left outer join */
#define JT_RIGHT     0x0010	/* Right outer join */
#define JT_OUTER     0x0020	/* The "OUTER" keyword is present */
#define JT_ERROR     0x0040	/* unknown or unsupported join type */

/*
 * Flags appropriate for the wctrlFlags parameter of sqlWhereBegin()
 * and the WhereInfo.wctrlFlags member.
 *
 * Value constraints (enforced via assert()):
 *     WHERE_USE_LIMIT  == SF_FixedLimit
 */
#define WHERE_ORDERBY_NORMAL   0x0000	/* No-op */
#define WHERE_ORDERBY_MIN      0x0001	/* ORDER BY processing for min() func */
#define WHERE_ORDERBY_MAX      0x0002	/* ORDER BY processing for max() func */
#define WHERE_ONEPASS_DESIRED  0x0004	/* Want to do one-pass UPDATE/DELETE */
#define WHERE_ONEPASS_MULTIROW 0x0008	/* ONEPASS is ok with multiple rows */
#define WHERE_DUPLICATES_OK    0x0010	/* Ok to return a row more than once */
#define WHERE_OR_SUBCLAUSE     0x0020	/* Processing a sub-WHERE as part of
					 * the OR optimization
					 */
#define WHERE_GROUPBY          0x0040	/* pOrderBy is really a GROUP BY */
#define WHERE_DISTINCTBY       0x0080	/* pOrderby is really a DISTINCT clause */
#define WHERE_WANT_DISTINCT    0x0100	/* All output needs to be distinct */
#define WHERE_SORTBYGROUP      0x0200	/* Support sqlWhereIsSorted() */
#define WHERE_SEEK_TABLE       0x0400	/* Do not defer seeks on main table */
#define WHERE_ORDERBY_LIMIT    0x0800	/* ORDERBY+LIMIT on the inner loop */
			/*     0x1000    not currently used */
			/*     0x2000    not currently used */
#define WHERE_USE_LIMIT        0x4000	/* Use the LIMIT in cost estimates */
			/*     0x8000    not currently used */

/* Allowed return values from sqlWhereIsDistinct()
 */
#define WHERE_DISTINCT_NOOP      0	/* DISTINCT keyword not used */
#define WHERE_DISTINCT_UNIQUE    1	/* No duplicates */
#define WHERE_DISTINCT_ORDERED   2	/* All duplicates are adjacent */
#define WHERE_DISTINCT_UNORDERED 3	/* Duplicates are scattered */

/*
 * A NameContext defines a context in which to resolve table and column
 * names.  The context consists of a list of tables (the pSrcList) field and
 * a list of named expression (pEList).  The named expression list may
 * be NULL.  The pSrc corresponds to the FROM clause of a SELECT or
 * to the table being operated on by INSERT, UPDATE, or DELETE.  The
 * pEList corresponds to the result set of a SELECT and is NULL for
 * other statements.
 *
 * NameContexts can be nested.  When resolving names, the inner-most
 * context is searched first.  If no match is found, the next outer
 * context is checked.  If there is still no match, the next context
 * is checked.  This process continues until either a match is found
 * or all contexts are check.  When a match is found, the nRef member of
 * the context containing the match is incremented.
 *
 * Each subquery gets a new NameContext.  The pNext field points to the
 * NameContext in the parent query.  Thus the process of scanning the
 * NameContext list corresponds to searching through successively outer
 * subqueries looking for a match.
 */
struct NameContext {
	Parse *pParse;		/* The parser */
	SrcList *pSrcList;	/* One or more tables used to resolve names */
	ExprList *pEList;	/* Optional list of result-set columns */
	AggInfo *pAggInfo;	/* Information about aggregates at this level */
	NameContext *pNext;	/* Next outer name context.  NULL for outermost */
	int nRef;		/* Number of names resolved by this context */
	int nErr;		/* Number of errors encountered while resolving names */
	u16 ncFlags;		/* Zero or more NC_* flags defined below */
};

/*
 * Allowed values for the NameContext, ncFlags field.
 *
 * Value constraints (all checked via assert()):
 *    NC_HasAgg    == SF_HasAgg
 *    NC_MinMaxAgg == SF_MinMaxAgg == sql_FUNC_MINMAX
 *
 */
#define NC_AllowAgg  0x0001	/* Aggregate functions are allowed here */
#define NC_IsCheck   0x0004	/* True if resolving names in a CHECK constraint */
#define NC_InAggFunc 0x0008	/* True if analyzing arguments to an agg func */
#define NC_HasAgg    0x0010	/* One or more aggregate functions seen */
#define NC_IdxExpr   0x0020	/* True if resolving columns of CREATE INDEX */
#define NC_VarSelect 0x0040	/* A correlated subquery has been seen */
#define NC_MinMaxAgg 0x1000	/* min/max aggregates seen.  See note above */
/** One or more identifiers are out of aggregate function. */
#define NC_HasUnaggregatedId     0x2000
/*
 * An instance of the following structure contains all information
 * needed to generate code for a single SELECT statement.
 *
 * nLimit is set to -1 if there is no LIMIT clause.  nOffset is set to 0.
 * If there is a LIMIT clause, the parser sets nLimit to the value of the
 * limit and nOffset to the value of the offset (or 0 if there is not
 * offset).  But later on, nLimit and nOffset become the memory locations
 * in the VDBE that record the limit and offset counters.
 *
 * addrOpenEphm[] entries contain the address of OP_OpenEphemeral opcodes.
 * These addresses must be stored so that we can go back and fill in
 * the P4_KEYINFO and P2 parameters later.  Neither the key_info nor
 * the number of columns in P2 can be computed at the same time
 * as the OP_OpenEphm instruction is coded because not
 * enough information about the compound query is known at that point.
 * The key_info for addrOpenTran[0] and [1] contains collating sequences
 * for the result set. The key_info for addrOpenEphm[2] contains collating
 * sequences for the ORDER BY clause.
 */
struct Select {
	ExprList *pEList;	/* The fields of the result */
	u8 op;			/* One of: TK_UNION TK_ALL TK_INTERSECT TK_EXCEPT */
	LogEst nSelectRow;	/* Estimated number of result rows */
	u32 selFlags;		/* Various SF_* values */
	int iLimit, iOffset;	/* Memory registers holding LIMIT & OFFSET counters */
	char zSelName[12];	/* Symbolic name of this SELECT use for debugging */
	int addrOpenEphm[2];	/* OP_OpenEphem opcodes related to this select */
	SrcList *pSrc;		/* The FROM clause */
	Expr *pWhere;		/* The WHERE clause */
	ExprList *pGroupBy;	/* The GROUP BY clause */
	Expr *pHaving;		/* The HAVING clause */
	ExprList *pOrderBy;	/* The ORDER BY clause */
	Select *pPrior;		/* Prior select in a compound select statement */
	Select *pNext;		/* Next select to the left in a compound */
	Expr *pLimit;		/* LIMIT expression. NULL means not used. */
	Expr *pOffset;		/* OFFSET expression. NULL means not used. */
	With *pWith;		/* WITH clause attached to this select. Or NULL. */
};

/*
 * Allowed values for Select.selFlags.  The "SF" prefix stands for
 * "Select Flag".
 *
 * Value constraints (all checked via assert())
 *     SF_HasAgg     == NC_HasAgg
 *     SF_MinMaxAgg  == NC_MinMaxAgg     == sql_FUNC_MINMAX
 *     SF_FixedLimit == WHERE_USE_LIMIT
 */
#define SF_Distinct       0x00001	/* Output should be DISTINCT */
#define SF_All            0x00002	/* Includes the ALL keyword */
#define SF_Resolved       0x00004	/* Identifiers have been resolved */
#define SF_Aggregate      0x00008	/* Contains agg functions or a GROUP BY */
#define SF_HasAgg         0x00010	/* Contains aggregate functions */
#define SF_UsesEphemeral  0x00020	/* Uses the OpenEphemeral opcode */
#define SF_Expanded       0x00040	/* sqlSelectExpand() called on this */
#define SF_HasTypeInfo    0x00080	/* FROM subqueries have Table metadata */
#define SF_Compound       0x00100	/* Part of a compound query */
#define SF_Values         0x00200	/* Synthesized from VALUES clause */
#define SF_MultiValue     0x00400	/* Single VALUES term with multiple rows */
#define SF_NestedFrom     0x00800	/* Part of a parenthesized FROM clause */
#define SF_MinMaxAgg      0x01000	/* Aggregate containing min() or max() */
#define SF_Recursive      0x02000	/* The recursive part of a recursive CTE */
#define SF_FixedLimit     0x04000	/* nSelectRow set by a constant LIMIT */
#define SF_MaybeConvert   0x08000	/* Need convertCompoundSelectToSubquery() */
#define SF_Converted      0x10000	/* By convertCompoundSelectToSubquery() */
/** Abort subquery if its output contains more than one row. */
#define SF_SingleRow      0x20000

/*
 * The results of a SELECT can be distributed in several ways, as defined
 * by one of the following macros.  The "SRT" prefix means "SELECT Result
 * Type".
 *
 *     SRT_Union       Store results as a key in a temporary index
 *                     identified by pDest->iSDParm.
 *
 *     SRT_Except      Remove results from the temporary index pDest->iSDParm.
 *
 *     SRT_Exists      Store a 1 in memory cell pDest->iSDParm if the result
 *                     set is not empty.
 *
 *     SRT_Discard     Throw the results away.  This is used by SELECT
 *                     statements within triggers whose only purpose is
 *                     the side-effects of functions.
 *
 * All of the above are free to ignore their ORDER BY clause. Those that
 * follow must honor the ORDER BY clause.
 *
 *     SRT_Output      Generate a row of output (using the OP_ResultRow
 *                     opcode) for each row in the result set.
 *
 *     SRT_Mem         Only valid if the result is a single column.
 *                     Store the first column of the first result row
 *                     in register pDest->iSDParm then abandon the rest
 *                     of the query.  This destination implies "LIMIT 1".
 *
 *     SRT_Set         The result must be a single column.  Store each
 *                     row of result as the key in table pDest->iSDParm.
 *                     Apply the type pDest->det_type before storing
 *                     results.  Used to implement "IN (SELECT ...)".
 *
 *     SRT_EphemTab    Create an temporary table pDest->iSDParm and store
 *                     the result there. The cursor is left open after
 *                     returning.  This is like SRT_Table except that
 *                     this destination uses OP_OpenEphemeral to create
 *                     the table first.
 *
 *     SRT_Coroutine   Generate a co-routine that returns a new row of
 *                     results each time it is invoked.  The entry point
 *                     of the co-routine is stored in register pDest->iSDParm
 *                     and the result row is stored in pDest->nDest registers
 *                     starting with pDest->iSdst.
 *
 *     SRT_Table       Store results in temporary table pDest->iSDParm.
 *     SRT_Fifo        This is like SRT_EphemTab except that the table
 *                     is assumed to already be open.  SRT_Fifo has
 *                     the additional property of being able to ignore
 *                     the ORDER BY clause.
 *
 *     SRT_DistFifo    Store results in a temporary table pDest->iSDParm.
 *                     But also use temporary table pDest->iSDParm+1 as
 *                     a record of all prior results and ignore any duplicate
 *                     rows.  Name means:  "Distinct Fifo".
 *
 *     SRT_Queue       Store results in priority queue pDest->iSDParm (really
 *                     an index).  Append a sequence number so that all entries
 *                     are distinct.
 *
 *     SRT_DistQueue   Store results in priority queue pDest->iSDParm only if
 *                     the same record has never been stored before.  The
 *                     index at pDest->iSDParm+1 hold all prior stores.
 */
#define SRT_Union        1	/* Store result as keys in an index */
#define SRT_Except       2	/* Remove result from a UNION index */
#define SRT_Exists       3	/* Store 1 if the result is not empty */
#define SRT_Discard      4	/* Do not save the results anywhere */
#define SRT_Fifo         5	/* Store result as data with an automatic rowid */
#define SRT_DistFifo     6	/* Like SRT_Fifo, but unique results only */
#define SRT_Queue        7	/* Store result in an queue */
#define SRT_DistQueue    8	/* Like SRT_Queue, but unique results only */

/* The ORDER BY clause is ignored for all of the above */
#define IgnorableOrderby(X) ((X->eDest)<=SRT_DistQueue)

#define SRT_Output       9	/* Output each row of result */
#define SRT_Mem         10	/* Store result in a memory cell */
#define SRT_Set         11	/* Store results as keys in an index */
#define SRT_EphemTab    12	/* Create transient tab and store like SRT_Table */
#define SRT_Coroutine   13	/* Generate a single row of result */
#define SRT_Table       14	/* Store result as data with an automatic rowid */

/*
 * An instance of this object describes where to put of the results of
 * a SELECT statement.
 */
struct SelectDest {
	u8 eDest;		/* How to dispose of the results.  On of SRT_* above. */
	/** Type used when eDest==SRT_Set */
	enum field_type *dest_type;
	int iSDParm;		/* A parameter used by the eDest disposal method */
	/** Register containing ephemeral's space pointer. */
	int reg_eph;
	int iSdst;		/* Base register where results are written */
	int nSdst;		/* Number of registers allocated */
	ExprList *pOrderBy;	/* Key columns for SRT_Queue and SRT_DistQueue */
};

/*
 * Size of the column cache
 */
#ifndef SQL_N_COLCACHE
#define SQL_N_COLCACHE 10
#endif

/*
 * At least one instance of the following structure is created for each
 * trigger that may be fired while parsing an INSERT, UPDATE or DELETE
 * statement. All such objects are stored in the linked list headed at
 * Parse.pTriggerPrg and deleted once statement compilation has been
 * completed.
 *
 * A Vdbe sub-program that implements the body and WHEN clause of trigger
 * TriggerPrg.pTrigger, assuming a default ON CONFLICT clause of
 * TriggerPrg.orconf, is stored in the TriggerPrg.pProgram variable.
 * The Parse.pTriggerPrg list never contains two entries with the same
 * values for both pTrigger and orconf.
 *
 * The TriggerPrg.aColmask[0] variable is set to a mask of old.* columns
 * accessed (or set to 0 for triggers fired as a result of INSERT
 * statements). Similarly, the TriggerPrg.aColmask[1] variable is set to
 * a mask of new.* columns used by the program.
 */
struct TriggerPrg {
	/** Trigger this program was coded from. */
	struct sql_trigger *trigger;
	TriggerPrg *pNext;	/* Next entry in Parse.pTriggerPrg list */
	SubProgram *pProgram;	/* Program implementing pTrigger/orconf */
	int orconf;		/* Default ON CONFLICT policy */
	u32 aColmask[2];	/* Masks of old.*, new.* columns accessed */
};

enum ast_type {
	AST_TYPE_UNDEFINED = 0,
	AST_TYPE_SELECT,
	AST_TYPE_EXPR,
	AST_TYPE_TRIGGER,
	ast_type_MAX
};

/**
 * Structure representing foreign keys constraints appeared
 * within CREATE TABLE statement. Used only during parsing.
 */
struct fk_constraint_parse {
	/**
	 * Foreign keys constraint declared in <CREATE TABLE ...>
	 * statement. They must be coded after space creation.
	 */
	struct fk_constraint_def *fk_def;
	/**
	 * If inside CREATE TABLE statement we want to declare
	 * self-referenced FK constraint, we must delay their
	 * resolution until the end of parsing of all columns.
	 * E.g.: CREATE TABLE t1(id REFERENCES t1(b), b);
	 */
	struct ExprList *selfref_cols;
	/**
	 * Still, self-referenced columns might be NULL, if
	 * we declare FK constraints referencing PK:
	 * CREATE TABLE t1(id REFERENCES t1) - it is a valid case.
	 */
	bool is_self_referenced;
	/** Organize these structs into linked list. */
	struct rlist link;
};

/*
 * An SQL parser context.  A copy of this structure is passed through
 * the parser and down into all the parser action routine in order to
 * carry around information that is global to the entire parse.
 *
 * The structure is divided into two parts.  When the parser and code
 * generate call themselves recursively, the first part of the structure
 * is constant but the second part is reset at the beginning and end of
 * each recursion.
 */
struct Parse {
	sql *db;		/* The main database structure */
	char *zErrMsg;		/* An error message */
	Vdbe *pVdbe;		/* An engine for executing database bytecode */
	int rc;			/* Return code from execution */
	u8 colNamesSet;		/* TRUE after OP_ColumnName has been issued to pVdbe */
	u8 nTempReg;		/* Number of temporary registers in aTempReg[] */
	u8 isMultiWrite;	/* True if statement may modify/insert multiple rows */
	u8 mayAbort;		/* True if statement may throw an ABORT exception */
	u8 hasCompound;		/* Need to invoke convertCompoundSelectToSubquery() */
	u8 okConstFactor;	/* OK to factor out constants */
	u8 disableLookaside;	/* Number of times lookaside has been disabled */
	u8 nColCache;		/* Number of entries in aColCache[] */
	int nRangeReg;		/* Size of the temporary register block */
	int iRangeReg;		/* First register in temporary register block */
	int nErr;		/* Number of errors seen */
	int nTab;		/* Number of previously allocated VDBE cursors */
	int nMem;		/* Number of memory cells used so far */
	int nOpAlloc;		/* Number of slots allocated for Vdbe.aOp[] */
	int szOpAlloc;		/* Bytes of memory space allocated for Vdbe.aOp[] */
	int ckBase;		/* Base register of data during check constraints */
	int iSelfTab;		/* Table of an index whose exprs are being coded */
	int iCacheLevel;	/* ColCache valid when aColCache[].iLevel<=iCacheLevel */
	int iCacheCnt;		/* Counter used to generate aColCache[].lru values */
	int nLabel;		/* Number of labels used */
	int *aLabel;		/* Space to hold the labels */
	ExprList *pConstExpr;	/* Constant expressions */
	Token constraintName;	/* Name of the constraint currently being parsed */
	int nMaxArg;		/* Max args passed to user function by sub-program */
	int nSelect;		/* Number of SELECT statements seen */
	int nSelectIndent;	/* How far to indent SELECTTRACE() output */
	Parse *pToplevel;	/* Parse structure for main program (or NULL) */
	u32 nQueryLoop;		/* Est number of iterations of a query (10*log2(N)) */
	u32 oldmask;		/* Mask of old.* columns referenced */
	u32 newmask;		/* Mask of new.* columns referenced */
	u8 eTriggerOp;		/* TK_UPDATE, TK_INSERT or TK_DELETE */
	u8 eOrconf;		/* Default ON CONFLICT policy for trigger steps */
	/** Region to make SQL temp allocations. */
	struct region region;

  /**************************************************************************
  * Fields above must be initialized to zero.  The fields that follow,
  * down to the beginning of the recursive section, do not need to be
  * initialized as they will be set before being used.  The boundary is
  * determined by offsetof(Parse,aColCache).
  *************************************************************************/

	struct yColCache {
		int iTable;	/* Table cursor number */
		i16 iColumn;	/* Table column number */
		u8 tempReg;	/* iReg is a temp register that needs to be freed */
		int iLevel;	/* Nesting level */
		int iReg;	/* Reg with value of this column. 0 means none. */
		int lru;	/* Least recently used entry has the smallest value */
	} aColCache[SQL_N_COLCACHE];	/* One for each column cache entry */
	int aTempReg[8];	/* Holding area for temporary registers */
	Token sNameToken;	/* Token with unqualified schema object name */

  /************************************************************************
  * Above is constant between recursions.  Below is reset before and after
  * each recursion.  The boundary between these two regions is determined
  * using offsetof(Parse,sLastToken) so the sLastToken field must be the
  * first field in the recursive region.
  ***********************************************************************/

	Token sLastToken;	/* The last token parsed */
	ynVar nVar;		/* Number of '?' variables seen in the SQL so far */
	u8 explain;		/* True if the EXPLAIN flag is found on the query */
	int nHeight;		/* Expression tree height of current sub-select */
	int iSelectId;		/* ID of current select for EXPLAIN output */
	int iNextSelectId;	/* Next available select ID for EXPLAIN output */
	VList *pVList;		/* Mapping between variable names and numbers */
	Vdbe *pReprepare;	/* VM being reprepared (sqlReprepare()) */
	const char *zTail;	/* All SQL text past the last semicolon parsed */
	TriggerPrg *pTriggerPrg;	/* Linked list of coded triggers */
	With *pWith;		/* Current WITH clause, or NULL */
	With *pWithToFree;	/* Free this WITH object at the end of the parse */
	/** Space triggers are being coded for. */
	struct space *triggered_space;
	/** A space being constructed by CREATE TABLE */
	struct space *new_space;
	/**
	 * Number of FK constraints declared within
	 * CREATE TABLE statement.
	 */
	uint32_t fk_constraint_count;
	/**
	 * Foreign key constraint appeared in CREATE TABLE stmt.
	 */
	struct rlist new_fk_constraint;
	/**
	 * List of all records that were inserted in system spaces
	 * in current statement.
	 */
	struct rlist record_list;
	bool initiateTTrans;	/* Initiate Tarantool transaction */
	/** True, if table to be created has AUTOINCREMENT PK. */
	bool is_new_table_autoinc;
	/** If set - do not emit byte code at all, just parse.  */
	bool parse_only;
	/** Type of parsed_ast member. */
	enum ast_type parsed_ast_type;
	/**
	 * Members of this union are valid only
	 * if parse_only is set to true.
	 */
	union {
		struct Expr *expr;
		struct Select *select;
		struct sql_trigger *trigger;
	} parsed_ast;
};

/*
 * Bitfield flags for P5 value in various opcodes.
 *
 * Value constraints (enforced via assert()):
 *    OPFLAG_LENGTHARG    == sql_FUNC_LENGTH
 *    OPFLAG_TYPEOFARG    == sql_FUNC_TYPEOF
 *    OPFLAG_FORDELETE    == BTREE_FORDELETE
 *    OPFLAG_SAVEPOSITION == BTREE_SAVEPOSITION
 *    OPFLAG_AUXDELETE    == BTREE_AUXDELETE
 */
#define OPFLAG_NCHANGE       0x01	/* OP_Insert: Set to update db->nChange */
				     /* Also used in P2 (not P5) of OP_Delete */
#define OPFLAG_EPHEM         0x01	/* OP_Column: Ephemeral output is ok */
#define OPFLAG_OE_IGNORE    0x200	/* OP_IdxInsert: Ignore flag */
#define OPFLAG_OE_FAIL      0x400	/* OP_IdxInsert: Fail flag */
#define OPFLAG_OE_ROLLBACK  0x800	/* OP_IdxInsert: Rollback flag. */
#define OPFLAG_LENGTHARG     0x40	/* OP_Column only used for length() */
#define OPFLAG_TYPEOFARG     0x80	/* OP_Column only used for typeof() */
#define OPFLAG_SEEKEQ        0x02	/* OP_Open** cursor uses EQ seek only */
#define OPFLAG_FORDELETE     0x08	/* OP_Open should use BTREE_FORDELETE */
#define OPFLAG_P2ISREG       0x10	/* P2 to OP_Open** is a register number */
#define OPFLAG_PERMUTE       0x01	/* OP_Compare: use the permutation */
#define OPFLAG_SAVEPOSITION  0x02	/* OP_Delete: keep cursor position */
#define OPFLAG_AUXDELETE     0x04	/* OP_Delete: index in a DELETE op */

#define OPFLAG_SAME_FRAME    0x01	/* OP_FCopy: use same frame for source
					 * register
					 */
#define OPFLAG_NOOP_IF_NULL  0x02	/* OP_FCopy: if source register is NULL
					 * then do nothing
					 */
#define OPFLAG_SYSTEMSP      0x20	/* OP_Open**: set if space pointer
					 * points to system space.
					 */

/**
 * Prepare vdbe P5 flags for OP_{IdxInsert, IdxReplace, Update}
 * by on_conflict action.
 */
#define SET_CONFLICT_FLAG(opflag, on_conflict) do { \
	if (on_conflict == ON_CONFLICT_ACTION_IGNORE) \
	    opflag |= OPFLAG_OE_IGNORE; \
	else if (on_conflict == ON_CONFLICT_ACTION_FAIL) \
	    opflag |= OPFLAG_OE_FAIL; \
	else if (on_conflict == ON_CONFLICT_ACTION_ROLLBACK) \
	    opflag |= OPFLAG_OE_ROLLBACK; \
} while (0)

/* OP_RowData: xferOptimization started processing */
#ifdef SQL_TEST
#define OPFLAG_XFER_OPT      0x01
#endif

/*
 * Each trigger present in the database schema is stored as an
 * instance of struct sql_trigger.
 * Pointers to instances of struct sql_trigger are stored in a
 * linked list, using the next member of struct sql_trigger. A
 * pointer to the first element of the linked list is stored as
 * sql_triggers member of the associated space.
 *
 * The "step_list" member points to the first element of a linked
 * list containing the SQL statements specified as the trigger
 * program.
 */
struct sql_trigger {
	/** The name of the trigger. */
	char *zName;
	/** The ID of space the trigger refers to. */
	uint32_t space_id;
	/** One of TK_DELETE, TK_UPDATE, TK_INSERT. */
	u8 op;
	/** One of TRIGGER_BEFORE, TRIGGER_AFTER. */
	u8 tr_tm;
	/** The WHEN clause of the expression (may be NULL). */
	Expr *pWhen;
	/**
	 * If this is an UPDATE OF <column-list> trigger,
	 * the <column-list> is stored here
	 */
	IdList *pColumns;
	/** Link list of trigger program steps. */
	TriggerStep *step_list;
	/** Next trigger associated with the table. */
	struct sql_trigger *next;
};

/*
 * A trigger is either a BEFORE or an AFTER trigger.  The following constants
 * determine which.
 *
 * If there are multiple triggers, you might of some BEFORE and some AFTER.
 * In that cases, the constants below can be ORed together.
 */
#define TRIGGER_BEFORE  1
#define TRIGGER_AFTER   2

/*
 * An instance of struct TriggerStep is used to store a single SQL statement
 * that is a part of a trigger-program.
 *
 * Instances of struct TriggerStep are stored in a singly linked list (linked
 * using the "pNext" member) referenced by the "step_list" member of the
 * associated struct sql_trigger instance. The first element of the linked list
 * is the first step of the trigger-program.
 *
 * The "op" member indicates whether this is a "DELETE", "INSERT", "UPDATE" or
 * "SELECT" statement. The meanings of the other members is determined by the
 * value of "op" as follows:
 *
 * (op == TK_INSERT)
 * orconf    -> stores the ON CONFLICT algorithm
 * pSelect   -> If this is an INSERT INTO ... SELECT ... statement, then
 *              this stores a pointer to the SELECT statement. Otherwise NULL.
 * zTarget   -> Dequoted name of the table to insert into.
 * pExprList -> If this is an INSERT INTO ... VALUES ... statement, then
 *              this stores values to be inserted. Otherwise NULL.
 * pIdList   -> If this is an INSERT INTO ... (<column-names>) VALUES ...
 *              statement, then this stores the column-names to be
 *              inserted into.
 *
 * (op == TK_DELETE)
 * zTarget   -> Dequoted name of the table to delete from.
 * pWhere    -> The WHERE clause of the DELETE statement if one is specified.
 *              Otherwise NULL.
 *
 * (op == TK_UPDATE)
 * zTarget   -> Dequoted name of the table to update.
 * pWhere    -> The WHERE clause of the UPDATE statement if one is specified.
 *              Otherwise NULL.
 * pExprList -> A list of the columns to update and the expressions to update
 *              them to. See sqlUpdate() documentation of "pChanges"
 *              argument.
 *
 */
struct TriggerStep {
	u8 op;			/* One of TK_DELETE, TK_UPDATE, TK_INSERT, TK_SELECT */
	u8 orconf;		/* ON_CONFLICT_ACTION_ROLLBACK etc. */
	/** The trigger that this step is a part of */
	struct sql_trigger *trigger;
	Select *pSelect;	/* SELECT statement or RHS of INSERT INTO SELECT ... */
	char *zTarget;		/* Target table for DELETE, UPDATE, INSERT */
	Expr *pWhere;		/* The WHERE clause for DELETE or UPDATE steps */
	ExprList *pExprList;	/* SET clause for UPDATE. */
	IdList *pIdList;	/* Column names for INSERT */
	TriggerStep *pNext;	/* Next in the link-list */
	TriggerStep *pLast;	/* Last element in link-list. Valid for 1st elem only */
};

/*
 * An objected used to accumulate the text of a string where we
 * do not necessarily know how big the string will be in the end.
 */
struct StrAccum {
	sql *db;		/* Optional database for lookaside.  Can be NULL */
	char *zBase;		/* A base allocation.  Not from malloc. */
	char *zText;		/* The string collected so far */
	u32 nChar;		/* Length of the string so far */
	u32 nAlloc;		/* Amount of space allocated in zText */
	u32 mxAlloc;		/* Maximum allowed allocation.  0 for no malloc usage */
	u8 accError;		/* STRACCUM_NOMEM or STRACCUM_TOOBIG */
	u8 printfFlags;		/* sql_PRINTF flags below */
};
#define STRACCUM_NOMEM   1
#define STRACCUM_TOOBIG  2
#define SQL_PRINTF_INTERNAL 0x01	/* Internal-use-only converters allowed */
#define SQL_PRINTF_SQLFUNC  0x02	/* SQL function arguments to VXPrintf */
#define SQL_PRINTF_MALLOCED 0x04	/* True if xText is allocated space */

#define isMalloced(X)  (((X)->printfFlags & SQL_PRINTF_MALLOCED)!=0)

/*
 * Structure containing global configuration data for the sql library.
 *
 * This structure also contains some state information.
 */
struct sqlConfig {
	int bMemstat;		/* True to enable memory status */
	int bOpenUri;		/* True to interpret filenames as URIs */
	int bUseCis;		/* Use covering indices for full-scans */
	int mxStrlen;		/* Maximum string length */
	int neverCorrupt;	/* Database is always well-formed */
	int szLookaside;	/* Default lookaside buffer size */
	int nLookaside;		/* Default lookaside buffer count */
	int nStmtSpill;		/* Stmt-journal spill-to-disk threshold */
	void *pHeap;		/* Heap storage space */
	int nHeap;		/* Size of pHeap[] */
	int mnReq, mxReq;	/* Min and max heap requests sizes */
	sql_int64 szMmap;	/* mmap() space per open file */
	sql_int64 mxMmap;	/* Maximum value for szMmap */
	void *pScratch;		/* Scratch memory */
	int szScratch;		/* Size of each scratch buffer */
	int nScratch;		/* Number of scratch buffers */
	void *pPage;		/* Page cache memory */
	int szPage;		/* Size of each page in pPage[] */
	int nPage;		/* Number of pages in pPage[] */
	int mxParserStack;	/* maximum depth of the parser stack */
	int sharedCacheEnabled;	/* true if shared-cache mode enabled */
	u32 szPma;		/* Maximum Sorter PMA size */
	/* The above might be initialized to non-zero.  The following need to always
	 * initially be zero, however.
	 */
	int isInit;		/* True after initialization has finished */
	int inProgress;		/* True while initialization in progress */
	int isMallocInit;	/* True after malloc is initialized */
	void (*xLog) (void *, int, const char *);	/* Function for logging */
	void *pLogArg;		/* First argument to xLog() */
#ifdef SQL_ENABLE_SQLLOG
	void (*xSqllog) (void *, sql *, const char *, int);
	void *pSqllogArg;
#endif
#ifdef SQL_VDBE_COVERAGE
	/* The following callback (if not NULL) is invoked on every VDBE branch
	 * operation.  Set the callback using sql_TESTCTRL_VDBE_COVERAGE.
	 */
	void (*xVdbeBranch) (void *, int iSrcLine, u8 eThis, u8 eMx);	/* Callback */
	void *pVdbeBranchArg;	/* 1st argument */
#endif
#ifndef SQL_UNTESTABLE
	int (*xTestCallback) (int);	/* Invoked by sqlFaultSim() */
#endif
	int bLocaltimeFault;	/* True to fail localtime() calls */
	int iOnceResetThreshold;	/* When to reset OP_Once counters */
};

/*
 * This macro is used inside of assert() statements to indicate that
 * the assert is only valid on a well-formed database.  Instead of:
 *
 *     assert( X );
 *
 * One writes:
 *
 *     assert( X || CORRUPT_DB );
 *
 * CORRUPT_DB is true during normal operation.  CORRUPT_DB does not indicate
 * that the database is definitely corrupt, only that it might be corrupt.
 * For most test cases, CORRUPT_DB is set to false using a special
 * sql_test_control().  This enables assert() statements to prove
 * things that are always true for well-formed databases.
 */
#define CORRUPT_DB  (sqlConfig.neverCorrupt==0)

/*
 * Context pointer passed down through the tree-walk.
 */
struct Walker {
	Parse *pParse;		/* Parser context.  */
	int (*xExprCallback) (Walker *, Expr *);	/* Callback for expressions */
	int (*xSelectCallback) (Walker *, Select *);	/* Callback for SELECTs */
	void (*xSelectCallback2) (Walker *, Select *);	/* Second callback for SELECTs */
	int walkerDepth;	/* Number of subqueries */
	u8 eCode;		/* A small processing code */
	union {			/* Extra data for callback */
		NameContext *pNC;	/* Naming context */
		int n;		/* A counter */
		int iCur;	/* A cursor number */
		SrcList *pSrcList;	/* FROM clause */
		struct SrcCount *pSrcCount;	/* Counting column references */
		int *aiCol;	/* array of column indexes */
		/** Space definition. */
		struct space_def *space_def;
	} u;
};

/* Forward declarations */
int sqlWalkExpr(Walker *, Expr *);
int sqlWalkExprList(Walker *, ExprList *);
int sqlWalkSelect(Walker *, Select *);
int sqlWalkSelectExpr(Walker *, Select *);
int sqlWalkSelectFrom(Walker *, Select *);
int sqlExprWalkNoop(Walker *, Expr *);

/*
 * Return code from the parse-tree walking primitives and their
 * callbacks.
 */
#define WRC_Continue    0	/* Continue down into children */
#define WRC_Prune       1	/* Omit children but continue walking siblings */
#define WRC_Abort       2	/* Abandon the tree walk */

/*
 * An instance of this structure represents a set of one or more CTEs
 * (common table expressions) created by a single WITH clause.
 */
struct With {
	int nCte;		/* Number of CTEs in the WITH clause */
	With *pOuter;		/* Containing WITH clause, or NULL */
	struct Cte {		/* For each CTE in the WITH clause.... */
		char *zName;	/* Name of this CTE */
		ExprList *pCols;	/* List of explicit column names, or NULL */
		Select *pSelect;	/* The definition of this CTE */
		const char *zCteErr;	/* Error message for circular references */
	} a[1];
};

#ifdef SQL_DEBUG
/*
 * An instance of the TreeView object is used for printing the content of
 * data structures on sqlDebugPrintf() using a tree-like view.
 */
struct TreeView {
	int iLevel;		/* Which level of the tree we are on */
	u8 bLine[100];		/* Draw vertical in column i if bLine[i] is true */
};
#endif				/* SQL_DEBUG */

/*
 * Assuming zIn points to the first byte of a UTF-8 character,
 * advance zIn to point to the first byte of the next UTF-8 character.
 */
#define SQL_SKIP_UTF8(zIn) {                        \
  if( (*(zIn++))>=0xc0 ){                              \
    while( (*zIn & 0xc0)==0x80 ){ zIn++; }             \
  }                                                    \
}

/*
 * FTS4 is really an extension for FTS3.  It is enabled using the
 * sql_ENABLE_FTS3 macro.  But to avoid confusion we also call
 * the sql_ENABLE_FTS4 macro to serve as an alias for sql_ENABLE_FTS3.
 */
#if defined(SQL_ENABLE_FTS4) && !defined(SQL_ENABLE_FTS3)
#define SQL_ENABLE_FTS3 1
#endif

/*
 * The following macros mimic the standard library functions toupper(),
 * isspace(), isalnum(), isdigit() and isxdigit(), respectively. The
 * sql versions only work for ASCII characters, regardless of locale.
 */
#define sqlToupper(x)  ((x)&~(sqlCtypeMap[(unsigned char)(x)]&0x20))
#define sqlIsspace(x)   (sqlCtypeMap[(unsigned char)(x)]&0x01)
#define sqlIsalnum(x)   (sqlCtypeMap[(unsigned char)(x)]&0x06)
#define sqlIsalpha(x)   (sqlCtypeMap[(unsigned char)(x)]&0x02)
#define sqlIsdigit(x)   (sqlCtypeMap[(unsigned char)(x)]&0x04)
#define sqlIsxdigit(x)  (sqlCtypeMap[(unsigned char)(x)]&0x08)
#define sqlTolower(x)   (sqlUpperToLower[(unsigned char)(x)])
#define sqlIsquote(x)   (sqlCtypeMap[(unsigned char)(x)]&0x80)

/*
 * Internal function prototypes
 */
int sqlStrICmp(const char *, const char *);
unsigned sqlStrlen30(const char *);
#define sqlStrNICmp sql_strnicmp

void sqlMallocInit(void);
void sqlMallocEnd(void);
void *sqlMalloc(u64);
void *sqlMallocZero(u64);
void *sqlDbMallocZero(sql *, u64);
void *sqlDbMallocRaw(sql *, u64);
void *sqlDbMallocRawNN(sql *, u64);
char *sqlDbStrDup(sql *, const char *);
char *sqlDbStrNDup(sql *, const char *, u64);
void *sqlRealloc(void *, u64);
void *sqlDbReallocOrFree(sql *, void *, u64);
void *sqlDbRealloc(sql *, void *, u64);
void sqlDbFree(sql *, void *);
int sqlMallocSize(void *);
int sqlDbMallocSize(sql *, void *);
void *sqlScratchMalloc(int);
void sqlScratchFree(void *);
void *sqlPageMalloc(int);
void sqlPageFree(void *);
void sqlMemSetDefault(void);
#ifndef SQL_UNTESTABLE
void sqlBenignMallocHooks(void (*)(void), void (*)(void));
#endif
int sqlHeapNearlyFull(void);

/*
 * On systems with ample stack space and that support alloca(), make
 * use of alloca() to obtain space for large automatic objects.  By default,
 * obtain space from malloc().
 *
 * The alloca() routine never returns NULL.  This will cause code paths
 * that deal with sqlStackAlloc() failures to be unreachable.
 */
#ifdef SQL_USE_ALLOCA
#define sqlStackAllocRaw(D,N)   alloca(N)
#define sqlStackAllocZero(D,N)  memset(alloca(N), 0, N)
#define sqlStackFree(D,P)
#else
#define sqlStackAllocRaw(D,N)   sqlDbMallocRaw(D,N)
#define sqlStackAllocZero(D,N)  sqlDbMallocZero(D,N)
#define sqlStackFree(D,P)       sqlDbFree(D,P)
#endif

sql_int64 sqlStatusValue(int);
void sqlStatusUp(int, int);
void sqlStatusDown(int, int);
void sqlStatusHighwater(int, int);

#ifndef SQL_OMIT_FLOATING_POINT
int sqlIsNaN(double);
#else
#define sqlIsNaN(X)  0
#endif

/*
 * An instance of the following structure holds information about SQL
 * functions arguments that are the parameters to the printf() function.
 */
struct PrintfArguments {
	int nArg;		/* Total number of arguments */
	int nUsed;		/* Number of arguments used so far */
	sql_value **apArg;	/* The argument values */
};

void sqlVXPrintf(StrAccum *, const char *, va_list);
void sqlXPrintf(StrAccum *, const char *, ...);
char *sqlMPrintf(sql *, const char *, ...);
char *sqlVMPrintf(sql *, const char *, va_list);
#if defined(SQL_DEBUG)
void sqlDebugPrintf(const char *, ...);
#endif
#if defined(SQL_TEST)
void *sqlTestTextToPtr(const char *);
#endif

#if defined(SQL_DEBUG)
void sqlTreeViewExpr(TreeView *, const Expr *, u8);
void sqlTreeViewBareExprList(TreeView *, const ExprList *, const char *);
void sqlTreeViewExprList(TreeView *, const ExprList *, u8, const char *);
void sqlTreeViewSelect(TreeView *, const Select *, u8);
void sqlTreeViewWith(TreeView *, const With *);
#endif

void sqlSetString(char **, sql *, const char *);
void sqlErrorMsg(Parse *, const char *, ...);
void sqlDequote(char *);
void sqlNormalizeName(char *z);
void sqlTokenInit(Token *, char *);
int sqlKeywordCode(const unsigned char *, int);
int sqlRunParser(Parse *, const char *, char **);

/**
 * Increment error counter.
 *
 * @param parse_context Current parsing context.
 */
void
sql_parser_error(struct Parse *parse_context);

/**
 * This routine is called after a single SQL statement has been
 * parsed and a VDBE program to execute that statement has been
 * prepared.  This routine puts the finishing touches on the
 * VDBE program and resets the pParse structure for the next
 * parse.
 *
 * Note that if an error occurred, it might be the case that
 * no VDBE code was generated.
 *
 * @param parse_context Current parsing context.
 */
void
sql_finish_coding(struct Parse *parse_context);

int sqlGetTempReg(Parse *);
void sqlReleaseTempReg(Parse *, int);
int sqlGetTempRange(Parse *, int);
void sqlReleaseTempRange(Parse *, int, int);
void sqlClearTempRegCache(Parse *);
#ifdef SQL_DEBUG
int sqlNoTempsInRange(Parse *, int, int);
#endif
Expr *sqlExprAlloc(sql *, int, const Token *, int);
Expr *sqlExpr(sql *, int, const char *);
Expr *sqlExprInteger(sql *, int);
void sqlExprAttachSubtrees(sql *, Expr *, Expr *, Expr *);
Expr *sqlPExpr(Parse *, int, Expr *, Expr *);
void sqlPExprAddSelect(Parse *, Expr *, Select *);
Expr *sqlExprAnd(sql *, Expr *, Expr *);
Expr *sqlExprFunction(Parse *, ExprList *, Token *);
void sqlExprAssignVarNumber(Parse *, Expr *, u32);
ExprList *sqlExprListAppendVector(Parse *, ExprList *, IdList *, Expr *);

/**
 * Set the sort order for the last element on the given ExprList.
 *
 * @param p Expression list.
 * @param sort_order Sort order to set.
 */
void sqlExprListSetSortOrder(ExprList *, enum sort_order sort_order);

void sqlExprListSetName(Parse *, ExprList *, Token *, int);
void sqlExprListSetSpan(Parse *, ExprList *, ExprSpan *);
u32 sqlExprListFlags(const ExprList *);
int sqlInit(sql *);

void sqlPragma(Parse *, Token *, Token *, Token *, int);

/**
 * Return true if given column is part of primary key.
 * If field number is less than 63, corresponding bit
 * in column mask is tested. Otherwise, check whether 64-th bit
 * in mask is set or not. If it is set, then iterate through
 * key parts of primary index and check field number.
 * In case it isn't set, there are no key columns among
 * the rest of fields.
 */
bool
sql_space_column_is_in_pk(struct space *space, uint32_t);

/**
 * Given an expression list (which is really the list of expressions
 * that form the result set of a SELECT statement) compute appropriate
 * column names for a table that would hold the expression list.
 * All column names will be unique.
 * Initialize fields and field_count.
 *
 * @param parse Parsing context.
 * @param expr_list  Expr list from which to derive column names.
 * @param space_def Destination space definition.
 * @retval sql_OK on success.
 * @retval error codef on error.
 */
int sqlColumnsFromExprList(Parse *parse, ExprList *expr_list,
			   struct space_def *space_def);

void
sqlSelectAddColumnTypeAndCollation(Parse *, struct space_def *, Select *);
struct space *sqlResultSetOfSelect(Parse *, Select *);

void sqlStartTable(Parse *, Token *, int);
void sqlAddColumn(Parse *, Token *, struct type_def *);

/**
 * This routine is called by the parser while in the middle of
 * parsing a CREATE TABLE statement.  A "NOT NULL" constraint has
 * been seen on a column.  This routine sets the is_nullable flag
 * on the column currently under construction.
 * If nullable_action has been already set, this function raises
 * an error.
 *
 * @param parser SQL Parser object.
 * @param nullable_action on_conflict_action value.
 */
void
sql_column_add_nullable_action(struct Parse *parser,
			       enum on_conflict_action nullable_action);

void sqlAddPrimaryKey(Parse *, ExprList *, int, enum sort_order);

/**
 * Add a new CHECK constraint to the table currently under
 * construction.
 * @param parser Parsing context.
 * @param span Expression span object.
 */
void
sql_add_check_constraint(Parse *parser, ExprSpan *span);

void sqlAddDefaultValue(Parse *, ExprSpan *);
void sqlAddCollateType(Parse *, Token *);

/**
 * Return collation of given column from table.
 * @param def space definition which is used to fetch column.
 * @param column Number of column.
 * @param[out] coll_id Collation identifier.
 *
 * @retval Pointer to collation.
 */
struct coll *
sql_column_collation(struct space_def *def, uint32_t column, uint32_t *coll_id);

void sqlEndTable(Parse *, Token *, Select *);

/**
 * Create cursor which will be positioned to the space/index.
 * It makes space lookup and loads pointer to it into register,
 * which is passes to OP_IteratorOpen as an argument.
 *
 * @param parse_context Parse context.
 * @param cursor Number of cursor to be created.
 * @param index_id index id. In future will be replaced with
 *        pointer to struct index.
 * @param space Pointer to space object.
 * @retval address of last opcode.
 */
int
vdbe_emit_open_cursor(struct Parse *parse, int cursor, int index_id,
		      struct space *space);

int sqlParseUri(const char *, const char *, unsigned int *,
		    sql_vfs **, char **, char **);

#ifdef SQL_UNTESTABLE
#define sqlFaultSim(X) SQL_OK
#else
int sqlFaultSim(int);
#endif

/**
 * The parser calls this routine in order to create a new VIEW.
 *
 * @param parse_context Current parsing context.
 * @param begin The CREATE token that begins the statement.
 * @param name The token that holds the name of the view.
 * @param aliases Optional list of view column names.
 * @param select A SELECT statement that will become the new view.
 * @param if_exists Suppress error messages if VIEW already exists.
 */
void
sql_create_view(struct Parse *parse_context, struct Token *begin,
		struct Token *name, struct ExprList *aliases,
		struct Select *select, bool if_exists);

/**
 * Compile view, i.e. create struct Select from
 * 'CREATE VIEW...' string, and assign cursors to each table from
 * 'FROM' clause.
 *
 * @param parse Parsing context.
 * @param view_stmt String containing 'CREATE VIEW' statement.
 * @retval 0 if success, -1 in case of error.
 */
int
sql_view_assign_cursors(struct Parse *parse, const char *view_stmt);

/**
 * Store duplicate of SELECT into parsing context.
 * This routine is called during parsing.
 *
 * @param parse_context Current parsing context.
 * @param select Select to be stored.
 */
void
sql_store_select(struct Parse *parse_context, struct Select *select);

void
sql_drop_table(struct Parse *, struct SrcList *, bool, bool);
void sqlInsert(Parse *, SrcList *, Select *, IdList *,
	       enum on_conflict_action);
void *sqlArrayAllocate(sql *, void *, int, int *, int *);
IdList *sqlIdListAppend(sql *, IdList *, Token *);
int sqlIdListIndex(IdList *, const char *);
SrcList *sqlSrcListEnlarge(sql *, SrcList *, int, int);
SrcList *
sql_alloc_src_list(sql *db);
SrcList *sqlSrcListAppend(sql *, SrcList *, Token *);
SrcList *sqlSrcListAppendFromTerm(Parse *, SrcList *, Token *,
				      Token *, Select *, Expr *, IdList *);
void sqlSrcListIndexedBy(Parse *, SrcList *, Token *);
void sqlSrcListFuncArgs(Parse *, SrcList *, ExprList *);
int sqlIndexedByLookup(Parse *, struct SrcList_item *);
void sqlSrcListShiftJoinType(SrcList *);
void sqlSrcListAssignCursors(Parse *, SrcList *);
void sqlIdListDelete(sql *, IdList *);

/**
 * Create a new index for an SQL table.  name is the name of the
 * index and tbl_name is the name of the table that is to be
 * indexed.  Both will be NULL for a primary key or an index that
 * is created to satisfy a UNIQUE constraint.  If tbl_name and
 * name are NULL, use parse->new_space as the table to be indexed.
 * parse->new_space is a space that is currently being
 * constructed by a CREATE TABLE statement.
 *
 * col_list is a list of columns to be indexed.  col_list will be
 * NULL if this is a primary key or unique-constraint on the most
 * recent column added to the table currently under construction.
 *
 * @param parse All information about this parse.
 * @param token Index name. May be NULL.
 * @param tbl_name Table to index. Use pParse->new_space if NULL.
 * @param col_list A list of columns to be indexed.
 * @param start The CREATE token that begins this statement.
 * @param sort_order Sort order of primary key when pList==NULL.
 * @param if_not_exist Omit error if index already exists.
 * @param idx_type The index type.
 */
void
sql_create_index(struct Parse *parse, struct Token *token,
		 struct SrcList *tbl_name, struct ExprList *col_list,
		 struct Token *start, enum sort_order sort_order,
		 bool if_not_exist, enum sql_index_type idx_type);

/**
 * This routine will drop an existing named index.  This routine
 * implements the DROP INDEX statement.
 *
 * @param parse_context Current parsing context.
 * @param index_name_list List containing index name.
 * @param table_token Token representing table name.
 * @param if_exists True, if statement contains 'IF EXISTS' clause.
 */
void
sql_drop_index(struct Parse *, struct SrcList *, struct Token *, bool);

int sqlSelect(Parse *, Select *, SelectDest *);
Select *sqlSelectNew(Parse *, ExprList *, SrcList *, Expr *, ExprList *,
			 Expr *, ExprList *, u32, Expr *, Expr *);

/**
 * While a SrcList can in general represent multiple spaces and
 * subqueries (as in the FROM clause of a SELECT statement) in
 * this case it contains the name of a single table, as one might
 * find in an INSERT, DELETE, or UPDATE statement. Look up that
 * space in the cache.
 * Set an error message and return NULL if the table name is not
 * found or if space doesn't have format.
 *
 * The following fields are initialized appropriate in src_list:
 *
 *    src_list->a[0].space      Pointer to the space object.
 *    src_list->a[0].pIndex     Pointer to the INDEXED BY index,
 *                              if there is one.
 *
 * @param parse Parsing context.
 * @param space_name Space element.
 * @retval Space object if found, NULL otherwise.
 */
struct space *
sql_lookup_space(struct Parse *parse, struct SrcList_item *space_name);

/**
 * Generate code for a DELETE FROM statement.
 *
 *     DELETE FROM table_wxyz WHERE a<5 AND b NOT NULL;
 *                 \________/       \________________/
 *                  tab_list              where
 *
 * @param parse Parsing context.
 * @param tab_list List of single element which table from which
 * deletetion if performed.
 * @param where The WHERE clause.  May be NULL.
 */
void
sql_table_delete_from(struct Parse *parse, struct SrcList *tab_list,
		      struct Expr *where);

/**
 * Generate a code for TRUNCATE TABLE statement.
 *
 * @param parse Parsing context.
 * @param tab_list List of single table to truncate.
 */
void
sql_table_truncate(struct Parse *parse, struct SrcList *tab_list);

void sqlUpdate(Parse *, SrcList *, ExprList *, Expr *,
		   enum on_conflict_action);
WhereInfo *sqlWhereBegin(Parse *, SrcList *, Expr *, ExprList *, ExprList *,
			     u16, int);
void sqlWhereEnd(WhereInfo *);
LogEst sqlWhereOutputRowCount(WhereInfo *);
int sqlWhereIsDistinct(WhereInfo *);
int sqlWhereIsOrdered(WhereInfo *);
int sqlWhereOrderedInnerLoop(WhereInfo *);
int sqlWhereIsSorted(WhereInfo *);
int sqlWhereContinueLabel(WhereInfo *);
int sqlWhereBreakLabel(WhereInfo *);
int sqlWhereOkOnePass(WhereInfo *, int *);
#define ONEPASS_OFF      0	/* Use of ONEPASS not allowed */
#define ONEPASS_SINGLE   1	/* ONEPASS valid for a single row update */
#define ONEPASS_MULTI    2	/* ONEPASS is valid for multiple rows */

/**
 * Generate code that will extract the iColumn-th column from
 * table pTab and store the column value in a register.
 *
 * An effort is made to store the column value in register iReg.
 * This is not garanteeed for GetColumn() - the result can be
 * stored in any register.  But the result is guaranteed to land
 * in register iReg for GetColumnToReg().
 * @param pParse Parsing and code generating context.
 * @param space_def Space definition.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 * @param p5 P5 value for OP_Column + FLAGS.
 * @return iReg value.
 */
int
sqlExprCodeGetColumn(Parse *, struct space_def *, int, int, int, u8);

/**
 * Generate code that will extract the iColumn-th column from
 * table defined by space_def and store the column value in
 * a register, copy the result.
 * @param pParse Parsing and code generating context.
 * @param space_def Space definition.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 */
void
sqlExprCodeGetColumnToReg(Parse *, struct space_def *, int, int, int);

/**
 * Generate code to extract the value of the iCol-th column of a table.
 * @param v  The VDBE under construction.
 * @param space_def Space definition.
 * @param iTabCur The PK cursor.
 * @param iCol Index of the column to extract.
 * @param regOut  Extract the value into this register.
 */
void
sqlExprCodeGetColumnOfTable(Vdbe *, struct space_def *, int, int, int);

void sqlExprCodeMove(Parse *, int, int, int);
void sqlExprCacheStore(Parse *, int, int, int);
void sqlExprCachePush(Parse *);
void sqlExprCachePop(Parse *);
void sqlExprCacheRemove(Parse *, int, int);
void sqlExprCacheClear(Parse *);
void sql_expr_type_cache_change(Parse *, int, int);
void sqlExprCode(Parse *, Expr *, int);
void sqlExprCodeCopy(Parse *, Expr *, int);
void sqlExprCodeFactorable(Parse *, Expr *, int);
void sqlExprCodeAtInit(Parse *, Expr *, int, u8);
int sqlExprCodeTemp(Parse *, Expr *, int *);
int sqlExprCodeTarget(Parse *, Expr *, int);
void sqlExprCodeAndCache(Parse *, Expr *, int);
int sqlExprCodeExprList(Parse *, ExprList *, int, int, u8);
#define SQL_ECEL_DUP      0x01	/* Deep, not shallow copies */
#define SQL_ECEL_FACTOR   0x02	/* Factor out constant terms */
#define SQL_ECEL_REF      0x04	/* Use ExprList.u.x.iOrderByCol */
#define SQL_ECEL_OMITREF  0x08	/* Omit if ExprList.u.x.iOrderByCol */
void sqlExprIfTrue(Parse *, Expr *, int, int);
void sqlExprIfFalse(Parse *, Expr *, int, int);

char *sqlNameFromToken(sql *, Token *);
int sqlExprCompare(Expr *, Expr *, int);
int sqlExprListCompare(ExprList *, ExprList *, int);
int sqlExprImpliesExpr(Expr *, Expr *, int);
void sqlExprAnalyzeAggregates(NameContext *, Expr *);
void sqlExprAnalyzeAggList(NameContext *, ExprList *);
int sqlFunctionUsesThisSrc(Expr *, SrcList *);
Vdbe *sqlGetVdbe(Parse *);
#ifndef SQL_UNTESTABLE
void sqlPrngSaveState(void);
void sqlPrngRestoreState(void);
#endif
void sqlRollbackAll(Vdbe *);

/**
 * Generate opcodes which start new Tarantool transaction.
 * Used from parser to process BEGIN statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_begin(struct Parse *parse_context);

/**
 * Generate opcodes which commit Tarantool transaction.
 * Used from parser to process COMMIT statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_commit(struct Parse *parse_context);

/**
 * Generate opcodes which rollback Tarantool transaction.
 * Used from parser to process ROLLBACK statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_rollback(struct Parse *parse_context);

void sqlSavepoint(Parse *, int, Token *);
void sqlCloseSavepoints(Vdbe *);
int sqlExprIsConstant(Expr *);
int sqlExprIsConstantNotJoin(Expr *);
int sqlExprIsConstantOrFunction(Expr *, u8);
int sqlExprIsTableConstant(Expr *, int);
int sqlExprIsInteger(Expr *, int *);
int sqlExprCanBeNull(const Expr *);

/**
 * Return TRUE if the given expression is a constant which would
 * be unchanged by OP_ApplyType with the type given in the second
 * argument.
 *
 * This routine is used to determine if the OP_ApplyType operation
 * can be omitted.  When in doubt return FALSE.  A false negative
 * is harmless. A false positive, however, can result in the wrong
 * answer.
 */
bool
sql_expr_needs_no_type_change(const struct Expr *expr, enum field_type type);

/**
 * This routine generates VDBE code that causes a single row of a
 * single table to be deleted.  Both the original table entry and
 * all indices are removed.
 *
 * Preconditions:
 *
 *   1.  cursor is an open cursor on the btree that is the
 *       canonical data store for the table.  (This will be the
 *       PRIMARY KEY index)
 *
 *   2.  The primary key for the row to be deleted must be stored
 *       in a sequence of npk memory cells starting at reg_pk. If
 *       npk==0 that means that a search record formed from
 *       OP_MakeRecord is contained in the single memory location
 *       reg_pk.
 *
 *   Parameter mode may be passed either ONEPASS_OFF (0),
 *   ONEPASS_SINGLE, or ONEPASS_MULTI.  If mode is not
 *   ONEPASS_OFF, then the cursor already points to the row to
 *   delete. If mode is ONEPASS_OFF then this function must seek
 *   cursor to the entry identified by reg_pk and npk before
 *   reading from it.
 *
 *   If mode is ONEPASS_MULTI, then this call is being made as
 *   part of a ONEPASS delete that affects multiple rows. In this
 *   case, if idx_noseek is a valid cursor number (>=0), then its
 *   position should be preserved following the delete operation.
 *   Or, if idx_noseek is not a valid cursor number, the position
 *   of cursor should be preserved instead.
 *
 * @param parse Parsing context.
 * @param space Space containing the row to be deleted.
 * @param trigger_list List of triggers to (potentially) fire.
 * @param cursor Cursor from which column data is extracted/
 * @param reg_pk First memory cell containing the PRIMARY KEY.
 * @param npk umber of PRIMARY KEY memory cells.
 * @param need_update_count. If non-zero, increment the row change
 *        counter.
 * @param onconf Default ON CONFLICT policy for triggers.
 * @param mode ONEPASS_OFF, _SINGLE, or _MULTI.  See above.
 * @param idx_noseek If it is a valid cursor number (>=0),
 *        then it identifies an index cursor that already points
 *        to the index entry to be deleted.
 */
void
sql_generate_row_delete(struct Parse *parse, struct space *space,
			struct sql_trigger *trigger_list, int cursor,
			int reg_pk, short npk, bool need_update_count,
			enum on_conflict_action onconf, u8 mode,
			int idx_noseek);

/**
 * Generate code that will assemble an index key and stores it in
 * register reg_out. The key will be for index which is an
 * index on table. cursor is the index of a cursor open on the
 * table table and pointing to the entry that needs indexing.
 * cursor must be the cursor of the PRIMARY KEY index.
 *
 * The prev and reg_prev parameters are used to implement a
 * cache to avoid unnecessary register loads.  If prev is not
 * NULL, then it is a pointer to a different index for which an
 * index key has just been computed into register reg_prev. If the
 * current index is generating its key into the same
 * sequence of registers and if prev and index share a column in
 * common, then the register corresponding to that column already
 * holds the correct value and the loading of that register is
 * skipped. This optimization is helpful when doing a DELETE or
 * an INTEGRITY_CHECK on a table with multiple indices, and
 * especially with the PRIMARY KEY columns of the index.
 *
 * @param parse Parsing context.
 * @param index The index for which to generate a key.
 * @param cursor Cursor number from which to take column data.
 * @param reg_out Put the new key into this register if not NULL.
 * @param prev Previously generated index key
 * @param reg_prev Register holding previous generated key.
 *
 * @retval Return a register number which is the first in a block
 * of registers that holds the elements of the index key. The
 * block of registers has already been deallocated by the time
 * this routine returns.
 */
int
sql_generate_index_key(struct Parse *parse, struct index *index, int cursor,
		       int reg_out, struct index *prev, int reg_prev);

/**
 * Generate code to do constraint checks prior to an INSERT or
 * an UPDATE on the given table.
 *
 * The @new_tuple_reg is the first register in a range that
 * contains the data to be inserted or the data after the update.
 * There will be field_count registers in this range.
 * The first register in the range will contains the content of
 * the first table column, and so forth.
 *
 * To test NULL, CHECK and statement (except for REPLACE)
 * constraints we can avoid opening cursors on secondary indexes.
 * However, to implement INSERT OR REPLACE or UPDATE OR REPLACE,
 * we should
 *
 * Constraint
 *    type       Action              What Happens
 * ----------  ----------  --------------------------------------
 *    any       ROLLBACK   The current transaction is rolled
 *                         back and VDBE stops immediately
 *                         with return code of sql_CONSTRAINT.
 *
 *    any        ABORT     Back out changes from the current
 *                         command only (do not do a complete
 *                         rollback) then cause VDBE to return
 *                         immediately with sql_CONSTRAINT.
 *
 *    any        FAIL      VDBE returns immediately with a
 *                         return code of sql_CONSTRAINT. The
 *                         transaction is not rolled back and any
 *                         changes to prior rows are retained.
 *
 *    any       IGNORE     The attempt in insert or update the
 *                         current row is skipped, without
 *                         throwing an error. Processing
 *                         continues with the next row.
 *
 *  NOT NULL    REPLACE    The NULL value is replace by the
 *                         default value for that column. If the
 *                         default value is NULL, the action is
 *                         the same as ABORT.
 *
 *  UNIQUE      REPLACE    The other row that conflicts with the
 *                         row being inserted is removed.
 *                         Triggers are fired, foreign keys
 *                         constraints are checked.
 *
 *  CHECK       REPLACE    Illegal. Results in an exception.
 *
 * @param parse_context Current parsing context.
 * @param space The space being inserted or updated.
 * @param new_tuple_reg First register in a range holding values
 *                      to insert.
 * @param on_conflict On conflict error action of INSERT or
 *        UPDATE statement (for example INSERT OR REPLACE).
 * @param ignore_label Jump to this label on an IGNORE resolution.
 * @param upd_cols Columns to be updated with the size of table's
 *                 field count. NULL for INSERT operation.
 */
void
vdbe_emit_constraint_checks(struct Parse *parse_context,
			    struct space *space, int new_tuple_reg,
			    enum on_conflict_action on_conflict,
			    int ignore_label, int *upd_cols);

/**
 * This routine generates code to finish the INSERT or UPDATE
 * operation that was started by a prior call to
 * vdbe_emit_constraint_checks. It encodes raw data which is held
 * in a range of registers starting from @raw_data_reg and length
 * of @tuple_len and inserts this record to space using given
 * @cursor_id.
 *
 * @param v Virtual database engine.
 * @param space Pointer to space object.
 * @param raw_data_reg Register with raw data to insert.
 * @param tuple_len Number of registers to hold the tuple.
 * @param on_conflict On conflict action.
 */
void
vdbe_emit_insertion_completion(struct Vdbe *v, struct space *space,
			       int raw_data_reg, uint32_t tuple_len,
			       enum on_conflict_action on_conflict);

void
sql_set_multi_write(Parse *, bool);
void sqlMayAbort(Parse *);
void sqlHaltConstraint(Parse *, int, int, char *, i8, u8);

Expr *sqlExprDup(sql *, Expr *, int);
SrcList *sqlSrcListDup(sql *, SrcList *, int);
IdList *sqlIdListDup(sql *, IdList *);
Select *sqlSelectDup(sql *, Select *, int);
#ifdef SQL_DEBUG
void sqlSelectSetName(Select *, const char *);
#else
#define sqlSelectSetName(A,B)
#endif
void sqlInsertBuiltinFuncs(FuncDef *, int);
FuncDef *sqlFindFunction(sql *, const char *, int, u8);
void sqlRegisterBuiltinFunctions(void);
void sqlRegisterDateTimeFunctions(void);
void sqlRegisterPerConnectionBuiltinFunctions(sql *);
int sqlSafetyCheckOk(sql *);
int sqlSafetyCheckSickOrOk(sql *);

/**
 * Evaluate a view and store its result in an ephemeral table.
 * The where argument is an optional WHERE clause that restricts
 * the set of rows in the view that are to be added to the
 * ephemeral table.
 *
 * @param parse Parsing context.
 * @param name View name.
 * @param where Option WHERE clause to be added.
 * @param cursor Cursor number for ephemeral table.
 */
void
sql_materialize_view(struct Parse *parse, const char *name, struct Expr *where,
		     int cursor);

/**
 * This is called by the parser when it sees a CREATE TRIGGER
 * statement up to the point of the BEGIN before the trigger
 * actions.  A sql_trigger structure is generated based on the
 * information available and stored in parse->parsed_ast.trigger.
 * After the trigger actions have been parsed, the
 * sql_trigger_finish() function is called to complete the trigger
 * construction process.
 *
 * @param parse The parse context of the CREATE TRIGGER statement.
 * @param name The name of the trigger.
 * @param tr_tm One of TK_BEFORE, TK_AFTER, TK_INSTEAD.
 * @param op One of TK_INSERT, TK_UPDATE, TK_DELETE.
 * @param columns column list if this is an UPDATE OF trigger.
 * @param table The name of the table/view the trigger applies to.
 * @param when  WHEN clause.
 * @param no_err Suppress errors if the trigger already exists.
 */
void
sql_trigger_begin(struct Parse *parse, struct Token *name, int tr_tm,
		  int op, struct IdList *columns, struct SrcList *table,
		  struct Expr *when, int no_err);

/**
 * This routine is called after all of the trigger actions have
 * been parsed in order to complete the process of building the
 * trigger.
 *
 * @param parse Parser context.
 * @param step_list The triggered program.
 * @param token Token that describes the complete CREATE TRIGGER.
 */
void
sql_trigger_finish(struct Parse *parse, struct TriggerStep *step_list,
		   struct Token *token);

/**
 * This function is called from parser to generate drop trigger
 * VDBE code.
 *
 * @param parser Parser context.
 * @param name The name of trigger to drop.
 * @param no_err Suppress errors if the trigger already exists.
 */
void
sql_drop_trigger(struct Parse *parser, struct SrcList *name, bool no_err);

/**
 * Drop a trigger given a pointer to that trigger.
 *
 * @param parser Parser context.
 * @param trigger_name The name of trigger to drop.
 * @param account_changes Increase number of db changes made since
 *        last reset.
 */
void
vdbe_code_drop_trigger(struct Parse *parser, const char *trigger_name,
		       bool account_changes);

/**
 * Return a list of all triggers on space (represented with
 * space_def) if there exists at least one trigger that must be
 * fired when an operation of type 'op' is performed on the
 * table, and, if that operation is an UPDATE, if at least one
 * of the columns in changes_list is being modified.
 *
 * @param space_def The definition of the space that contains
 *        the triggers.
 * @param op operation one of TK_DELETE, TK_INSERT, TK_UPDATE.
 * @param changes_list Columns that change in an UPDATE statement.
 * @param[out] pMask Mask of TRIGGER_BEFORE|TRIGGER_AFTER
 */
struct sql_trigger *
sql_triggers_exist(struct space_def *space_def, int op,
		   struct ExprList *changes_list, int *mask_ptr);

/**
 * This is called to code the required FOR EACH ROW triggers for
 * an operation on table. The operation to code triggers for
 * (INSERT, UPDATE or DELETE) is given by the op parameter. The
 * tr_tm parameter determines whether the BEFORE or AFTER triggers
 * are coded. If the operation is an UPDATE, then parameter
 * changes_list is passed the list of columns being modified.
 *
 * If there are no triggers that fire at the specified time for
 * the specified operation on table, this function is a no-op.
 *
 * The reg argument is the address of the first in an array of
 * registers that contain the values substituted for the new.*
 * and old.* references in the trigger program. If N is the number
 * of columns in table table, then registers are populated as
 * follows:
 *
 *   Register       Contains
 *   ------------------------------------------------------
 *   reg+0          OLD.PK
 *   reg+1          OLD.* value of left-most column of space
 *   ...            ...
 *   reg+N          OLD.* value of right-most column of space
 *   reg+N+1        NEW.PK
 *   reg+N+2        OLD.* value of left-most column of space
 *   ...            ...
 *   reg+N+N+1      NEW.* value of right-most column of space
 *
 * For ON DELETE triggers, the registers containing the NEW.*
 * values will never be accessed by the trigger program, so they
 * are not allocated or populated by the caller (there is no data
 * to populate them with anyway). Similarly, for ON INSERT
 * triggers the values stored in the OLD.* registers are never
 * accessed, and so are not allocated by the caller. So, for an
 * ON INSERT trigger, the value passed to this function as
 * parameter reg is not a readable register, although registers
 * (reg+N) through (reg+N+N+1) are.
 *
 * Parameter orconf is the default conflict resolution algorithm
 * for the trigger program to use (REPLACE, IGNORE etc.).
 * Parameter ignoreJump is the instruction that control should
 * jump to if a trigger program raises an IGNORE exception.
 *
 * @param parser Parse context.
 * @param trigger List of triggers on table.
 * @param op operation, one of TK_UPDATE, TK_INSERT, TK_DELETE.
 * @param changes_list Changes list for any UPDATE OF triggers.
 * @param tr_tm One of TRIGGER_BEFORE, TRIGGER_AFTER.
 * @param space The space to code triggers from.
 * @param reg The first in an array of registers.
 * @param orconf ON CONFLICT policy.
 * @param ignore_jump Instruction to jump to for RAISE(IGNORE).
 */
void
vdbe_code_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		      int op, struct ExprList *changes_list, int tr_tm,
		      struct space *space, int reg, int orconf, int ignore_jump);

/**
 * Generate code for the trigger program associated with trigger
 * p on table table. The reg, orconf and ignoreJump parameters
 * passed to this function are the same as those described in the
 * header function for sql_code_row_trigger().
 *
 * @param parser Parse context.
 * @param trigger Trigger to code.
 * @param space The space to code triggers from.
 * @param reg Reg array containing OLD.* and NEW.* values.
 * @param orconf ON CONFLICT policy.
 * @param ignore_jump Instruction to jump to for RAISE(IGNORE).
 */
void
vdbe_code_row_trigger_direct(struct Parse *parser, struct sql_trigger *trigger,
			     struct space *space, int reg, int orconf,
			     int ignore_jump);

void sqlDeleteTriggerStep(sql *, TriggerStep *);
TriggerStep *sqlTriggerSelectStep(sql *, Select *);
TriggerStep *sqlTriggerInsertStep(sql *, Token *, IdList *,
				      Select *, u8);
TriggerStep *sqlTriggerUpdateStep(sql *, Token *, ExprList *, Expr *,
				      u8);
TriggerStep *sqlTriggerDeleteStep(sql *, Token *, Expr *);

/**
 * Triggers may access values stored in the old.* or new.*
 * pseudo-table.
 * This function returns a 32-bit bitmask indicating which columns
 * of the old.* or new.* tables actually are used by triggers.
 * This information may be used by the caller, for example, to
 * avoid having to load the entire old.* record into memory when
 * executing an UPDATE or DELETE command.
 *
 * Bit 0 of the returned mask is set if the left-most column of
 * the table may be accessed using an [old|new].<col> reference.
 * Bit 1 is set if the second leftmost column value is required,
 * and so on. If there are more than 32 columns in the table, and
 * at least one of the columns with an index greater than 32 may
 * be accessed, 0xffffffff is returned.
 *
 * It is not possible to determine if the old.PK or new.PK column
 * is accessed by triggers. The caller must always assume that it
 * is.
 *
 * Parameter isNew must be either 1 or 0. If it is 0, then the
 * mask returned applies to the old.* table. If 1, the new.* table.
 *
 * Parameter tr_tm must be a mask with one or both of the
 * TRIGGER_BEFORE and TRIGGER_AFTER bits set. Values accessed by
 * BEFORE triggers are only included in the returned mask if the
 * TRIGGER_BEFORE bit is set in the tr_tm parameter. Similarly,
 * values accessed by AFTER triggers are only included in the
 * returned mask if the TRIGGER_AFTER bit is set in tr_tm.
 *
 * @param parser  Parse context.
 * @param trigger List of triggers on table.
 * @param changes_list Changes list for any UPDATE OF triggers.
 * @param new  1 for new.* ref mask, 0 for old.* ref mask.
 * @param tr_tm Mask of TRIGGER_BEFORE|TRIGGER_AFTER.
 * @param space The space to code triggers from.
 * @param orconf Default ON CONFLICT policy for trigger steps.
 *
 * @retval mask value.
 */
u32
sql_trigger_colmask(Parse *parser, struct sql_trigger *trigger,
		    ExprList *changes_list, int new, int tr_tm,
		    struct space *space, int orconf);
#define sqlParseToplevel(p) ((p)->pToplevel ? (p)->pToplevel : (p))
#define sqlIsToplevel(p) ((p)->pToplevel==0)

int sqlJoinType(Parse *, Token *, Token *, Token *);

/**
 * Change defer mode of last FK constraint processed during
 * <CREATE TABLE> statement.
 *
 * @param parse_context Current parsing context.
 * @param is_deferred Change defer mode to this value.
 */
void
fk_constraint_change_defer_mode(struct Parse *parse_context, bool is_deferred);

/**
 * Function called from parser to handle
 * <ALTER TABLE child ADD CONSTRAINT constraint
 *     FOREIGN KEY (child_cols) REFERENCES parent (parent_cols)>
 * OR to handle <CREATE TABLE ...>
 *
 * @param parse_context Parsing context.
 * @param child Name of table to be altered. NULL on CREATE TABLE
 *              statement processing.
 * @param constraint Name of the constraint to be created. May be
 *                   NULL on CREATE TABLE statement processing.
 *                   Then, auto-generated name is used.
 * @param child_cols Columns of child table involved in FK.
 *                   May be NULL on CREATE TABLE statement processing.
 *                   If so, the last column added is used.
 * @param parent Name of referenced table.
 * @param parent_cols List of referenced columns. If NULL, columns
 *                    which make up PK of referenced table are used.
 * @param is_deferred Is FK constraint initially deferred.
 * @param match Value of MATCH clause: SIMPLE, PARTIAL, FULL.
 * @param actions ON DELETE and ON UPDATE resolution algorithms
 *               (e.g. CASCADE, RESTRICT etc).
 */
void
sql_create_foreign_key(struct Parse *parse_context, struct SrcList *child,
		       struct Token *constraint, struct ExprList *child_cols,
		       struct Token *parent, struct ExprList *parent_cols,
		       bool is_deferred, int match, int actions);

/**
 * Function called from parser to handle
 * <ALTER TABLE table DROP CONSTRAINT constraint> SQL statement.
 *
 * @param parse_context Parsing context.
 * @param table Table to be altered.
 * @param constraint Name of constraint to be dropped.
 */
void
sql_drop_foreign_key(struct Parse *parse_context, struct SrcList *table,
		     struct Token *constraint);

/**
 * Counts the trail bytes for a UTF-8 lead byte of a valid UTF-8
 * sequence.
 *
 * Note that implementation is borrowed from ICU library.
 * It is not directly included from icu/utf8.h owing to the
 * fact that different versions of ICU treat incorrect byte
 * sequences in different ways. We like this implementation
 * but don't like that it could give different results depending
 * on version of library. And that's why we inlined these macros.
 *
 * @param lead_byte The first byte of a UTF-8 sequence.
 */
#define SQL_UTF8_COUNT_TRAIL_BYTES(lead_byte) \
	(((uint8_t)(lead_byte) >= 0xc2) + ((uint8_t)(lead_byte) >= 0xe0) + \
	((uint8_t)(lead_byte) >= 0xf0))

/**
 * Advance the string offset from one code point boundary to the
 * next. (Post-incrementing iteration.)
 *
 * After the whole string is traversed, (str + i) points to the
 * position right after the last element of the string (*).
 *
 * If resulting offset > byte_size then resulting offset is set
 * to byte_size. This is to provide (*) in cases where it might
 * be violated.
 *
 * SQL_UTF8_FWD_1 sometimes is used to get the size of utf-8
 * character sub-sequence and we don't want to get summary size
 * which exceeds total string size (in bytes). Consider example:
 *
 * '0xE0' - this is invalid utf-8 string because it consists only
 * of first byte of 3 byte sequence. After traverse, the
 * offset == 2 and we set it to 1, to keep (*).
 *
 * @param s const uint8_t * string.
 * @param i string offset.
 * @param byte_size byte size of the string.
 */
#define SQL_UTF8_FWD_1(str, i, byte_size) \
	(i) += 1 + SQL_UTF8_COUNT_TRAIL_BYTES((str)[i]); \
	(i) = (i) <= (byte_size) ? (i) : (byte_size);

void sqlDetach(Parse *, Expr *);
int sqlAtoF(const char *z, double *, int);
int sqlGetInt32(const char *, int *);
int sqlAtoi(const char *);

/**
 * Return number of symbols in the given string.
 *
 * Number of symbols != byte size of string because some symbols
 * are encoded with more than one byte. Also note that all
 * symbols from 'str' to 'str + byte_len' would be counted,
 * even if there is a '\0' somewhere between them.
 *
 * This function is implemented to be fast and indifferent to
 * correctness of string being processed. If input string has
 * even one invalid utf-8 sequence, then the resulting length
 * could be arbitary in these boundaries (0 < len < byte_len).
 * @param str String to be counted.
 * @param byte_len Byte length of given string.
 * @return number of symbols in the given string.
 */
int
sql_utf8_char_count(const unsigned char *str, int byte_len);

u32 sqlUtf8Read(const u8 **);
LogEst sqlLogEst(u64);
LogEst sqlLogEstAdd(LogEst, LogEst);
u64 sqlLogEstToInt(LogEst);
VList *sqlVListAdd(sql *, VList *, const char *, int, int);
const char *sqlVListNumToName(VList *, int);
int sqlVListNameToNum(VList *, const char *, int);

/*
 * Routines to read and write variable-length integers.  These used to
 * be defined locally, but now we use the varint routines in the util.c
 * file.
 */
int sqlPutVarint(unsigned char *, u64);
u8 sqlGetVarint(const unsigned char *, u64 *);
u8 sqlGetVarint32(const unsigned char *, u32 *);
int sqlVarintLen(u64 v);

/*
 * The common case is for a varint to be a single byte.  They following
 * macros handle the common case without a procedure call, but then call
 * the procedure for larger varints.
 */
#define getVarint32(A,B)  \
  (u8)((*(A)<(u8)0x80)?((B)=(u32)*(A)),1:sqlGetVarint32((A),(u32 *)&(B)))
#define putVarint32(A,B)  \
  (u8)(((u32)(B)<(u32)0x80)?(*(A)=(unsigned char)(B)),1:\
  sqlPutVarint((A),(B)))
#define getVarint    sqlGetVarint
#define putVarint    sqlPutVarint

/** Return string consisting of fields types of given index. */
enum field_type *
sql_index_type_str(struct sql *db, const struct index_def *idx_def);

/**
 * Code an OP_ApplyType opcode that will force types
 * for given range of register starting from @reg.
 *
 * @param v VDBE.
 * @param def Definition of table to be used.
 * @param reg Register where types will be placed.
 */
void
sql_emit_table_types(struct Vdbe *v, struct space_def *def, int reg);

enum field_type
sql_type_result(enum field_type lhs, enum field_type rhs);

/**
 * pExpr is a comparison operator. Return the type affinity
 * that should be applied to both operands prior to doing
 * the comparison.
 */
enum field_type
expr_cmp_mutual_type(struct Expr *pExpr);

/**
 * Return the type of the expression pExpr.
 *
 * If pExpr is a column, a reference to a column via an 'AS' alias,
 * or a sub-select with a column as the return value, then the
 * type of that column is returned. Otherwise, type ANY is returned,
 * indicating that the expression can feature any type.
 *
 * The WHERE clause expressions in the following statements all
 * have an type:
 *
 * CREATE TABLE t1(a);
 * SELECT * FROM t1 WHERE a;
 * SELECT a AS b FROM t1 WHERE b;
 * SELECT * FROM t1 WHERE (select a from t1);
 */
enum field_type
sql_expr_type(struct Expr *pExpr);

/**
 * This function duplicates first @len entries of types array
 * and terminates new array with field_type_MAX member.
 */
enum field_type *
field_type_sequence_dup(struct Parse *parse, enum field_type *types,
			uint32_t len);

/**
 * Converts z to a 64-bit signed or unsigned integer.
 * z must be decimal. This routine does *not* accept
 * hexadecimal notation.
 *
 * If the z value is representable as a 64-bit twos-complement
 * integer, then write that value into *val and return 0.
 *
 * If z is a number in the range
 * [9223372036854775808, 18446744073709551615] function returns
 * 0 and is_unsigned = true, the result must be treated as unsigned.
 *
 * If z is too big for a 64-bit unsigned integer
 * or if z contains any non-numeric text,
 * then return -1.
 *
 * length is the number of bytes in the string (bytes, not
 * characters). The string is not necessarily zero-terminated.
 * The encoding is given by enc.
 *
 * @param z String being parsed.
 * @param[out] val Output integer value.
 * @param[out] is_unsigned is true is returned value is positive
 * and its value is in the range [INT64_MAX+1, UINT64_MAX]
 * @param length String length in bytes.
 * @retval
 *     0	Successful transformation.
 *     -1	An error occurred.
 */
int
sql_atoi64(const char *z, int64_t *val, bool *is_unsigned, int length);

/**
 * Transform a UTF-8 integer literal, in either decimal or
 * hexadecimal, into a 64-bit signed integer.  This routine
 * accepts hexadecimal literals, whereas sql_atoi64() does not.
 *
 * @param z Literal being parsed.
 * @param is_neg Sign of the number being converted
 * @param[out] val Parsed value.
 * @param[out] is_unsigned is true is returned value is positive
 * and its value is in the range [INT64_MAX+1, UINT64_MAX]
 * @retval
 *     0	Successful transformation.
 *     -1	An error occurred.
 */
int
sql_dec_or_hex_to_i64(const char *z, bool is_neg, int64_t *val,
		      bool *is_unsigned);

void sqlErrorWithMsg(sql *, int, const char *, ...);
void sqlError(sql *, int);
void sqlSystemError(sql *, int);
void *sqlHexToBlob(sql *, const char *z, int n);
u8 sqlHexToInt(int h);

const char *sqlErrStr(int);

/**
 * Return the collation sequence for the expression pExpr. If
 * there is no defined collating sequence, return NULL.
 *
 * The collating sequence might be determined by a COLLATE operator
 * or by the presence of a column with a defined collating sequence.
 * COLLATE operators take first precedence.  Left operands take
 * precedence over right operands.
 *
 * @param parse Parsing context.
 * @param expr Expression to scan.
 * @param[out] is_explicit_coll Flag set if explicit COLLATE
 *             clause is used.
 * @param[out] coll_id Collation identifier.
 * @param[out] coll Collation object.
 *
 * @retval 0 on success.
 * @retval -1 in case of error.
 */
int
sql_expr_coll(Parse *parse, Expr *p, bool *is_explicit_coll, uint32_t *coll_id,
	      struct coll **coll);

Expr *sqlExprAddCollateToken(Parse * pParse, Expr *, const Token *, int);
Expr *sqlExprAddCollateString(Parse *, Expr *, const char *);
Expr *sqlExprSkipCollate(Expr *);
int sqlCheckIdentifierName(Parse *, char *);
void sqlVdbeSetChanges(sql *, int);

enum arithmetic_result {
	/* The result fits the signed 64-bit integer */
	ATHR_SIGNED,
	/* The result is positive and fits the
	 * unsigned 64-bit integer
	 */
	ATHR_UNSIGNED,
	/* The operation causes an overflow */
	ATHR_OVERFLOW,
	/* The operation causes division by zero */
	ATHR_DIVBYZERO
};

enum arithmetic_result
sqlAddInt64(i64 *, bool, i64, bool);
enum arithmetic_result
sqlSubInt64(i64 *, bool, i64, bool);
enum arithmetic_result
sqlMulInt64(i64 *, bool, i64, bool);
enum arithmetic_result
sqlDivInt64(i64 *, bool, i64, bool);
enum arithmetic_result
sqlRemInt64(i64 *, bool, i64, bool);

int sqlAbsInt32(int);
u8 sqlGetBoolean(const char *z, u8);

const void *sqlValueText(sql_value *);
int sqlValueBytes(sql_value *);
void sqlValueSetStr(sql_value *, int, const void *,
			void (*)(void *));
void sqlValueSetNull(sql_value *);
void sqlValueFree(sql_value *);
sql_value *sqlValueNew(sql *);
int sqlValueFromExpr(sql *, Expr *, enum field_type type,
			 sql_value **);
void sql_value_apply_type(sql_value *val, enum field_type type);
#ifndef SQL_AMALGAMATION
extern const unsigned char sqlOpcodeProperty[];
extern const char sqlStrBINARY[];
extern const unsigned char sqlUpperToLower[];
extern const unsigned char sqlCtypeMap[];
extern const Token sqlIntTokens[];
extern SQL_WSD struct sqlConfig sqlConfig;
extern FuncDefHash sqlBuiltinFunctions;
#ifndef SQL_OMIT_WSD
extern int sqlPendingByte;
#endif
#endif

/**
 * Generate code to implement the "ALTER TABLE xxx RENAME TO yyy"
 * command.
 *
 * @param parse Current parsing context.
 * @param src_tab The table to rename.
 * @param new_name_tk Token containing new name of the table.
 */
void
sql_alter_table_rename(struct Parse *parse, struct SrcList *src_tab,
		       struct Token *new_name_tk);

/**
 * Return the length (in bytes) of the token that begins at z[0].
 * Store the token type in *type before returning.
 *
 * @param z Input stream.
 * @param[out] type Detected type of token.
 * @param[out] is_reserved True if reserved word.
 */
int
sql_token(const char *z, int *type, bool *is_reserved);

void sqlExpirePreparedStatements(sql *);
int sqlCodeSubselect(Parse *, Expr *, int);
void sqlSelectPrep(Parse *, Select *, NameContext *);

/**
 * Error message for when two or more terms of a compound select
 * have different size result sets.
 *
 * @param parse Parsing context.
 * @param p Select struct to analyze.
 */
void
sqlSelectWrongNumTermsError(struct Parse *parse, struct Select *p);

int sqlMatchSpanName(const char *, const char *, const char *);
int sqlResolveExprNames(NameContext *, Expr *);
int sqlResolveExprListNames(NameContext *, ExprList *);
void sqlResolveSelectNames(Parse *, Select *, NameContext *);
int sqlResolveOrderGroupBy(Parse *, Select *, ExprList *, const char *);

/**
 * Generate code for default value.
 * The most recently coded instruction was an OP_Column to retrieve the
 * i-th column of table defined by space_def. This routine sets
 * the P4 parameter of the OP_Column to the default value, if any.
 *
 * The default value of a column is specified by a DEFAULT clause in the
 * column definition. This was either supplied by the user when the table
 * was created, or added later to the table definition by an ALTER TABLE
 * command. If the latter, then the row-records in the table btree on disk
 * may not contain a value for the column and the default value, taken
 * from the P4 parameter of the OP_Column instruction, is returned instead.
 * If the former, then all row-records are guaranteed to include a value
 * for the column and the P4 value is not required.
 *
 * Column definitions created by an ALTER TABLE command may only have
 * literal default values specified: a number, null or a string. (If a more
 * complicated default expression value was provided, it is evaluated
 * when the ALTER TABLE is executed and one of the literal values written
 * into the schema.)
 *
 * Therefore, the P4 parameter is only required if the default value for
 * the column is a literal number, string or null. The sqlValueFromExpr()
 * function is capable of transforming these types of expressions into
 * sql_value objects.
 *
 * If parameter iReg is not negative, code an OP_Realify instruction
 * on register iReg. This is used when an equivalent integer value is
 * stored in place of an 8-byte floating point value in order to save
 * space.
 * @param v Vdbe object.
 * @param def space definition object.
 * @param i column index.
 * @param iReg register index.
 */
void
sqlColumnDefault(Vdbe *v, struct space_def *def, int i, int ireg);

char* rename_trigger(sql *, char const *, char const *, bool *);
/**
 * Find a collation by name. Set error in @a parser if not found.
 * @param parser Parser.
 * @param name Collation name.
 * @param[out] Collation identifier.
 *
 * @retval Collation object. NULL on error or not found.
 */
struct coll *
sql_get_coll_seq(Parse *parser, const char *name, uint32_t *coll_id);
void sqlAnalyze(Parse *, Token *);

/**
 * This function returns average size of tuple in given index.
 * Currently, all indexes from one space feature the same size,
 * due to the absence of partial indexes.
 *
 * @param space Index belongs to this space.
 * @param idx Index to be examined.
 * @retval Average size of tuple in given index.
 */
ssize_t
sql_index_tuple_size(struct space *space, struct index *idx);

/**
 * Load the content of the _sql_stat1 and sql_stat4 tables. The
 * contents of _sql_stat1 are used to populate the tuple_stat1[]
 * arrays. The contents of sql_stat4 are used to populate the
 * samples[] arrays.
 *
 * @param db Database handler.
 * @retval sql_OK on success, smth else otherwise.
 */
int
sql_analysis_load(struct sql *db);

/**
 * An instance of the following structure controls how keys
 * are compared by VDBE, see P4_KEYINFO.
 */
struct sql_key_info {
	sql *db;
	/**
	 * Key definition created from this object,
	 * see sql_key_info_to_key_def().
	 */
	struct key_def *key_def;
	/** Reference counter. */
	uint32_t refs;
	/** Number of parts in the key. */
	uint32_t part_count;
	/** Definition of the key parts. */
	struct key_part_def parts[];
};

/**
 * Allocate a key_info object sufficient for an index with
 * the given number of key columns.
 */
struct sql_key_info *
sql_key_info_new(sql *db, uint32_t part_count);

/**
 * Allocate a key_info object from the given key definition.
 */
struct sql_key_info *
sql_key_info_new_from_key_def(sql *db, const struct key_def *key_def);

/**
 * Increment the reference counter of a key_info object.
 */
struct sql_key_info *
sql_key_info_ref(struct sql_key_info *key_info);

/**
 * Decrement the reference counter of a key_info object and
 * free memory if the object isn't referenced anymore.
 */
void
sql_key_info_unref(struct sql_key_info *key_info);

/**
 * Create a key definition from a key_info object.
 * The new key definition is cached in key_info struct
 * so that subsequent calls to this function are free.
 */
struct key_def *
sql_key_info_to_key_def(struct sql_key_info *key_info);

void sqlRegisterLikeFunctions(sql *, int);

/**
 * Check if the function implements LIKE-style comparison & if it
 * is appropriate to apply a LIKE query optimization.
 *
 * @param db database structure.
 * @param pExpr pointer to a function-implementing expression.
 * @param[out] is_like_ci true if LIKE is case insensitive.
 *
 * @retval 1 if LIKE optimization can be used, 0 otherwise.
 */
int
sql_is_like_func(struct sql *db, struct Expr *expr, int *is_like_ci);

int sqlCreateFunc(sql *, const char *, enum field_type,
		      int, int, void *,
		      void (*)(sql_context *, int, sql_value **),
		      void (*)(sql_context *, int, sql_value **),
		      void (*)(sql_context *),
		      FuncDestructor * pDestructor);
void sqlOomFault(sql *);
void sqlOomClear(sql *);
int sqlApiExit(sql * db, int);

void sqlStrAccumInit(StrAccum *, sql *, char *, int, int);
void sqlStrAccumAppend(StrAccum *, const char *, int);
void sqlStrAccumAppendAll(StrAccum *, const char *);
void sqlAppendChar(StrAccum *, int, char);
char *sqlStrAccumFinish(StrAccum *);
void sqlStrAccumReset(StrAccum *);
void sqlSelectDestInit(SelectDest *, int, int, int);
Expr *sqlCreateColumnExpr(sql *, SrcList *, int, int);

int sqlExprCheckIN(Parse *, Expr *);

void sqlAnalyzeFunctions(void);
int sqlStat4ProbeSetValue(Parse *, struct index_def *, UnpackedRecord **, Expr *, int,
			      int, int *);
int sqlStat4ValueFromExpr(Parse *, Expr *, enum field_type type,
			      sql_value **);
void sqlStat4ProbeFree(UnpackedRecord *);

/**
 * Extract the col_num-th column from the record.  Write
 * the column value into *res.  If *res is initially NULL
 * then a new sql_value object is allocated.
 *
 * If *res is initially NULL then the caller is responsible for
 * ensuring that the value written into *res is eventually
 * freed.
 *
 * @param db Database handle.
 * @param record Pointer to buffer containing record.
 * @param col_num Column to extract.
 * @param[out] res Extracted value.
 *
 * @retval -1 on error or 0.
 */
int
sql_stat4_column(struct sql *db, const char *record, uint32_t col_num,
		 sql_value **res);

/*
 * The interface to the LEMON-generated parser
 */
void *sqlParserAlloc(void *(*)(u64));
void sqlParserFree(void *, void (*)(void *));
void sqlParser(void *, int, Token, Parse *);
#ifdef YYTRACKMAXSTACKDEPTH
int sqlParserStackPeak(void *);
#endif

void sqlInvalidFunction(sql_context *, int, sql_value **);
sql_int64 sqlStmtCurrentTime(sql_context *);
int sqlVdbeParameterIndex(Vdbe *, const char *, int);
int sqlTransferBindings(sql_stmt *, sql_stmt *);
int sqlReprepare(Vdbe *);
void sqlExprListCheckLength(Parse *, ExprList *, const char *);

/**
 * This function verifies that two collations (to be more precise
 * their ids) are compatible. In terms of SQL ANSI they are
 * compatible if:
 *  - one of collations is mentioned alongside with explicit
 *    COLLATE clause, which forces this collation over another
 *    one. It is allowed to have the same forced collations;
 * - both collations are derived from table columns and they
 *   are the same;
 * - one collation is derived from table column and another
 *   one is not specified (i.e. COLL_NONE);
 * In all other cases they are not accounted to be compatible
 * and error should be raised.
 * Collation to be used in comparison operator is returned
 * via @res_id: in case one of collations is absent, then
 * the second one is utilized.
 */
int
collations_check_compatibility(uint32_t lhs_id, bool is_lhs_forced,
			       uint32_t rhs_id, bool is_rhs_forced,
			       uint32_t *res_id);

/**
 * Return a pointer to the collation sequence that should be used
 * by a binary comparison operator comparing left and right.
 *
 * If the left hand expression has a collating sequence type, then
 * it is used. Otherwise the collation sequence for the right hand
 * expression is used, or the default (BINARY) if neither
 * expression has a collating type.
 *
 * Argument right (but not left) may be a null pointer. In this
 * case, it is not considered.
 * @param parser Parser.
 * @param left Left expression.
 * @param right Right expression. Can be NULL.
 * @param[out] id Id of resulting collation.
 *
 * @retval 0 on success.
 * @retval -1 in case of error (e.g. no collation found).
 */
int
sql_binary_compare_coll_seq(Parse *parser, Expr *left, Expr *right,
			    uint32_t *id);
int sqlTempInMemory(const sql *);
#ifndef SQL_OMIT_CTE
With *sqlWithAdd(Parse *, With *, Token *, ExprList *, Select *);
void sqlWithDelete(sql *, With *);
void sqlWithPush(Parse *, With *, u8);
#else
#define sqlWithPush(x,y,z)
#define sqlWithDelete(x,y)
#endif

/*
 * This function is called when inserting, deleting or updating a
 * row of table tab to generate VDBE code to perform foreign key
 * constraint processing for the operation.
 *
 * For a DELETE operation, parameter reg_old is passed the index
 * of the first register in an array of (tab->def->field_count +
 * 1) registers containing the PK of the row being deleted,
 * followed by each of the column values of the row being deleted,
 * from left to right. Parameter reg_new is passed zero in this
 * case.
 *
 * For an INSERT operation, reg_old is passed zero and reg_new is
 * passed the first register of an array of
 * (tab->def->field_count + 1) registers containing the new row
 * data.
 *
 * For an UPDATE operation, this function is called twice. Once
 * before the original record is deleted from the table using the
 * calling convention described for DELETE. Then again after the
 * original record is deleted but before the new record is
 * inserted using the INSERT convention.
 *
 * @param parser SQL parser.
 * @param space Space from which the row is deleted.
 * @param reg_old Register with deleted row.
 * @param reg_new Register with inserted row.
 * @param changed_cols Array of updated columns. Can be NULL.
 */
void
fk_constraint_emit_check(struct Parse *parser, struct space *space, int reg_old,
		int reg_new, const int *changed_cols);

/**
 * Emit VDBE code to do CASCADE, SET NULL or SET DEFAULT actions
 * when deleting or updating a row.
 * @param parser SQL parser.
 * @param space Space being updated or deleted from.
 * @param reg_old Register of the old record.
 * param changes Array of numbers of changed columns.
 */
void
fk_constraint_emit_actions(struct Parse *parser, struct space *space, int reg_old,
		  const int *changes);

/**
 * This function is called before generating code to update or
 * delete a row contained in given space. If the operation is
 * a DELETE, then parameter changes is passed a NULL value.
 * For an UPDATE, changes points to an array of size N, where N
 * is the number of columns in table. If the i'th column is not
 * modified by the UPDATE, then the corresponding entry in the
 * changes[] array is set to -1. If the column is modified,
 * the value is 0 or greater.
 *
 * @param space Space to be modified.
 * @param changes Array of modified fields for UPDATE.
 * @retval True, if any foreign key processing will be required.
 */
bool
fk_constraint_is_required(struct space *space, const int *changes);

/*
 * Available fault injectors.  Should be numbered beginning with 0.
 */
#define SQL_FAULTINJECTOR_MALLOC     0
#define SQL_FAULTINJECTOR_COUNT      1

/*
 * The interface to the code in fault.c used for identifying "benign"
 * malloc failures. This is only present if sql_UNTESTABLE
 * is not defined.
 */
#ifndef SQL_UNTESTABLE
void sqlBeginBenignMalloc(void);
void sqlEndBenignMalloc(void);
#else
#define sqlBeginBenignMalloc()
#define sqlEndBenignMalloc()
#endif

/*
 * Allowed return values from sqlFindInIndex()
 */
#define IN_INDEX_EPH          2	/* Search an ephemeral b-tree */
#define IN_INDEX_INDEX_ASC    3	/* Existing index ASCENDING */
#define IN_INDEX_INDEX_DESC   4	/* Existing index DESCENDING */
#define IN_INDEX_NOOP         5	/* No table available. Use comparisons */
/*
 * Allowed flags for the 3rd parameter to sqlFindInIndex().
 */
#define IN_INDEX_NOOP_OK     0x0001	/* OK to return IN_INDEX_NOOP */
#define IN_INDEX_MEMBERSHIP  0x0002	/* IN operator used for membership test */
#define IN_INDEX_LOOP        0x0004	/* IN operator used as a loop */
int sqlFindInIndex(Parse *, Expr *, u32, int *, int *, int *);

void sqlExprSetHeightAndFlags(Parse * pParse, Expr * p);
#if SQL_MAX_EXPR_DEPTH>0
int sqlSelectExprHeight(Select *);
int sqlExprCheckHeight(Parse *, int);
#else
#define sqlSelectExprHeight(x) 0
#define sqlExprCheckHeight(x,y)
#endif

u32 sqlGet4byte(const u8 *);
void sqlPut4byte(u8 *, u32);

#ifdef SQL_DEBUG
void sqlParserTrace(FILE *, char *);
#endif

/*
 * If the sql_ENABLE IOTRACE exists then the global variable
 * sqlIoTrace is a pointer to a printf-like routine used to
 * print I/O tracing messages.
 */
#ifdef SQL_ENABLE_IOTRACE
#define IOTRACE(A)  if( sqlIoTrace ){ sqlIoTrace A; }
void sqlVdbeIOTraceSql(Vdbe *);
 SQL_EXTERN void (SQL_CDECL * sqlIoTrace) (const char *,
							       ...);
#else
#define IOTRACE(A)
#define sqlVdbeIOTraceSql(X)
#endif

/*
 * These routines are available for the mem2.c debugging memory allocator
 * only.  They are used to verify that different "types" of memory
 * allocations are properly tracked by the system.
 *
 * sqlMemdebugSetType() sets the "type" of an allocation to one of
 * the MEMTYPE_* macros defined below.  The type must be a bitmask with
 * a single bit set.
 *
 * sqlMemdebugHasType() returns true if any of the bits in its second
 * argument match the type set by the previous sqlMemdebugSetType().
 * sqlMemdebugHasType() is intended for use inside assert() statements.
 *
 * sqlMemdebugNoType() returns true if none of the bits in its second
 * argument match the type set by the previous sqlMemdebugSetType().
 *
 * Perhaps the most important point is the difference between MEMTYPE_HEAP
 * and MEMTYPE_LOOKASIDE.  If an allocation is MEMTYPE_LOOKASIDE, that means
 * it might have been allocated by lookaside, except the allocation was
 * too large or lookaside was already full.  It is important to verify
 * that allocations that might have been satisfied by lookaside are not
 * passed back to non-lookaside free() routines.  Asserts such as the
 * example above are placed on the non-lookaside free() routines to verify
 * this constraint.
 *
 * All of this is no-op for a production build.  It only comes into
 * play when the sql_MEMDEBUG compile-time option is used.
 */
#ifdef SQL_MEMDEBUG
void sqlMemdebugSetType(void *, u8);
int sqlMemdebugHasType(void *, u8);
int sqlMemdebugNoType(void *, u8);
#else
#define sqlMemdebugSetType(X,Y)	/* no-op */
#define sqlMemdebugHasType(X,Y)  1
#define sqlMemdebugNoType(X,Y)   1
#endif
#define MEMTYPE_HEAP       0x01	/* General heap allocations */
#define MEMTYPE_LOOKASIDE  0x02	/* Heap that might have been lookaside */
#define MEMTYPE_SCRATCH    0x04	/* Scratch allocations */
#define MEMTYPE_PCACHE     0x08	/* Page cache allocations */

/*
 * Threading interface
 */
#if SQL_MAX_WORKER_THREADS>0
int sqlThreadCreate(sqlThread **, void *(*)(void *), void *);
int sqlThreadJoin(sqlThread *, void **);
#endif

int sqlExprVectorSize(Expr * pExpr);
int sqlExprIsVector(Expr * pExpr);
Expr *sqlVectorFieldSubexpr(Expr *, int);
Expr *sqlExprForVectorField(Parse *, Expr *, int);
void sqlVectorErrorMsg(Parse *, Expr *);

/* Tarantool: right now query compilation is invoked on top of
 * fiber's stack. Need to limit number of nested programs under
 * compilation to avoid stack overflow.
 */
extern int sqlSubProgramsRemaining;

/**
 * Generate VDBE code to halt execution with correct error if
 * the object with specified key is already present (or doesn't
 * present - configure with cond_opcodeq) in specified space.
 * The function allocates error and name resources for VDBE
 * itself.
 *
 * @param parser Parsing context.
 * @param space_id Space to lookup identifier.
 * @param index_id Index identifier of key.
 * @param key_reg Register where key to be found is held.
 * @param key_len Lenght of key (number of resiters).
 * @param tarantool_error_code to set on halt.
 * @param error_src Error message to display on VDBE halt.
 * @param no_error Do not raise error flag.
 * @param cond_opcode Condition to raise - OP_NoConflict or
 *        OP_Found.
 *
 * @retval -1 on memory allocation error.
 * @retval 0 on success.
 */
int
vdbe_emit_halt_with_presence_test(struct Parse *parser, int space_id,
				  int index_id, int key_reg, uint32_t key_len,
				  int tarantool_error_code,
				  const char *error_src, bool no_error,
				  int cond_opcode);

/**
 * Generate VDBE code to delete records from system _sql_stat1 or
 * _sql_stat4 table.
 *
 * @param parse The parsing context.
 * @param stat_table_name System stat table name.
 * @param idx_name Index name.
 * @param table_name Table name.
 */
void
vdbe_emit_stat_space_clear(struct Parse *parse, const char *stat_table_name,
			   const char *idx_name, const char *table_name);

#endif				/* sqlINT_H */
