

#ifndef S_TEST_BESTEST_H
#define S_TEST_BESTEST_H


#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus 
extern "C" {
#endif


#if !defined(BESTEST_THREAD_LOCAL) && __STDC_VERSION__ >= 201112
#include <threads.h>
#define BESTEST_THREAD_LOCAL thread_local
#endif


#ifndef BESTEST_CODE_SECTION
#define BESTEST_CODE_SECTION(s)  __attribute__((section(s)))
#endif

#ifndef BESTEST_WEAK
#define BESTEST_WEAK __attribute__((weak))
#endif

#define BESTEST_LOG_SUCCESS               1
#define BESTEST_LOG_DURATION              2
#define BESTEST_ERROR_MESSAGE_MAX_LENGTH  256

typedef void (*bestest_UnitTest)(void*);
typedef void* (*bestest_Setup)(void);
typedef void (*bestest_TearDown)(void*);

typedef struct bestest_Result {
    size_t numErrors;
} bestest_Result;

struct bestest_Test  {
    const char *domain;
    const char *name;
    bestest_UnitTest test;
    bestest_Setup *setup;
    bestest_TearDown *teardown;
};

struct bestest_Context {
    bestest_Result result;
    const struct bestest_Test **startOfTests;
    const struct bestest_Test **endOfTests;
    struct bestest_Options {
        int verbosity;
    } opts;
};

struct bestest_UnitUnderTest {
    jmp_buf exitPoint;
    const char *funcName;
    const char *error;
    size_t line;
};

/*
 * Assertions 
 */
#define bestest_Assert(expr)  bestest_Assert_(expr, __func__, __LINE__, #expr)
#define bestest_AssertMemory(p1, p2, size) bestest_AssertMemory_(p1, p2, size, __func__, __LINE__, #p1 " == " #p2)

#define bestest_AssertEqual(v1, v2)            bestest_AssertEqual_(v1, v2)         
#define bestest_AssertNotEqual(v1, v2)         bestest_AssertNotEqual_(v1, v2)      
#define bestest_AssertGreaterOrEqual(v1, v2)   bestest_AssertGreaterOrEqual_(v1, v2)
#define bestest_AssertGreaterThan(v1, v2)      bestest_AssertGreaterThan_(v1, v2)   
#define bestest_AssertLessOrEqual(v1, v2)      bestest_AssertLessOrEqual_(v1, v2)   
#define bestest_AssertLessThan(v1, v2)         bestest_AssertLessThan_(v1, v2)      


/*
 * API
 */
#define bestest_CreateGlobalContext(options)   \
    extern const struct bestest_Test *__start_bestest_tests; \
    extern const struct bestest_Test *__stop_bestest_tests; \
    struct bestest_Context bestest_context = { .opts.verbosity = options, .startOfTests = &__start_bestest_tests, .endOfTests = &__stop_bestest_tests }; \
    BESTEST_THREAD_LOCAL struct bestest_UnitUnderTest bestest_uut;


#define BESTEST_TEST(domain, name) \
    BESTEST_CODE_SECTION(".utest_functionpath") static const char* domain##_##name##path = #domain "::" #name; \
    void domain##_##name(void *context); \
    BESTEST_WEAK extern bestest_Setup domain##_setupPtr; \
    BESTEST_WEAK extern bestest_TearDown domain##_teardownPtr; \
    const struct bestest_Test domain##_##name##_Meta = { #domain, #name, &domain##_##name, &domain##_setupPtr, &domain##_teardownPtr }; \
    BESTEST_CODE_SECTION("bestest_tests") const struct bestest_Test *domain##_##name##_MetaPtr = &domain##_##name##_Meta; \
    void domain##_##name(void *context)

#define BESTEST_SETUP(domain)       void *domain##_setup(void); bestest_Setup domain##_setupPtr = &domain##_setup; BESTEST_CODE_SECTION("bestest_setups")      void *domain##_setup(void)
#define BESTEST_TEARDOWN(domain)    void domain##_teardown(void*); bestest_TearDown domain##_teardownPtr = &domain##_teardown; BESTEST_CODE_SECTION("bestest_teardowns")   void domain##_teardown(void *context)

static inline bestest_Result bestest_Run(int argc, char *argv[]);

static inline void bestest_SetVerbosity(int level);

/*
 * User implemented
 */
void bestest_LogResult(const char *msg);
unsigned long long bestest_Time(void);




/*
 * Implementation
 */

extern struct bestest_Context bestest_context;
extern thread_local struct bestest_UnitUnderTest bestest_uut;

static inline void bestest_RunTest(const struct bestest_Test *test)
{
static char logMsg[BESTEST_ERROR_MESSAGE_MAX_LENGTH];
    unsigned long long endTime, startTime = bestest_Time();
    int offset = 0;

    void *testContext = test->setup ? (*test->setup)() : NULL;
    if (setjmp(bestest_uut.exitPoint) == 0) {
        test->test(testContext);
        endTime = bestest_Time();
        if (bestest_context.opts.verbosity & BESTEST_LOG_SUCCESS) {
            offset = snprintf(logMsg, sizeof(logMsg), "[SUCCESS] in %s::%s", test->domain, bestest_uut.funcName);
        }
        else
            logMsg[0] = 0;
    }
    else {
        endTime = bestest_Time();
        bestest_context.result.numErrors++;
        offset = snprintf(logMsg, sizeof(logMsg), "[ERROR  ] in %s::%s:%zu => %s", test->domain, bestest_uut.funcName, bestest_uut.line, bestest_uut.error);
    }
    if (bestest_context.opts.verbosity & BESTEST_LOG_DURATION)
        snprintf(logMsg + offset, sizeof logMsg - offset, " (duration: %.3f)", ((double)endTime - (double)startTime) / 1000.0);
    if (test->teardown)
        (*test->teardown)(testContext);


    if (logMsg[0])
        bestest_LogResult(logMsg);
}

static inline bool bestest_TestInList(const char *list, int len, const struct bestest_Test *test)
{
    const char *entry = list;
    for (int i = 0; i < len; i++, entry += strlen(entry) + 1) {
        static BESTEST_THREAD_LOCAL char fullName[BESTEST_ERROR_MESSAGE_MAX_LENGTH];
        snprintf(fullName, sizeof fullName, "%s::%s", test->domain, test->name);
        if (!strncmp(entry, fullName, strlen(entry)))
            return true;
    }
    return false;
}

__attribute__((constructor(0xFFFF)))
static inline bestest_Result bestest_Run(int argc, char* argv[])
{
    struct {
        char *list;
        int len;
    } testList = {}, ignoreList = {};
    if (argc > 1) {
        testList.list = argv[1];
        testList.len = 1;
        for (char* p = testList.list; *p; p++)
            if (*p == ',')
                *p = '\0', testList.len++;
    }
    if (argc > 2) {
        ignoreList.list = argv[1];
        ignoreList.len = 1;
        for (char* p = ignoreList.list; *p; p++)
            if (*p == ',')
                *p = '\0', ignoreList.len++;
    }
    ptrdiff_t numTests = bestest_context.endOfTests - bestest_context.startOfTests;
    for (const struct bestest_Test **test = bestest_context.startOfTests; test < bestest_context.endOfTests; test++) {
        const struct bestest_Test *uut = *test;
        if (ignoreList.list && bestest_TestInList(ignoreList.list, ignoreList.len, uut))
            continue;
        if (!testList.list || bestest_TestInList(testList.list, testList.len, uut))
            bestest_RunTest(uut);
    }

    exit(bestest_context.result.numErrors);
}


static inline void bestest_Assert_(bool expr, const char *funcName, size_t line, const char *exprText)
{
    bestest_uut.funcName = funcName;
    if (!expr) {
        bestest_uut.error = exprText;
        bestest_uut.line = line;
        longjmp(bestest_uut.exitPoint, 1);
    }
}

static inline void bestest_AssertMemory_(void *p1, void *p2, size_t size, const char *funcName, size_t line, const char *exprText)
{
    bestest_uut.funcName = funcName;
    if (memcmp(p1, p2, size)) {
        bestest_uut.error = exprText;
        bestest_uut.line = line;
        longjmp(bestest_uut.exitPoint, 1);
    }
}

static inline void bestest_SetVerbosity(int level)
{
    bestest_context.opts.verbosity = (level < 0) ? 0 : level;
}





#if __STDC_VERSION__ >= 201112
#define bestest_AssertEqual_(v1, v2)   _Generic((v1),  short:                  bestest_AssertEQShort,      \
                                                    int:                    bestest_AssertEQInt,        \
                                                    long:                   bestest_AssertEQLong,       \
                                                    long long:              bestest_AssertEQLongLong,   \
                                                    unsigned short:         bestest_AssertEQShort,      \
                                                    unsigned int:           bestest_AssertEQInt,        \
                                                    unsigned long:          bestest_AssertEQLong,       \
                                                    unsigned long long:     bestest_AssertEQLongLong,   \
                                                    float:                  bestest_AssertEQFloat,      \
                                                    double:                 bestest_AssertEQDouble,     \
                                                    long double:            bestest_AssertEQLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)

