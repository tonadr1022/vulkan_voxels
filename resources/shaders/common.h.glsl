// #define PACK_QUAD

#define SINGLE_TRIANGLE_QUAD

#ifdef PACK_QUAD
#define VERTEX_SIZE 5
#else
#define VERTEX_SIZE 8
#endif
#define EXPXM1(x) (2 << (x) - 1)
