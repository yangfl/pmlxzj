#ifndef MACRO_H
#define MACRO_H 1


#define promise(x) if (!(x)) __builtin_unreachable()

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define if_fail(expr) if (unlikely(!(expr)))
#define return_if_fail(expr) if (unlikely(!(expr))) return
#define break_if_fail(expr) if (unlikely(!(expr))) break
#define continue_if_fail(expr) if (unlikely(!(expr))) continue
#define goto_if_fail(expr) if (unlikely(!(expr))) goto
#define return_with_nonzero(expr) do { \
  int res__ = (expr); \
  return_if_fail (res__ == 0) res__; \
} while (0)


#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define arraysize(a) (sizeof(a) / sizeof(a[0]))

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))


#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define clamp(value, left, right) min(max(value, left), right)
#define cmp(a, b) ((a) == (b) ? 0 : (a) < (b) ? -1 : 1)


#define BIT_FIELD(reg, shift, n) (((reg) >> (shift)) & ((1u << n) - 1))
#define SET_BIT_FIELD(val, shift, n) (((val) & ((1u << n) - 1)) << (shift))


#endif /* MACRO_H */