#define bestest_AssertNotEqual_(v1, v2)   _Generic((v1),   short:                  bestest_AssertNEQShort,      \
                                                        int:                    bestest_AssertNEQInt,        \
                                                        long:                   bestest_AssertNEQLong,       \
                                                        long long:              bestest_AssertNEQLongLong,   \
                                                        unsigned short:         bestest_AssertNEQShort,      \
                                                        unsigned int:           bestest_AssertNEQInt,        \
                                                        unsigned long:          bestest_AssertNEQLong,       \
                                                        unsigned long long:     bestest_AssertNEQLongLong,   \
                                                        float:                  bestest_AssertNEQFloat,      \
                                                        double:                 bestest_AssertNEQDouble,     \
                                                        long double:            bestest_AssertNEQLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)

#define bestest_AssertGreaterOrEqual_(v1, v2) _Generic((v1),   short:                  bestest_AssertGEQShort,      \
                                                            int:                    bestest_AssertGEQInt,        \
                                                            long:                   bestest_AssertGEQLong,       \
                                                            long long:              bestest_AssertGEQLongLong,   \
                                                            unsigned short:         bestest_AssertGEQShort,      \
                                                            unsigned int:           bestest_AssertGEQInt,        \
                                                            unsigned long:          bestest_AssertGEQLong,       \
                                                            unsigned long long:     bestest_AssertGEQLongLong,   \
                                                            float:                  bestest_AssertGEQFloat,      \
                                                            double:                 bestest_AssertGEQDouble,     \
                                                            long double:            bestest_AssertGEQLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)

