#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PI 3.141592654f

#define min(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a < _b ? _a : _b; \
})

#define max(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a > _b ? _a : _b; \
})

#define clamp(a, min_val, max_val) (max(min(a, max_val), min_val))

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	exit(EXIT_FAILURE); \
})
