
#include <thread> // std::this_thread::yield()

#include "lightsky/setup/Compiler.h"
#include "lightsky/setup/Macros.h"
#include "lightsky/setup/OS.h"

#include "lightsky/utils/Log.h"

#ifdef LS_OS_UNIX
    #include <time.h> // nanosleep, time_spec
#endif

#if defined(LS_COMPILER_CLANG) && defined(LS_ARCH_AARCH64)
    #include <arm_acle.h>
#endif

#include "lightsky/utils/Assertions.h" // LS_DEBUG_ASSERT

#include "lightsky/math/mat_utils.h"

#include "soft_render/SR_Config.hpp"
#include "soft_render/SR_Context.hpp"
#include "soft_render/SR_FragmentProcessor.hpp"
#include "soft_render/SR_Framebuffer.hpp"
#include "soft_render/SR_IndexBuffer.hpp"
#include "soft_render/SR_Shader.hpp"
#include "soft_render/SR_ShaderProcessor.hpp"
#include "soft_render/SR_VertexArray.hpp"
#include "soft_render/SR_VertexBuffer.hpp"
#include "soft_render/SR_VertexProcessor.hpp"



/*-----------------------------------------------------------------------------
 * Anonymous Helper Functions
-----------------------------------------------------------------------------*/
namespace math = ls::math;



namespace
{



#if SR_VERTEX_CACHING_ENABLED
class SR_PTVCache
{
  public:
    enum : size_t
    {
        PTV_CACHE_SIZE = SR_VERTEX_CACHE_SIZE,
        PTV_CACHE_MISS = ~(size_t)0,
    };

    alignas(sizeof(math::vec4)) size_t mIndices[PTV_CACHE_SIZE];

    SR_VertexParam& mParam;

    ls::math::vec4_t<float> (*mShader)(SR_VertexParam&);

    alignas(sizeof(math::vec4)) SR_TransformedVert mVertices[PTV_CACHE_SIZE];

    SR_PTVCache(const SR_PTVCache&)            = delete;
    SR_PTVCache(SR_PTVCache&&)                 = delete;
    SR_PTVCache& operator=(const SR_PTVCache&) = delete;
    SR_PTVCache& operator=(SR_PTVCache&&)      = delete;

    SR_PTVCache(ls::math::vec4_t<float> (*pShader)(SR_VertexParam&), SR_VertexParam& inParam) noexcept :
        mIndices{},
        mParam{inParam},
        mShader{pShader},
        mVertices{}
    {
        for (size_t& index : mIndices)
        {
            index = PTV_CACHE_MISS;
        }
    }