#define bestest_AssertGreaterThan_(v1, v2)   _Generic((v1),    short:                  bestest_AssertGTShort,      \
                                                            int:                    bestest_AssertGTInt,        \
                                                            long:                   bestest_AssertGTLong,       \
                                                            long long:              bestest_AssertGTLongLong,   \
                                                            unsigned short:         bestest_AssertGTShort,      \
                                                            unsigned int:           bestest_AssertGTInt,        \
                                                            unsigned long:          bestest_AssertGTLong,       \
                                                            unsigned long long:     bestest_AssertGTLongLong,   \
                                                            float:                  bestest_AssertGTFloat,      \
                                                            double:                 bestest_AssertGTDouble,     \
                                                            long double:            bestest_AssertGTLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)

#define bestest_AssertLessOrEqual_(v1, v2)   _Generic((v1),    short:                  bestest_AssertLEQShort,      \
                                                            int:                    bestest_AssertLEQInt,        \
                                                            long:                   bestest_AssertLEQLong,       \
                                                            long long:              bestest_AssertLEQLongLong,   \
                                                            unsigned short:         bestest_AssertLEQShort,      \
                                                            unsigned int:           bestest_AssertLEQInt,        \
                                                            unsigned long:          bestest_AssertLEQLong,       \
                                                            unsigned long long:     bestest_AssertLEQLongLong,   \
                                                            float:                  bestest_AssertLEQFloat,      \
                                                            double:                 bestest_AssertLEQDouble,     \
                                                            long double:            bestest_AssertLEQLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)

