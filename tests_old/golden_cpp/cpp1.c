

#define gamers(a)
gamers(rise)

#pragma message Hello World!

#define xstr(s) str(s)
#define str(s) #s
#define foo 4
// str (foo)
// xstr (foo)

#define FOO(x, y) (int x, int y)
#define BAR(x, y) x ## y
#define BAZ(x, y) BAR(x, y)
#define BBB(x) #x
#define AAA(a) BBB(a)

// BAZ(apple, pear)
// BBB(BAZ(apple, pear))
AAA(BAZ(apple, pear))

int foo FOO(L"16" AAA(BAZ(apple, pear)) L"16", b);
int bar FOO(apple AAA(BAZ(apple, pear)) L"16", b);
int baz FOO(apple BBB(BAZ(apple, pear)) L"16", b);

#if !defined(foo)
FOO(a, b);
#endif
