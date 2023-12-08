#ifndef DEFER_H_
#define DEFER_H_H
#define PPCAT2(n,x) n ## x
#define PPCAT(n,x) PPCAT2(n,x)

// DEFER(stmt): execute "stmt" statement when the current lexical block exits.
// This is useful in tests to execute cleanup functions even if the test fails
// through ASSERT macros.
#define DEFER2(stmt, counter) \
    void PPCAT(__cleanup, counter) (int* __u) { stmt; } \
    int PPCAT(__var, counter) __attribute__((unused, cleanup(PPCAT(__cleanup, counter ))));
#define DEFER(stmt) DEFER2(stmt, __COUNTER__)

#endif