#define bestest_AssertLessThan_(v1, v2)   _Generic((v1),   short:                  bestest_AssertLTShort,      \
                                                        int:                    bestest_AssertLTInt,        \
                                                        long:                   bestest_AssertLTLong,       \
                                                        long long:              bestest_AssertLTLongLong,   \
                                                        unsigned short:         bestest_AssertLTShort,      \
                                                        unsigned int:           bestest_AssertLTInt,        \
                                                        unsigned long:          bestest_AssertLTLong,       \
                                                        unsigned long long:     bestest_AssertLTLongLong,   \
                                                        float:                  bestest_AssertLTFloat,      \
                                                        double:                 bestest_AssertLTDouble,     \
                                                        long double:            bestest_AssertLTLongDouble)(v1, v2, #v1, #v2, __LINE__, __func__)
#else
#define bestest_AssertEqual_(v1, v2)            bestest_Assert(v1 == v2)
#define bestest_AssertNotEqual_(v1, v2)         bestest_Assert(v1 != v2)
#define bestest_AssertGreaterOrEqual_(v1, v2)   bestest_Assert(v1 >= v2)
#define bestest_AssertGreaterThan_(v1, v2)      bestest_Assert(v1 >  v2)
#define bestest_AssertLessOrEqual_(v1, v2)      bestest_Assert(v1 <= v2)
#define bestest_AssertLessThan_(v1, v2)         bestest_Assert(v1 <  v2)
#endif





#define BESTEST_ASSERTION(assertion, name, type, fmt) \
    static void bestest_Assert##assertion##name(type v1, type v2, const char *v1Name, const char *v2Name, size_t line, const char *funcName) \
    {                                                                                   \
        bestest_uut.funcName = funcName;                                          \
        if (!(v1 BESTEST_ASSERTION_TYPE_##assertion v2)) {                                \
            static BESTEST_THREAD_LOCAL char msg[BESTEST_ERROR_MESSAGE_MAX_LENGTH];     \
            sprintf(msg, "%s == %s => " fmt " != " fmt, v1Name, v2Name, v1, v2);        \
            bestest_uut.error = msg;                                              \
            bestest_uut.line = line;                                              \
            longjmp(bestest_uut.exitPoint, 1);                                    \
        }                                                                               \
    }

#define BESTEST_ASSERTION_TYPE_EQ     ==
#define BESTEST_ASSERTION_TYPE_NEQ    !=
#define BESTEST_ASSERTION_TYPE_GEQ    >=
#define BESTEST_ASSERTION_TYPE_GT     >
#define BESTEST_ASSERTION_TYPE_LEQ    <=
#define BESTEST_ASSERTION_TYPE_LT     <

#define BESTEST_ASSERTIONS(...) \
    BESTEST_ASSERTION(EQ,  __VA_ARGS__) \
    BESTEST_ASSERTION(NEQ, __VA_ARGS__) \
    BESTEST_ASSERTION(GEQ, __VA_ARGS__) \
    BESTEST_ASSERTION(GT,  __VA_ARGS__) \
    BESTEST_ASSERTION(LEQ, __VA_ARGS__) \
    BESTEST_ASSERTION(LT,  __VA_ARGS__)

BESTEST_ASSERTIONS(Short, short, "%d");
BESTEST_ASSERTIONS(Int, int, "%d");
BESTEST_ASSERTIONS(Long, long, "%ld");
BESTEST_ASSERTIONS(LongLong, long long, "%lld");
BESTEST_ASSERTIONS(UnsignedShort, unsigned short, "%u");
BESTEST_ASSERTIONS(UnsignedInt, unsigned int, "%u");
BESTEST_ASSERTIONS(UnsignedLong, unsigned long, "%lu");
BESTEST_ASSERTIONS(UnsignedLongLong, unsigned long long, "%llu");
BESTEST_ASSERTIONS(Float, float, "%f");
BESTEST_ASSERTIONS(Double, double, "%lf");
BESTEST_ASSERTIONS(LongDouble, long double, "%Lf");

#undef BESTEST_ASSERTION
#undef BESTEST_ASSERTIONS
#undef BESTEST_ASSERTION_TYPE_EQ 
#undef BESTEST_ASSERTION_TYPE_NEQ
#undef BESTEST_ASSERTION_TYPE_GEQ
#undef BESTEST_ASSERTION_TYPE_GT 
#undef BESTEST_ASSERTION_TYPE_LEQ
#undef BESTEST_ASSERTION_TYPE_LT 

#ifdef __cplusplus
}
#endif


#endif /* S_TEST_BESTEST_H */

