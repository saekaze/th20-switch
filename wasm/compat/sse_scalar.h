// Scalar SSE/SSE2 for the wasm32 build of TH20 (no x86 intrinsics on wasm).
// TH20 only uses the scalar (_ss/_sd) forms to pin float/double rounding the
// way the original x86 code did; each helper reproduces the x86 result,
// including the 0x80000000 "integer indefinite" for out-of-range truncation.
#pragma once
#include <stdint.h>
#include <string.h>
typedef struct { float f[4]; } __m128;
typedef struct { double d[2]; } __m128d;
typedef struct { int32_t i[4]; } __m128i;
#define TH20_SSE static inline __attribute__((always_inline))
TH20_SSE __m128 _mm_set_ss(float v){__m128 r={{v,0,0,0}};return r;}
TH20_SSE __m128 _mm_setzero_ps(void){__m128 r={{0,0,0,0}};return r;}
TH20_SSE float _mm_cvtss_f32(__m128 a){return a.f[0];}
TH20_SSE __m128 _mm_add_ss(__m128 a,__m128 b){a.f[0]=a.f[0]+b.f[0];return a;}
TH20_SSE __m128 _mm_sub_ss(__m128 a,__m128 b){a.f[0]=a.f[0]-b.f[0];return a;}
TH20_SSE __m128 _mm_mul_ss(__m128 a,__m128 b){a.f[0]=a.f[0]*b.f[0];return a;}
TH20_SSE __m128 _mm_div_ss(__m128 a,__m128 b){a.f[0]=a.f[0]/b.f[0];return a;}
TH20_SSE __m128d _mm_set_sd(double v){__m128d r={{v,0}};return r;}
TH20_SSE __m128d _mm_setzero_pd(void){__m128d r={{0,0}};return r;}
TH20_SSE double _mm_cvtsd_f64(__m128d a){return a.d[0];}
TH20_SSE __m128d _mm_add_sd(__m128d a,__m128d b){a.d[0]=a.d[0]+b.d[0];return a;}
TH20_SSE __m128d _mm_sub_sd(__m128d a,__m128d b){a.d[0]=a.d[0]-b.d[0];return a;}
TH20_SSE __m128d _mm_mul_sd(__m128d a,__m128d b){a.d[0]=a.d[0]*b.d[0];return a;}
TH20_SSE __m128d _mm_div_sd(__m128d a,__m128d b){a.d[0]=a.d[0]/b.d[0];return a;}
// x86 cvtt*: truncate toward zero; NaN or out of int32 range -> INT32_MIN.
TH20_SSE int32_t th20_sse_trunc_f(float v){return (v==v&&v>=-2147483648.f&&v<2147483648.f)?(int32_t)v:INT32_MIN;}
TH20_SSE int32_t th20_sse_trunc_d(double v){return (v==v&&v>-2147483649.0&&v<2147483648.0)?(int32_t)v:INT32_MIN;}
TH20_SSE int32_t _mm_cvtt_ss2si(__m128 a){return th20_sse_trunc_f(a.f[0]);}
TH20_SSE int32_t _mm_cvttss_si32(__m128 a){return th20_sse_trunc_f(a.f[0]);}
TH20_SSE int32_t _mm_cvttsd_si32(__m128d a){return th20_sse_trunc_d(a.d[0]);}
TH20_SSE __m128 _mm_cvtsi32_ss(__m128 a,int32_t v){a.f[0]=(float)v;return a;}
TH20_SSE __m128d _mm_cvtsi32_sd(__m128d a,int32_t v){a.d[0]=(double)v;return a;}
TH20_SSE __m128 _mm_cvtsd_ss(__m128 a,__m128d b){a.f[0]=(float)b.d[0];return a;}
TH20_SSE __m128d _mm_cvtss_sd(__m128d a,__m128 b){a.d[0]=(double)b.f[0];return a;}
TH20_SSE __m128 _mm_xor_ps(__m128 a,__m128 b){uint32_t x[4],y[4];memcpy(x,&a,16);memcpy(y,&b,16);for(int k=0;k<4;k++)x[k]^=y[k];memcpy(&a,x,16);return a;}
TH20_SSE __m128i _mm_set_epi32(int32_t e3,int32_t e2,int32_t e1,int32_t e0){__m128i r={{e0,e1,e2,e3}};return r;}
TH20_SSE __m128 _mm_castsi128_ps(__m128i a){__m128 r;memcpy(&r,&a,16);return r;}
TH20_SSE __m128i _mm_castps_si128(__m128 a){__m128i r;memcpy(&r,&a,16);return r;}