    inline SR_TransformedVert* query_and_update(size_t key) noexcept
    {
        size_t i = key % PTV_CACHE_SIZE;

        if (mIndices[i] != key)
        {
            mIndices[i] = key;
            mParam.vertId = key;
            mParam.pVaryings = mVertices[i].varyings;
            mVertices[i].vert = mShader(mParam);
        }

        return mVertices+i;
    }
};

#endif // SR_VERTEX_CACHING_ENABLED



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE math::vec4 sr_perspective_divide(const math::vec4& v) noexcept
{
    #if defined(LS_ARCH_X86)
        const __m128 p    = _mm_load_ps(reinterpret_cast<const float*>(&v));
        const __m128 wInv = _mm_rcp_ps(_mm_permute_ps(p, 0xFF));
        const __m128 vMul = _mm_mul_ps(p, wInv);
        return math::vec4{_mm_blend_ps(wInv, vMul, 0x07)};

    #elif defined(LS_ARCH_ARM)
        const uint32x4_t blendMask{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000};
        const float32x4_t w = vdupq_n_f32(vgetq_lane_f32(v.simd, 3));
        const float32x4_t wInv = vrecpeq_f32(w);
        const float32x4_t vMul = vmulq_f32(v.simd, vmulq_f32(vrecpsq_f32(w, wInv), wInv));
        return math::vec4{vbslq_f32(blendMask, vMul, wInv)};

    #else
        const math::vec4&& wInv = math::rcp(math::vec4{v[3]});
        const math::vec4&& vMul = v * wInv;
        return math::vec4{vMul[0], vMul[1], vMul[2], wInv[0]};

    #endif
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE void sr_perspective_divide3(math::vec4& LS_RESTRICT_PTR v0, math::vec4& LS_RESTRICT_PTR v1, math::vec4& LS_RESTRICT_PTR v2) noexcept
{
    #if defined(LS_ARCH_X86)
        const __m128 p0    = _mm_load_ps(reinterpret_cast<const float*>(&v0));
        const __m128 p1    = _mm_load_ps(reinterpret_cast<const float*>(&v1));
        const __m128 p2    = _mm_load_ps(reinterpret_cast<const float*>(&v2));

        const __m128 wInv0 = _mm_rcp_ps(_mm_permute_ps(p0, 0xFF));
        const __m128 wInv1 = _mm_rcp_ps(_mm_permute_ps(p1, 0xFF));
        const __m128 wInv2 = _mm_rcp_ps(_mm_permute_ps(p2, 0xFF));

        const __m128 vMul0 = _mm_mul_ps(p0, wInv0);
        const __m128 vMul1 = _mm_mul_ps(p1, wInv1);
        const __m128 vMul2 = _mm_mul_ps(p2, wInv2);

        _mm_store_ps(reinterpret_cast<float*>(&v0), _mm_blend_ps(wInv0, vMul0, 0x07));
        _mm_store_ps(reinterpret_cast<float*>(&v1), _mm_blend_ps(wInv1, vMul1, 0x07));
        _mm_store_ps(reinterpret_cast<float*>(&v2), _mm_blend_ps(wInv2, vMul2, 0x07));

    #elif defined(LS_ARCH_ARM)
        const uint32x4_t blendMask{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000};

        const float32x4_t w0 = vdupq_n_f32(vgetq_lane_f32(v0.simd, 3));
        const float32x4_t wInv0 = vrecpeq_f32(w0);
        const float32x4_t vMul0 = vmulq_f32(v0.simd, vmulq_f32(vrecpsq_f32(w0, wInv0), wInv0));
        v0.simd = vbslq_f32(blendMask, vMul0, wInv0);

        const float32x4_t w1 = vdupq_n_f32(vgetq_lane_f32(v1.simd, 3));
        const float32x4_t wInv1 = vrecpeq_f32(w1);
        const float32x4_t vMul1 = vmulq_f32(v1.simd, vmulq_f32(vrecpsq_f32(w1, wInv1), wInv1));
        v1.simd = vbslq_f32(blendMask, vMul1, wInv1);

        const float32x4_t w2 = vdupq_n_f32(vgetq_lane_f32(v2.simd, 3));
        const float32x4_t wInv2 = vrecpeq_f32(w2);
        const float32x4_t vMul2 = vmulq_f32(v2.simd, vmulq_f32(vrecpsq_f32(w2, wInv2), wInv2));
        v2.simd = vbslq_f32(blendMask, vMul2, wInv2);

    #else
        const math::vec4&& wInv0 = math::rcp(v0[3]);
        const math::vec4&& wInv1 = math::rcp(v1[3]);
        const math::vec4&& wInv2 = math::rcp(v2[3]);
        const math::vec4&& vMul0 = v0 * wInv0;
        const math::vec4&& vMul1 = v1 * wInv1;
        const math::vec4&& vMul2 = v2 * wInv2;
        v0 = {vMul0[0], vMul0[1], vMul0[2], wInv0[0]};
        v1 = {vMul1[0], vMul1[1], vMul1[2], wInv1[0]};
        v2 = {vMul2[0], vMul2[1], vMul2[2], wInv2[0]};

    #endif
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE void sr_world_to_screen_coords_divided(math::vec4& v, const float widthScale, const float heightScale) noexcept
{
    #if defined(LS_ARCH_X86)
        const __m128 p   = _mm_load_ps(reinterpret_cast<const float*>(&v));
        const __m128 wh0 = _mm_set_ps(0.f, 0.f, heightScale, widthScale);
        const __m128 wh1 = _mm_set_ps(1.f, 1.f, heightScale, widthScale);

        __m128 scl = _mm_fmadd_ps(wh1, p, wh0);
        scl = _mm_max_ps(_mm_floor_ps(scl), _mm_setzero_ps());
        _mm_store_ps(reinterpret_cast<float*>(&v), _mm_blend_ps(scl, p, 0x0C));

    #elif defined(LS_ARCH_AARCH64)
        const float32x4_t p = vld1q_f32(reinterpret_cast<const float*>(&v));

        const uint32x4_t blendMask{0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF};
        const float32x4_t wh0{widthScale, heightScale, 0.f, 0.f};
        const float32x4_t wh1{widthScale, heightScale, 1.f, 1.f};

        float32x4_t scl = vmlaq_f32(wh0, p, wh1);
        scl = vmaxq_f32(vdupq_n_f32(0.f), vrndmq_f32(scl));
        vst1q_f32(reinterpret_cast<float*>(&v), vbslq_f32(blendMask, p, scl));

    #elif defined(LS_ARCH_ARM)
        const uint32x4_t blendMask{0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF};
        const float32x4_t wh0{widthScale, heightScale, 0.f, 0.f};
        const float32x4_t wh1{widthScale, heightScale, 1.f, 1.f};

        const float32x4_t p = vld1q_f32(reinterpret_cast<const float*>(&v));
        float32x4_t scl = vmlaq_f32(wh0, p, wh1);
        scl = vmaxq_f32(vdupq_n_f32(0.f), vcvtq_f32_s32(vcvtq_s32_f32(scl)));
        vst1q_f32(reinterpret_cast<float*>(&v), vbslq_f32(blendMask, p, scl));

    #else
        v[0] = math::max(0.f, math::floor(math::fmadd(widthScale,  v[0], widthScale)));
        v[1] = math::max(0.f, math::floor(math::fmadd(heightScale, v[1], heightScale)));
        //v = math::fmadd(math::vec4{widthScale, heightScale, 1.f, 1.f}, v, math::vec4{widthScale, heightScale, 0.f, 0.f});

    #endif
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE void sr_world_to_screen_coords_divided3(
    math::vec4& LS_RESTRICT_PTR p0,
    math::vec4& LS_RESTRICT_PTR p1,
    math::vec4& LS_RESTRICT_PTR p2,
    const float widthScale,
    const float heightScale
) noexcept
{
    #if defined(LS_ARCH_X86)
        const __m128 wh0 = _mm_set_ps(0.f, 0.f, heightScale, widthScale);
        const __m128 wh1 = _mm_set_ps(1.f, 1.f, heightScale, widthScale);

        const __m128 v0 = _mm_load_ps(reinterpret_cast<float*>(&p0));
        const __m128 v1 = _mm_load_ps(reinterpret_cast<float*>(&p1));
        const __m128 v2 = _mm_load_ps(reinterpret_cast<float*>(&p2));

        __m128 scl0 = _mm_fmadd_ps(wh1, v0, wh0);
        __m128 scl1 = _mm_fmadd_ps(wh1, v1, wh0);
        __m128 scl2 = _mm_fmadd_ps(wh1, v2, wh0);

        scl0 = _mm_max_ps(_mm_floor_ps(scl0), _mm_setzero_ps());
        scl1 = _mm_max_ps(_mm_floor_ps(scl1), _mm_setzero_ps());
        scl2 = _mm_max_ps(_mm_floor_ps(scl2), _mm_setzero_ps());

        _mm_store_ps(reinterpret_cast<float*>(&p0), _mm_blend_ps(scl0, v0, 0x0C));
        _mm_store_ps(reinterpret_cast<float*>(&p1), _mm_blend_ps(scl1, v1, 0x0C));
        _mm_store_ps(reinterpret_cast<float*>(&p2), _mm_blend_ps(scl2, v2, 0x0C));

    #elif defined(LS_ARCH_AARCH64)
        const float32x4_t wh0{widthScale, heightScale, 0.f, 0.f};
        const float32x4_t wh1{widthScale, heightScale, 1.f, 1.f};
        const uint32x4_t blendMask{0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF};

        const float32x4_t v0 = vld1q_f32(reinterpret_cast<const float*>(&p0));
        const float32x4_t v1 = vld1q_f32(reinterpret_cast<const float*>(&p1));
        const float32x4_t v2 = vld1q_f32(reinterpret_cast<const float*>(&p2));

        float32x4_t scl0 = vmlaq_f32(wh0, v0, wh1);
        scl0 = vmaxq_f32(vdupq_n_f32(0.f), vrndmq_f32(scl0));
        vst1q_f32(reinterpret_cast<float*>(&p0), vbslq_f32(blendMask, v0, scl0));

        float32x4_t scl1 = vmlaq_f32(wh0, v1, wh1);
        scl1 = vmaxq_f32(vdupq_n_f32(0.f), vrndmq_f32(scl1));
        vst1q_f32(reinterpret_cast<float*>(&p1), vbslq_f32(blendMask, v1, scl1));

        float32x4_t scl2 = vmlaq_f32(wh0, v2, wh1);
        scl2 = vmaxq_f32(vdupq_n_f32(0.f), vrndmq_f32(scl2));
        vst1q_f32(reinterpret_cast<float*>(&p2), vbslq_f32(blendMask, v2, scl2));

    #elif defined(LS_ARCH_ARM)
        const float32x4_t wh0{widthScale, heightScale, 0.f, 0.f};
        const float32x4_t wh1{widthScale, heightScale, 1.f, 1.f};
        const uint32x4_t blendMask{0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF};

        const float32x4_t v0 = vld1q_f32(reinterpret_cast<const float*>(&p0));
        float32x4_t scl0 = vmlaq_f32(wh0, v0, wh1);
        scl0 = vmaxq_f32(vdupq_n_f32(0.f), vcvtq_f32_s32(vcvtq_s32_f32(scl0)));
        vst1q_f32(reinterpret_cast<float*>(&p0), vbslq_f32(blendMask, v0, scl0));

        const float32x4_t v1 = vld1q_f32(reinterpret_cast<const float*>(&p1));
        float32x4_t scl1 = vmlaq_f32(wh0, v1, wh1);
        scl1 = vmaxq_f32(vdupq_n_f32(0.f), vcvtq_f32_s32(vcvtq_s32_f32(scl1)));
        vst1q_f32(reinterpret_cast<float*>(&p1), vbslq_f32(blendMask, v1, scl1));

        const float32x4_t v2 = vld1q_f32(reinterpret_cast<const float*>(&p2));
        float32x4_t scl2 = vmlaq_f32(wh0, v2, wh1);
        scl2 = vmaxq_f32(vdupq_n_f32(0.f), vcvtq_f32_s32(vcvtq_s32_f32(scl2)));
        vst1q_f32(reinterpret_cast<float*>(&p2), vbslq_f32(blendMask, v2, scl2));

    #else
        p0[0] = math::max(0.f, math::floor(math::fmadd(widthScale,  p0[0], widthScale)));
        p0[1] = math::max(0.f, math::floor(math::fmadd(heightScale, p0[1], heightScale)));

        p1[0] = math::max(0.f, math::floor(math::fmadd(widthScale,  p1[0], widthScale)));
        p1[1] = math::max(0.f, math::floor(math::fmadd(heightScale, p1[1], heightScale)));

        p2[0] = math::max(0.f, math::floor(math::fmadd(widthScale,  p2[0], widthScale)));
        p2[1] = math::max(0.f, math::floor(math::fmadd(heightScale, p2[1], heightScale)));
        //v = math::fmadd(math::vec4{widthScale, heightScale, 1.f, 1.f}, v, math::vec4{widthScale, heightScale, 0.f, 0.f});l

    #endif
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE void sr_world_to_screen_coords(math::vec4& v, const float widthScale, const float heightScale) noexcept
{
    const float wInv = math::rcp(v.v[3]);
    math::vec4&& temp = v * wInv;

    temp[0] = widthScale  + temp[0] * widthScale;
    temp[1] = heightScale + temp[1] * heightScale;

    v[0] = temp[0];
    v[1] = temp[1];
    v[2] = temp[2];
    v[3] = wInv;
}



/*--------------------------------------
 * Get the next vertex from an IBO
--------------------------------------*/
inline size_t get_next_vertex(const SR_IndexBuffer* LS_RESTRICT_PTR pIbo, size_t vId) noexcept
{
    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:  return *reinterpret_cast<const unsigned char*>(pIbo->element(vId));
        case VERTEX_DATA_SHORT: return *reinterpret_cast<const unsigned short*>(pIbo->element(vId));
        case VERTEX_DATA_INT:   return *reinterpret_cast<const unsigned int*>(pIbo->element(vId));
        default:
            LS_UNREACHABLE();
    }
    return vId;
}



inline LS_INLINE math::vec3_t<size_t> get_next_vertex3(const SR_IndexBuffer* LS_RESTRICT_PTR pIbo, size_t vId) noexcept
{
    math::vec3_t<unsigned char> byteIds;
    math::vec3_t<unsigned short> shortIds;
    math::vec3_t<unsigned int> intIds;

    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:
            byteIds = *reinterpret_cast<const decltype(byteIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)byteIds;

        case VERTEX_DATA_SHORT:
            shortIds = *reinterpret_cast<const decltype(shortIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)shortIds;

        case VERTEX_DATA_INT:
            intIds = *reinterpret_cast<const decltype(intIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)intIds;

        default:
            LS_UNREACHABLE();
    }

    return math::vec3_t<size_t>{0, 0, 0};
}



/*--------------------------------------
 * Triangle determinants for backface culling
--------------------------------------*/
inline LS_INLINE float face_determinant(
    const math::vec4& LS_RESTRICT_PTR p0,
    const math::vec4& LS_RESTRICT_PTR p1,
    const math::vec4& LS_RESTRICT_PTR p2
) noexcept
{
    // 3D homogeneous determinant of the 3 vertices of a triangle. The
    // Z-component of each 3D vertex is replaced by the 4D W-component.

    #if defined(LS_ARCH_X86)
        constexpr int shuffleMask120 = 0x8D; // indices: <base> + (2, 0, 3, 1): 10001101
        constexpr int shuffleMask201 = 0x93; // indices: <base> + (2, 1, 0, 3): 10010011

        // Swap the z and w components for each vector while placing the
        // remaining elements in a position optimal for calculating the
        // determinant.
        const __m128 col4   = _mm_insert_ps( p0.simd, p0.simd, 0xE8);
        const __m128 p1_201 = _mm_permute_ps(p1.simd, shuffleMask201);
        const __m128 p2_120 = _mm_permute_ps(p2.simd, shuffleMask120);
        const __m128 p1_120 = _mm_permute_ps(p1.simd, shuffleMask120);
        const __m128 p2_201 = _mm_permute_ps(p2.simd, shuffleMask201);

        const __m128 col3 = _mm_mul_ps(p1_201, p2_120);
        const __m128 col1 = _mm_mul_ps(p1_120, p2_201);
        const __m128 sub0 = _mm_sub_ps(col1,   col3);

        // Remove the Z component which was shuffled earlier
        const __m128 mul2 = _mm_mul_ps(sub0, col4);

        // horizontal add of only the first 3 elements. We're using the
        // W-component of each vector as the 3rd dimension for out determinant
        // calculation
        __m128 sum;
        const __m128 lo = _mm_permute_ps(mul2, 0x55);
        const __m128 hi = _mm_movehl_ps(mul2, mul2);
        sum = _mm_add_ss(mul2, lo);
        sum = _mm_add_ss(sum, hi);
        return _mm_cvtss_f32(sum);

    #elif defined(LS_ARCH_ARM) // based on the AVX implementation
        const float32x4_t col4 = vcombine_f32(vget_low_f32(p0.simd), vrev64_f32(vget_high_f32(p0.simd)));

        const float32x2x2_t p1_120 = vzip_f32(vget_low_f32(p1.simd), vget_high_f32(p1.simd));
        const float32x4_t col0 = vcombine_f32(p1_120.val[1], p1_120.val[0]);

        const uint32x4_t p2_201 = vextq_u32(vreinterpretq_u32_f32(p2.simd), vreinterpretq_u32_f32(p2.simd), 3);
        const float32x4_t col1 = vmulq_f32(col0, vreinterpretq_f32_u32(p2_201));

        const float32x4_t col2 = vreinterpretq_f32_u32(vextq_u32(vreinterpretq_u32_f32(p1.simd), vreinterpretq_u32_f32(p1.simd), 3));

        const float32x2x2_t p2_120 = vzip_f32(vget_low_f32(p2.simd), vget_high_f32(p2.simd));
        const float32x4_t sub0 = vmlsq_f32(col1, col2, vcombine_f32(p2_120.val[1], p2_120.val[0]));

        // perform a dot product to get the determinant
        const float32x4_t mul2 = vmulq_f32(sub0, col4);

        #if 0//defined(LS_ARCH_AARCH64)
            return vaddvq_f32(mul2);
        #else
            const float32x2_t swap = vadd_f32(vget_high_f32(mul2), vget_low_f32(mul2));
            const float32x2_t sum = vpadd_f32(swap, swap);
            return vget_lane_f32(sum, 0);
        #endif

    #elif 1
        const math::mat3 det{
            math::vec3{p0[0], p0[1], p0[3]},
            math::vec3{p1[0], p1[1], p1[3]},
            math::vec3{p2[0], p2[1], p2[3]}
        };
        return math::determinant(det);

    #else
        // Alternative algorithm (requires division):
        // Get the normalized homogeneous normal of a triangle and determine
        // if the normal's Z-component is towards, or away from, the camera's
        // Z component.
        const math::vec4&& v0 = p0 / p0[3];
        const math::vec4&& v1 = p1 / p1[3];
        const math::vec4&& v2 = p2 / p2[3];
        const math::vec4&& cx = math::cross(v1-v0, v2-v0);

        // Z-component of the triangle normal
        return cx[2];

    #endif
}



/*--------------------------------------
 * Cull only triangle outside of the screen
--------------------------------------*/
inline LS_INLINE SR_ClipStatus face_visible(
    const math::vec4& LS_RESTRICT_PTR clip0,
    const math::vec4& LS_RESTRICT_PTR clip1,
    const math::vec4& LS_RESTRICT_PTR clip2
) noexcept
{
    #if defined(LS_ARCH_X86)
        const __m128 w0p = _mm_permute_ps(clip0.simd, 0xFF);
        const __m128 w1p = _mm_permute_ps(clip1.simd, 0xFF);
        const __m128 w2p = _mm_permute_ps(clip2.simd, 0xFF);

        const __m128 sign = _mm_set1_ps(-0.f);
        const __m128 ge0 = _mm_and_ps(_mm_cmple_ps(_mm_or_ps(w0p, sign), clip0.simd), _mm_cmpge_ps(w0p, clip0.simd));
        const __m128 ge1 = _mm_and_ps(_mm_cmple_ps(_mm_or_ps(w1p, sign), clip1.simd), _mm_cmpge_ps(w1p, clip1.simd));
        const __m128 ge2 = _mm_and_ps(_mm_cmple_ps(_mm_or_ps(w2p, sign), clip2.simd), _mm_cmpge_ps(w2p, clip2.simd));
        const __m128 vis = _mm_and_ps(_mm_and_ps(ge0, ge1), ge2);
        const int visI = SR_TRIANGLE_FULLY_VISIBLE & -(_mm_movemask_ps(vis) == 0x0F);

        const __m128 le0 = _mm_cmpgt_ps(w0p, _mm_setzero_ps());
        const __m128 le1 = _mm_cmpgt_ps(w1p, _mm_setzero_ps());
        const __m128 le2 = _mm_cmpgt_ps(w2p, _mm_setzero_ps());
        const __m128 part = _mm_or_ps(_mm_or_ps(le0, le1), le2);
        const int partI = SR_TRIANGLE_PARTIALLY_VISIBLE & -(_mm_movemask_ps(part) == 0x0F);

        return (SR_ClipStatus)(visI | partI);

    #elif defined(LS_ARCH_ARM)
        const float32x4_t w0p = vdupq_lane_f32(vget_high_f32(clip0.simd), 1);
        const float32x4_t w1p = vdupq_lane_f32(vget_high_f32(clip1.simd), 1);
        const float32x4_t w2p = vdupq_lane_f32(vget_high_f32(clip2.simd), 1);

        const uint32x4_t le0  = vcaleq_f32(clip0.simd, w0p);
        const uint32x4_t le1  = vcaleq_f32(clip1.simd, w1p);
        const uint32x4_t le2  = vcaleq_f32(clip2.simd, w2p);

        const uint32x4_t vis = vandq_u32(le2, vandq_u32(le1, le0));
        const uint32x2_t vis2 = vand_u32(vget_low_u32(vis), vget_high_u32(vis));
        const unsigned   visI = SR_TRIANGLE_FULLY_VISIBLE & vget_lane_u32(vand_u32(vis2, vrev64_u32(vis2)), 0);

        const uint32x2_t gt0 = vcgt_f32(vget_low_f32(w0p), vcreate_f32(0));
        const uint32x2_t gt1 = vcgt_f32(vget_low_f32(w1p), vcreate_f32(0));
        const uint32x2_t gt2 = vcgt_f32(vget_low_f32(w2p), vcreate_f32(0));
        const uint32x2_t part2 = vorr_u32(gt2, vorr_u32(gt1, gt0));
        const unsigned   partI = SR_TRIANGLE_PARTIALLY_VISIBLE & vget_lane_u32(vorr_u32(part2, vrev64_u32(part2)), 0);

        return (SR_ClipStatus)(visI | partI);

    #else
        const math::vec4 w0p = clip0[3];
        const math::vec4 w1p = clip1[3];
        const math::vec4 w2p = clip2[3];

        const math::vec4 w1n = -clip1[3];
        const math::vec4 w0n = -clip0[3];
        const math::vec4 w2n = -clip2[3];

        int vis = SR_TRIANGLE_FULLY_VISIBLE & -(
            clip0 <= w0p &&
            clip1 <= w1p &&
            clip2 <= w2p &&
            clip0 >= w0n &&
            clip1 >= w1n &&
            clip2 >= w2n
        );

        int part = SR_TRIANGLE_PARTIALLY_VISIBLE & -(w0p > 0.f || w1p > 0.f || w2p > 0.f);

        return (SR_ClipStatus)(vis | part);
    #endif
}



} // end anonymous namespace



/*-----------------------------------------------------------------------------
 * SR_VertexProcessor Class
-----------------------------------------------------------------------------*/
/*-------------------------------------
 * Execute a fragment processor
-------------------------------------*/
void SR_VertexProcessor::flush_bins() const noexcept
{
    // Allow the other threads to know this thread is ready for processing
    const bool noBlending = mShader->mFragShader.blend == SR_BLEND_OFF;

    const uint_fast64_t tileId = mFragProcessors->count.fetch_add(1ull, std::memory_order_acq_rel);
    mBinsReady[mThreadId].count.store((int32_t)mThreadId, std::memory_order_release);

    SR_FragmentProcessor fragTask{
        (uint16_t)tileId,
        mRenderMode,
        (uint32_t)mNumThreads,
        mBinsUsed[mThreadId].count,
        mShader,
        mFbo,
        mFragBins + mThreadId * SR_SHADER_MAX_BINNED_PRIMS,
        mVaryings + mThreadId * SR_SHADER_MAX_VARYING_VECTORS * SR_SHADER_MAX_QUEUED_FRAGS,
        mFragQueues + mThreadId
    };

    // Execute the fragment processor if possible
    if (noBlending && mBinsUsed[mThreadId].count)
    {
        fragTask.execute();
    }

    #if defined(LS_OS_UNIX) && !defined(LS_ARCH_X86)
        struct timespec sleepAmt;
        sleepAmt.tv_sec = 0;
        sleepAmt.tv_nsec = 1;
        (void)sleepAmt;
    #endif

    for (uint32_t t = 0; t < (uint32_t)mNumThreads; ++t)
    {
        if (noBlending && mBinsReady[t].count.load(std::memory_order_consume) == (int32_t)mThreadId)
        {
            continue;
        }

        // wait for the next available set of bins
        while (LS_LIKELY(mBinsReady[t].count.load(std::memory_order_consume) < 0))
        {
            #if defined(LS_ARCH_X86)
                _mm_pause();
            #elif defined(LS_COMPILER_CLANG) && defined(LS_ARCH_AARCH64)
                __yield();
            #elif defined(LS_OS_LINUX) || defined(LS_OS_ANDROID)
                clock_nanosleep(CLOCK_MONOTONIC, 0, &sleepAmt, nullptr);
            #elif defined(LS_OS_OSX) || defined(LS_OS_IOS) || defined(LS_OS_IOS_SIM)
                nanosleep(&sleepAmt, nullptr);
            #else
                std::this_thread::yield();
            #endif
        }

        uint32_t currentThread = (uint32_t)mBinsReady[t].count.load(std::memory_order_consume);
        uint32_t binsUsed = mBinsUsed[currentThread].count;
        if (!binsUsed)
        {
            continue;
        }

        fragTask.mNumBins = binsUsed;
        fragTask.mBins = mFragBins + currentThread * SR_SHADER_MAX_BINNED_PRIMS;
        fragTask.execute();
    }

    // Indicate to all threads we can now process more vertices
    const uint_fast64_t syncPoint = (uint_fast64_t)mNumThreads * 2ull - 1ull;
    if (syncPoint == mFragProcessors->count.fetch_add(1, std::memory_order_acq_rel))
    {
        for (uint32_t t = 0; t < (uint32_t)mNumThreads; ++t)
        {
            mBinsReady[t].count.store(-1, std::memory_order_release);
            mBinsUsed[t].count = 0;
        }

        mFragProcessors->count.store(0ull, std::memory_order_release);
    }
    else
    {
        // Wait for the last thread to reset the number of available bins.
        while (LS_LIKELY(mFragProcessors->count.load(std::memory_order_consume) >= (uint_fast64_t)mNumThreads))
        {
            #if defined(LS_ARCH_X86)
                _mm_pause();
            #elif defined(LS_COMPILER_CLANG) && defined(LS_ARCH_AARCH64)
                __yield();
            #elif defined(LS_OS_LINUX) || defined(LS_OS_ANDROID)
                clock_nanosleep(CLOCK_MONOTONIC, 0, &sleepAmt, nullptr);
            #elif defined(LS_OS_OSX) || defined(LS_OS_IOS) || defined(LS_OS_IOS_SIM)
                nanosleep(&sleepAmt, nullptr);
            #else
                std::this_thread::yield();
            #endif
        }
    }
}



/*--------------------------------------
 * Publish a vertex to a fragment thread
--------------------------------------*/
template <int renderMode, int vertCount>
void SR_VertexProcessor::push_bin(
    float fboW,
    float fboH,
    const SR_TransformedVert& a,
    const SR_TransformedVert& b,
    const SR_TransformedVert& c
) const noexcept
{
    const uint_fast32_t numVaryings = mShader->get_num_varyings();
    const uint32_t binId = mBinsUsed[mThreadId].count;

    const math::vec4& p0 = a.vert;
    const math::vec4& p1 = b.vert;
    const math::vec4& p2 = c.vert;
    float bboxMinX;
    float bboxMinY;
    float bboxMaxX;
    float bboxMaxY;

    // calculate the bounds of the tile which a certain thread will be
    // responsible for

    // render points through whichever tile/thread they appear in
    switch (renderMode)
    {
        case RENDER_MODE_POINTS:
            bboxMinX = p0[0];
            bboxMaxX = p0[0];
            bboxMinY = p0[1];
            bboxMaxY = p0[1];
            break;

        case RENDER_MODE_LINES:
            // establish a bounding box to detect overlap with a thread's tiles
            bboxMinX = math::min(p0[0], p1[0]);
            bboxMinY = math::min(p0[1], p1[1]);
            bboxMaxX = math::max(p0[0], p1[0]);
            bboxMaxY = math::max(p0[1], p1[1]);
            break;

        case RENDER_MODE_TRIANGLES:
            // establish a bounding box to detect overlap with a thread's tiles
            bboxMinX = math::min(p0[0], p1[0], p2[0]);
            bboxMinY = math::min(p0[1], p1[1], p2[1]);
            bboxMaxX = math::max(p0[0], p1[0], p2[0]);
            bboxMaxY = math::max(p0[1], p1[1], p2[1]);
            break;

        default:
            return;
    }

    int isPrimHidden = (bboxMaxX < 0.f || bboxMaxY < 0.f || fboW < bboxMinX || fboH < bboxMinY);
    isPrimHidden = isPrimHidden || (bboxMaxX-bboxMinX < 1.f) || (bboxMaxY-bboxMinY < 1.f);
    if (LS_UNLIKELY(isPrimHidden))
    {
        return;
    }

    SR_FragmentBin* const pFragBins = mFragBins + mThreadId * SR_SHADER_MAX_BINNED_PRIMS;

    // place a triangle into the next available bin
    SR_FragmentBin& bin = pFragBins[binId];
    bin.mScreenCoords[0] = p0;
    bin.mScreenCoords[1] = p1;
    bin.mScreenCoords[2] = p2;

    // Copy all per-vertex coordinates and varyings to the fragment bins
    // which will need the data for interpolation. The perspective-divide
    // is only used for rendering triangles.
    if (renderMode == SR_RenderMode::RENDER_MODE_TRIANGLES)
    {
        const math::vec4&& p0p1 = p0 - p1;
        const math::vec4&& p0p2 = p0 - p2;
        const math::vec4&& p1p0 = p1 - p0;
        const math::vec4&& p1p2 = p1 - p2;
        const math::vec4&& p2p0 = p2 - p0;
        const math::vec4&& p2p1 = p2 - p1;

        const float denom = math::rcp((p0p2[0])*(p1p0[1]) - (p0p1[0])*(p2p0[1]));
        bin.mBarycentricCoords[0] = denom*math::vec4(p1p2[1], p2p0[1], p0p1[1], 0.f);
        bin.mBarycentricCoords[1] = denom*math::vec4(p2p1[0], p0p2[0], p1p0[0], 0.f);
        bin.mBarycentricCoords[2] = denom*math::vec4(
            p1[0]*p2[1] - p2[0]*p1[1],
            p2[0]*p0[1] - p0[0]*p2[1],
            p0[0]*p1[1] - p1[0]*p0[1],
            0.f
        );
    }

    unsigned i;
    switch (vertCount)
    {
        case 3:
            for (i = 0; i < numVaryings; ++i)
            {
                bin.mVaryings[i+SR_SHADER_MAX_VARYING_VECTORS*0] = a.varyings[i];
                bin.mVaryings[i+SR_SHADER_MAX_VARYING_VECTORS*1] = b.varyings[i];
                bin.mVaryings[i+SR_SHADER_MAX_VARYING_VECTORS*2] = c.varyings[i];
            }
            break;

        case 2:
            for (i = 0; i < numVaryings; ++i)
            {
                bin.mVaryings[i+SR_SHADER_MAX_VARYING_VECTORS*0] = a.varyings[i];
                bin.mVaryings[i+SR_SHADER_MAX_VARYING_VECTORS*1] = b.varyings[i];
            }
            break;

        case 1:
            for (i = 0; i < numVaryings; ++i)
            {
                bin.mVaryings[i] = a.varyings[i];
            }
            break;
    }

    // Check if the output bin is full
    mBinsUsed[mThreadId].count = binId+1;
    if (LS_UNLIKELY(binId == SR_SHADER_MAX_BINNED_PRIMS-1))
    {
        flush_bins();
    }
}



template void SR_VertexProcessor::push_bin<RENDER_MODE_POINTS, 1>(
    float,
    float,
    const SR_TransformedVert&,
    const SR_TransformedVert&,
    const SR_TransformedVert&
) const noexcept;

template void SR_VertexProcessor::push_bin<RENDER_MODE_LINES, 2>(
    float,
    float,
    const SR_TransformedVert&,
    const SR_TransformedVert&,
    const SR_TransformedVert&
) const noexcept;

template void SR_VertexProcessor::push_bin<RENDER_MODE_TRIANGLES, 3>(
    float,
    float,
    const SR_TransformedVert&,
    const SR_TransformedVert&,
    const SR_TransformedVert&
) const noexcept;



/*-------------------------------------
 * Ensure only visible triangles get rendered. Triangles should have already
 * been tested for visibility within clip-space. Now we need to clip the
 * remaining tris and generate new ones.
 *
 * The basic clipping algorithm is as follows:
 *
 *  for each clipping edge do
 *      for (i = 0; i < Polygon.length; i++)
 *          Pi = Polygon.vertex[i];
 *          Pi+1 = Polygon.vertex[i+1];
 *          if (Pi is inside clipping region)
 *              if (Pi+1 is inside clipping region)
 *                  clippedPolygon.add(Pi+1)
 *              else
 *                  clippedPolygon.add(intersectionPoint(Pi, Pi+1, currentEdge)
 *          else
 *              if (Pi+1 is inside clipping region)
 *                  clippedPolygon.add(intersectionPoint(Pi, Pi+1, currentEdge)
 *                  clippedPolygon.add(Pi+1)
 *      end for
 *      Polygon = clippedPolygon     // keep on working with the new polygon
 *  end for
-------------------------------------*/
void SR_VertexProcessor::clip_and_process_tris(
    float fboW,
    float fboH,
    const SR_TransformedVert& a,
    const SR_TransformedVert& b,
    const SR_TransformedVert& c) noexcept
{
    const SR_VertexShader vertShader    = mShader->mVertShader;
    const unsigned        numVarys      = (unsigned)vertShader.numVaryings;
    const float           widthScale    = fboW * 0.5f;
    const float           heightScale   = fboH * 0.5f;
    constexpr unsigned    numTempVerts  = 9; // at most 9 vertices should be generated
    unsigned              numTotalVerts = 3;
    math::vec4            tempVerts     [numTempVerts];
    math::vec4            newVerts      [numTempVerts];
    math::vec4            tempVarys     [numTempVerts * SR_SHADER_MAX_VARYING_VECTORS];
    math::vec4            newVarys      [numTempVerts * SR_SHADER_MAX_VARYING_VECTORS];
    const math::vec4      clipEdges[]  = {
        { 1.f,  0.f,  0.f, 1.f},
        {-1.f,  0.f,  0.f, 1.f},
        { 0.f,  1.f,  0.f, 1.f},
        { 0.f, -1.f,  0.f, 1.f},
#if SR_Z_CLIPPING_ENABLED
        { 0.f,  0.f,  1.f, 1.f},
        { 0.f,  0.f, -1.f, 1.f},
#endif
    };

    const auto _copy_verts = [](int maxVerts, const math::vec4* inVerts, math::vec4* outVerts) noexcept->void
    {
        for (unsigned i = maxVerts; i--;)
        {
            outVerts[i] = inVerts[i];
        }
    };

    const auto _interpolate_varyings = [&numVarys](const math::vec4* inVarys, math::vec4* outVarys, int fromIndex, int toIndex, float amt) noexcept->void
    {
        const math::vec4* pV0 = inVarys + fromIndex * SR_SHADER_MAX_VARYING_VECTORS;
        const math::vec4* pV1 = inVarys + toIndex * SR_SHADER_MAX_VARYING_VECTORS;

        for (unsigned i = numVarys; i--;)
        {
            *outVarys++ = math::mix(*pV0++, *pV1++, amt);
        }
    };

    newVerts[0] = a.vert;
    _copy_verts(numVarys, a.varyings, newVarys + 0 * SR_SHADER_MAX_VARYING_VECTORS);

    newVerts[1] = b.vert;
    _copy_verts(numVarys, b.varyings, newVarys + 1 * SR_SHADER_MAX_VARYING_VECTORS);

    newVerts[2] = c.vert;
    _copy_verts(numVarys, c.varyings, newVarys + 2 * SR_SHADER_MAX_VARYING_VECTORS);

    for (const math::vec4& edge : clipEdges)
    {
        // caching
        unsigned   numNewVerts = 0;
        unsigned   j           = numTotalVerts-1;
        math::vec4 p0          = newVerts[numTotalVerts-1];
        float      t0          = math::dot(p0, edge);
        int        visible0    = t0 >= 0.f;

        for (unsigned k = 0; k < numTotalVerts; ++k)
        {
            const math::vec4& p1 = newVerts[k];
            const float t1       = math::dot(p1, edge);
            const int   visible1 = t1 >= 0.f;

            if (visible0 ^ visible1)
            {
                const float t = t0 / (t0-t1);//math::clamp(t0 / (t0-t1), 0.f, 1.f);
                tempVerts[numNewVerts] = math::mix(p0, p1, t);
                _interpolate_varyings(newVarys, tempVarys+(numNewVerts*SR_SHADER_MAX_VARYING_VECTORS), j, k, t);

                ++numNewVerts;
            }

            if (visible1)
            {
                tempVerts[numNewVerts] = p1;
                _copy_verts(numVarys, newVarys+(k*SR_SHADER_MAX_VARYING_VECTORS), tempVarys+(numNewVerts*SR_SHADER_MAX_VARYING_VECTORS));
                ++numNewVerts;
            }

            j           = k;
            p0          = p1;
            t0          = t1;
            visible0    = visible1;
        }

        if (!numNewVerts)
        {
            return;
        }

        numTotalVerts = numNewVerts;
        _copy_verts(numNewVerts, tempVerts, newVerts);

        for (unsigned i = numNewVerts; i--;)
        {
            const unsigned offset = i*SR_SHADER_MAX_VARYING_VECTORS;
            _copy_verts(numNewVerts, tempVarys+offset, newVarys+offset);
        }
    }

    if (numTotalVerts < 3)
    {
        return;
    }

    LS_DEBUG_ASSERT(numTotalVerts <= numTempVerts);

    for (unsigned i = numTotalVerts; i--;)
    {
        newVerts[i] = sr_perspective_divide(newVerts[i]);
        sr_world_to_screen_coords_divided(newVerts[i], widthScale, heightScale);
    }

    SR_TransformedVert p0, p1, p2;
    p0.vert = newVerts[0];
    _copy_verts(numVarys, newVarys, p0.varyings);

    for (unsigned i = numTotalVerts-2; i--;)
    {
        const unsigned j = i+1;
        const unsigned k = i+2;

        p1.vert = newVerts[j];
        _copy_verts(numVarys, newVarys+(j*SR_SHADER_MAX_VARYING_VECTORS), p1.varyings);

        p2.vert = newVerts[k];
        _copy_verts(numVarys, newVarys+(k*SR_SHADER_MAX_VARYING_VECTORS), p2.varyings);

        push_bin<SR_RenderMode::RENDER_MODE_TRIANGLES, 3>(fboW, fboH, p0, p1, p2);
    }
}



/*--------------------------------------
 * Process Points
--------------------------------------*/
void SR_VertexProcessor::process_points(const SR_Mesh& m, size_t instanceId) noexcept
{
    SR_TransformedVert      pVert0;
    const SR_VertexShader   vertShader   = mShader->mVertShader;
    const auto              shader       = vertShader.shader;
    const SR_VertexArray&   vao          = mContext->vao(m.vaoId);
    const float             fboW         = (float)mFbo->width();
    const float             fboH         = (float)mFbo->height();
    const float             widthScale   = fboW * 0.5f;
    const float             heightScale  = fboH * 0.5f;
    const SR_IndexBuffer*   pIbo         = vao.has_index_buffer() ? &mContext->ibo(vao.get_index_buffer()) : nullptr;
    const bool              usingIndices = m.mode == RENDER_MODE_INDEXED_POINTS;

    SR_VertexParam params;
    params.pUniforms  = mShader->mUniforms;
    params.instanceId = instanceId;
    params.pVao       = &vao;
    params.pVbo       = &mContext->vbo(vao.get_vertex_buffer());

    #if SR_VERTEX_CACHING_ENABLED
        size_t begin;
        size_t end;
        constexpr size_t step = 1;

        sr_calc_indexed_parition<1>(m.elementEnd-m.elementBegin, mNumThreads, mThreadId, begin, end);
        begin += m.elementBegin;
        end += m.elementBegin;

        SR_PTVCache ptvCache{shader, params};
    #else
        const size_t begin = m.elementBegin + mThreadId;
        const size_t end   = m.elementEnd;
        const size_t step  = mNumThreads;
    #endif

    for (size_t i = begin; i < end; i += step)
    {
        #if SR_VERTEX_CACHING_ENABLED
            const size_t vertId = usingIndices ? get_next_vertex(pIbo, i) : i;
            pVert0 = *ptvCache.query_and_update(vertId);
        #else
            params.vertId    = usingIndices ? get_next_vertex(pIbo, i) : i;
            params.pVaryings = pVert0.varyings;
            pVert0.vert      = shader(params);
        #endif

        if (pVert0.vert[3] > 0.f)
        {
            sr_world_to_screen_coords(pVert0.vert, widthScale, heightScale);
            push_bin<SR_RenderMode::RENDER_MODE_POINTS, 1>(fboW, fboH, pVert0, pVert0, pVert0);
        }
    }
}



/*--------------------------------------
 * Process Lines
--------------------------------------*/
void SR_VertexProcessor::process_lines(const SR_Mesh& m, size_t instanceId) noexcept
{
    SR_TransformedVert      pVert0;
    SR_TransformedVert      pVert1;
    const SR_VertexShader   vertShader   = mShader->mVertShader;
    const auto              shader       = vertShader.shader;
    const SR_VertexArray&   vao          = mContext->vao(m.vaoId);
    const float             fboW         = (float)mFbo->width();
    const float             fboH         = (float)mFbo->height();
    const float             widthScale   = fboW * 0.5f;
    const float             heightScale  = fboH * 0.5f;
    const SR_IndexBuffer*   pIbo         = vao.has_index_buffer() ? &mContext->ibo(vao.get_index_buffer()) : nullptr;
    const bool              usingIndices = m.mode == RENDER_MODE_INDEXED_LINES;

    SR_VertexParam params;
    params.pUniforms  = mShader->mUniforms;
    params.instanceId = instanceId;
    params.pVao       = &vao;
    params.pVbo       = &mContext->vbo(vao.get_vertex_buffer());

    #if SR_VERTEX_CACHING_ENABLED
        size_t begin;
        size_t end;
        constexpr size_t step = 2;

        sr_calc_indexed_parition<2>(m.elementEnd-m.elementBegin, mNumThreads, mThreadId, begin, end);
        begin += m.elementBegin;
        end += m.elementBegin;

        SR_PTVCache ptvCache{shader, params};
    #else
        const size_t begin = m.elementBegin + mThreadId * 2u;
        const size_t end   = m.elementEnd;
        const size_t step  = mNumThreads * 2u;
    #endif

    for (size_t i = begin; i < end; i += step)
    {
        const size_t index0  = i;
        const size_t index1  = i + 1;

        #if SR_VERTEX_CACHING_ENABLED
            const size_t vertId0 = usingIndices ? get_next_vertex(pIbo, index0) : index0;
            const size_t vertId1 = usingIndices ? get_next_vertex(pIbo, index1) : index1;
            pVert0 = *ptvCache.query_and_update(vertId0);
            pVert1 = *ptvCache.query_and_update(vertId1);
        #else
            params.vertId    = usingIndices ? get_next_vertex(pIbo, index0) : index0;
            params.pVaryings = pVert0.varyings;
            pVert0.vert      = shader(params);

            params.vertId    = usingIndices ? get_next_vertex(pIbo, index1) : index1;
            params.pVaryings = pVert1.varyings;
            pVert1.vert      = shader(params);
        #endif

        if (pVert0.vert[3] >= 0.f && pVert1.vert[3] >= 0.f)
        {
            sr_world_to_screen_coords(pVert0.vert, widthScale, heightScale);
            sr_world_to_screen_coords(pVert1.vert, widthScale, heightScale);

            push_bin<SR_RenderMode::RENDER_MODE_LINES, 2>(fboW, fboH, pVert0, pVert1, pVert1);
        }
    }
}



/*--------------------------------------
 * Process Triangles
--------------------------------------*/
void SR_VertexProcessor::process_tris(const SR_Mesh& m, size_t instanceId) noexcept
{
    SR_TransformedVert      pVert0;
    SR_TransformedVert      pVert1;
    SR_TransformedVert      pVert2;
    const SR_VertexShader   vertShader   = mShader->mVertShader;
    const SR_CullMode       cullMode     = vertShader.cullMode;
    const auto              shader       = vertShader.shader;
    const SR_VertexArray&   vao          = mContext->vao(m.vaoId);
    const float             fboW         = (float)mFbo->width();
    const float             fboH         = (float)mFbo->height();
    const float             widthScale   = fboW * 0.5f;
    const float             heightScale  = fboH * 0.5f;
    const SR_IndexBuffer*   pIbo         = vao.has_index_buffer() ? &mContext->ibo(vao.get_index_buffer()) : nullptr;
    const int               usingIndices = (m.mode == RENDER_MODE_INDEXED_TRIANGLES) || (m.mode == RENDER_MODE_INDEXED_TRI_WIRE);

    SR_VertexParam params;
    params.pUniforms  = mShader->mUniforms;
    params.instanceId = instanceId;
    params.pVao       = &vao;
    params.pVbo       = &mContext->vbo(vao.get_vertex_buffer());

    #if SR_VERTEX_CACHING_ENABLED
        size_t begin;
        size_t end;
        constexpr size_t step = 3;

        sr_calc_indexed_parition<3, true>(m.elementEnd-m.elementBegin, mNumThreads, mThreadId, begin, end);
        begin += m.elementBegin;
        end += m.elementBegin;

        SR_PTVCache ptvCache{shader, params};
    #else
        const size_t begin = m.elementBegin + mThreadId * 3u;
        const size_t end   = m.elementEnd;
        const size_t step  = mNumThreads * 3u;
    #endif

    for (size_t i = begin; i < end; i += step)
    {
        const math::vec3_t<size_t>&& vertId = usingIndices ? get_next_vertex3(pIbo, i) : math::vec3_t<size_t>{i, i+1, i+2};

        #if SR_VERTEX_CACHING_ENABLED
            pVert0 = *ptvCache.query_and_update(vertId[0]);
            pVert1 = *ptvCache.query_and_update(vertId[1]);
            pVert2 = *ptvCache.query_and_update(vertId[2]);
        #else
            params.vertId    = vertId.v[0];
            params.pVaryings = pVert0.varyings;
            pVert0.vert      = shader(params);

            params.vertId    = vertId.v[1];
            params.pVaryings = pVert1.varyings;
            pVert1.vert      = shader(params);

            params.vertId    = vertId.v[2];
            params.pVaryings = pVert2.varyings;
            pVert2.vert      = shader(params);
        #endif

        if (cullMode != SR_CULL_OFF)
        {
            const float det = face_determinant(pVert0.vert, pVert1.vert, pVert2.vert);

            // Using bitwise magic to reduce time spent making comparisons.
            // We can cull the backface with (det < 0.f) or cull the front
            // face with (det > 0.f).

            const bool culled = (cullMode == SR_CULL_BACK_FACE) ^ (det > 0.f);
            //if ((cullMode == SR_CULL_BACK_FACE && det < 0.f)
            //|| (cullMode == SR_CULL_FRONT_FACE && det > 0.f))
            if (culled)
            {
                continue;
            }
        }

        // Clip-space culling
        const SR_ClipStatus visStatus = face_visible(pVert0.vert, pVert1.vert, pVert2.vert);
        switch (visStatus)
        {
            case SR_TRIANGLE_FULLY_VISIBLE:
                sr_perspective_divide3(pVert0.vert, pVert1.vert, pVert2.vert);
                sr_world_to_screen_coords_divided3(pVert0.vert, pVert1.vert, pVert2.vert, widthScale, heightScale);
                push_bin<SR_RenderMode::RENDER_MODE_TRIANGLES, 3>(fboW, fboH, pVert0, pVert1, pVert2);
                break;

            case SR_TRIANGLE_PARTIALLY_VISIBLE:
                clip_and_process_tris(fboW, fboH, pVert0, pVert1, pVert2);

            default:
                break;
        }
    }
}



/*--------------------------------------
 * Process Vertices
--------------------------------------*/
void SR_VertexProcessor::execute() noexcept
{
    if (mNumInstances == 1)
    {
        if (mRenderMode & (RENDER_MODE_POINTS | RENDER_MODE_INDEXED_POINTS))
        {
            for (size_t i = 0; i < mNumMeshes; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_points(mMeshes[i], 0);
            }
        }
        else if (mRenderMode & (RENDER_MODE_LINES | RENDER_MODE_INDEXED_LINES))
        {
            for (size_t i = 0; i < mNumMeshes; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_lines(mMeshes[i], 0);
            }
        }
        else if (mRenderMode & (RENDER_MODE_TRIANGLES | RENDER_MODE_INDEXED_TRIANGLES | RENDER_MODE_TRI_WIRE | RENDER_MODE_INDEXED_TRI_WIRE))
        {
            for (size_t i = 0; i < mNumMeshes; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_tris(mMeshes[i], 0);
            }
        }
    }
    else
    {
        if (mRenderMode & (RENDER_MODE_POINTS | RENDER_MODE_INDEXED_POINTS))
        {
            for (size_t i = 0; i < mNumInstances; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_points(mMeshes[0], i);
            }
        }
        else if (mRenderMode & (RENDER_MODE_LINES | RENDER_MODE_INDEXED_LINES))
        {
            for (size_t i = 0; i < mNumInstances; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_lines(mMeshes[0], i);
            }
        }
        else if (mRenderMode & (RENDER_MODE_TRIANGLES | RENDER_MODE_INDEXED_TRIANGLES | RENDER_MODE_TRI_WIRE | RENDER_MODE_INDEXED_TRI_WIRE))
        {
            for (size_t i = 0; i < mNumInstances; ++i)
            {
                if (mFragProcessors->count.load(std::memory_order_consume))
                {
                    flush_bins();
                }

                process_tris(mMeshes[0], i);
            }
        }
    }

    if (mBinsUsed[mThreadId].count)
    {
        flush_bins();
    }

    mBusyProcessors->count.fetch_sub(1, std::memory_order_acq_rel);
    while (mBusyProcessors->count.load(std::memory_order_consume))
    {
        if (mFragProcessors->count.load(std::memory_order_consume))
        {
            flush_bins();
        }

        #if defined(LS_ARCH_X86)
            _mm_pause();
        #elif defined(LS_COMPILER_CLANG) && defined(LS_ARCH_AARCH64)
            __yield();
        #endif
    }
}
