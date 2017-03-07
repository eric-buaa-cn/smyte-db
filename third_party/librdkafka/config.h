// Automatically generated by ./configure
#pragma once
#define ARCH "x86_64"
#define CPU "generic"
#define WITHOUT_OPTIMIZATION 0
#define ENABLE_DEVEL 0
#define ENABLE_VALGRIND 0
#define ENABLE_REFCNT_DEBUG 0
#define ENABLE_SHAREDPTR_DEBUG 0
#define ENABLE_SSL 1
#define ENABLE_SASL 1
#define MKL_APP_NAME "librdkafka"
#define MKL_APP_DESC_ONELINE "The Apache Kafka C/C++ library"
// gcc
#define WITH_GCC 1
// gxx
#define WITH_GXX 1
// pkgconfig
#define WITH_PKGCONFIG 1
// install
#define WITH_INSTALL 1
// PIC
#define HAVE_PIC 1
// gnulib
#define WITH_GNULD 1
// __atomic_32
#define HAVE_ATOMICS_32 1
// __atomic_32
#define HAVE_ATOMICS_32_ATOMIC 1
// atomic_32
#define ATOMIC_OP32(OP1,OP2,PTR,VAL) __atomic_ ## OP1 ## _ ## OP2(PTR, VAL, __ATOMIC_SEQ_CST)
// __atomic_64
#define HAVE_ATOMICS_64 1
// __atomic_64
#define HAVE_ATOMICS_64_ATOMIC 1
// atomic_64
#define ATOMIC_OP64(OP1,OP2,PTR,VAL) __atomic_ ## OP1 ## _ ## OP2(PTR, VAL, __ATOMIC_SEQ_CST)
// atomic_64
#define ATOMIC_OP(OP1,OP2,PTR,VAL) __atomic_ ## OP1 ## _ ## OP2(PTR, VAL, __ATOMIC_SEQ_CST)
// parseversion
#define RDKAFKA_VERSION_STR "0.9.4"
// parseversion
#define MKL_APP_VERSION "0.9.4"
// zlib
#define WITH_ZLIB 1
// WITH_SNAPPY
#define WITH_SNAPPY 1
// WITH_SOCKEM
#define WITH_SOCKEM 1
// libssl
#define WITH_SSL 1
// regex
#define HAVE_REGEX 1
// strndup
#define HAVE_STRNDUP 1
// python
#define HAVE_PYTHON 1
