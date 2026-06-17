
#ifndef SQLITEX_EXPORT_H
#define SQLITEX_EXPORT_H

#ifdef SQLITEX_STATIC
#  define SQLITEX_EXPORT
#  define SQLITEX_NO_EXPORT
#else
#  ifndef SQLITEX_EXPORT
#    ifdef sqlitex_EXPORTS
        /* We are building this library */
#      define SQLITEX_EXPORT 
#    else
        /* We are using this library */
#      define SQLITEX_EXPORT 
#    endif
#  endif

#  ifndef SQLITEX_NO_EXPORT
#    define SQLITEX_NO_EXPORT 
#  endif
#endif

#ifndef SQLITEX_DEPRECATED
#  define SQLITEX_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef SQLITEX_DEPRECATED_EXPORT
#  define SQLITEX_DEPRECATED_EXPORT SQLITEX_EXPORT SQLITEX_DEPRECATED
#endif

#ifndef SQLITEX_DEPRECATED_NO_EXPORT
#  define SQLITEX_DEPRECATED_NO_EXPORT SQLITEX_NO_EXPORT SQLITEX_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef SQLITEX_NO_DEPRECATED
#    define SQLITEX_NO_DEPRECATED
#  endif
#endif

#endif /* SQLITEX_EXPORT_H */
