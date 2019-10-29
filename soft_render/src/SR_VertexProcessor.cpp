
#include "lightsky/setup/Macros.h"

#include "lightsky/setup/OS.h"

#ifdef LS_OS_UNIX
    #include <time.h> // nanosleep, time_spec
#endif

#include "lightsky/utils/Assertions.h" // LS_DEBUG_ASSERT
#include "lightsky/utils/Log.h" // utils::LS_LOG...()
#include "lightsky/utils/Sort.hpp" // utils::sort_quick

#include "lightsky/math/mat_utils.h"
#include "lightsky/math/vec4.h"
#include "lightsky/math/vec_utils.h"

#include "soft_render/SR_Context.hpp"
#include "soft_render/SR_FragmentProcessor.hpp"
#include "soft_render/SR_IndexBuffer.hpp"
#include "soft_render/SR_Shader.hpp"
#include "soft_render/SR_ShaderProcessor.hpp"
#include "soft_render/SR_VertexArray.hpp"
#include "soft_render/SR_VertexBuffer.hpp"
#include "soft_render/SR_VertexProcessor.hpp"

#ifndef SR_VERTEX_CLIPPING_ENABLED
    #define SR_VERTEX_CLIPPING_ENABLED 1
#endif /* SR_VERTEX_CLIPPING_ENABLED */

#ifndef SR_VERTEX_CACHING_ENABLED
    #define SR_VERTEX_CACHING_ENABLED 0
#endif /* SR_VERTEX_CACHING_ENABLED */



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
    static constexpr unsigned PTV_CACHE_SIZE = 4;

    static constexpr unsigned PTV_CACHE_MISS = 0xFFFFFFFFu;

    size_t lruIndices[PTV_CACHE_SIZE];

    long long lruCounts[PTV_CACHE_SIZE];

    math::vec4 lruVertices[PTV_CACHE_SIZE];

    math::vec4 lruVaryings[PTV_CACHE_SIZE][SR_SHADER_MAX_VARYING_VECTORS];

    const SR_VertexArray* vao;

    const SR_VertexBuffer* vbo;

    const SR_UniformBuffer* uniforms;

    ls::math::vec4_t<float> (*shader)(
        const size_t             vertId,
        const SR_VertexArray&    vao,
        const SR_VertexBuffer&   vbo,
        const SR_UniformBuffer*  uniforms,
        ls::math::vec4_t<float>* varyings
    );

    SR_PTVCache(const SR_PTVCache&) = delete;
    SR_PTVCache(SR_PTVCache&&) = delete;
    SR_PTVCache& operator=(const SR_PTVCache&) = delete;
    SR_PTVCache& operator=(SR_PTVCache&&) = delete;

    SR_PTVCache(
    ls::math::vec4_t<float> (*pShader)(
        const size_t             vertId,
        const SR_VertexArray&    vao,
        const SR_VertexBuffer&   vbo,
        const SR_UniformBuffer*  uniforms,
        ls::math::vec4_t<float>* varyings
    ),
    const SR_UniformBuffer* pUniforms,
    const SR_VertexArray* pVao,
    const SR_VertexBuffer* pVbo) noexcept
    {
        for (size_t& index : lruIndices)
        {
            index = ~(size_t)0;
        }

        for (long long& count : lruCounts)
        {
            count = -1;
        }

        vao = pVao;
        vbo = pVbo;
        uniforms = pUniforms;
        shader = pShader;
    }

    inline unsigned query(size_t index, math::vec4& outVert, math::vec4* outVaryings, unsigned numVaryings) noexcept
    {
        size_t ret = PTV_CACHE_MISS;

        for (unsigned i = PTV_CACHE_SIZE; i--;)
        {
            if (lruIndices[i] == index)
            {
                lruCounts[i] = 0;
                outVert = lruVertices[i];

                for (unsigned v = numVaryings; v--;)
                {
                    outVaryings[v] = lruVaryings[i][v];
                }

                ret = index;
            }
            else
            {
                if (lruCounts[i] >= 0)
                {
                    ++lruCounts[i];
                }
            }
        }

        return ret;
    }

    inline void update(size_t index, const math::vec4& vert, const math::vec4* inVaryings, unsigned numVaryings) noexcept
    {
        size_t nextIndex = 0;
        long long maxCount = 0;

        for (unsigned i = PTV_CACHE_SIZE; i--;)
        {
            if (maxCount < lruCounts[i])
            {
                nextIndex = i;

                if (lruCounts[i] < 0)
                {
                    break;
                }

                maxCount = lruCounts[i];
            }
        }

        lruIndices[nextIndex] = index;
        lruCounts[nextIndex] = 0;
        lruVertices[nextIndex] = vert;

        for (unsigned v = numVaryings; v--;)
        {
            lruVaryings[nextIndex][v] = inVaryings[v];
        }
    }

    inline void query_and_update(size_t index, math::vec4& outVert, math::vec4* outVaryings, unsigned numVaryings) noexcept
    {
        size_t ret = PTV_CACHE_MISS;
        size_t nextIndex = 0;
        long long maxCount = 0;

        for (unsigned i = PTV_CACHE_SIZE; i--;)
        {
            if (maxCount < lruCounts[i])
            {
                nextIndex = i;
            }

            if (lruIndices[i] == index)
            {
                lruCounts[i] = 0;
                outVert = lruVertices[i];

                for (unsigned v = numVaryings; v--;)
                {
                    outVaryings[v] = lruVaryings[i][v];
                }

                ret = index;
            }
            else
            {
                if (lruCounts[i] >= 0)
                {
                    ++lruCounts[i];
                }
            }
        }

        if (ret == PTV_CACHE_MISS)
        {
            lruIndices[nextIndex] = index;
            lruCounts[nextIndex] = 0;
            outVert = lruVertices[nextIndex] = shader(index, *vao, *vbo, uniforms, outVaryings);

            for (unsigned v = numVaryings; v--;)
            {
                lruVaryings[nextIndex][v] = outVaryings[v];
            }
        }
    }
};
#endif



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline void sr_perspective_divide(math::vec4& v) noexcept
{
    const math::vec4&& wInv = math::rcp(math::vec4{v[3]});
    v *= wInv;
    v[3] = wInv[0];
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline LS_INLINE void sr_world_to_screen_coords_divided(math::vec4& v, const float widthScale, const float heightScale) noexcept
{
    //v[0] = math::fmadd(widthScale, v[0], widthScale);
    //v[1] = math::fmadd(heightScale, v[1], heightScale);
    v = math::fmadd(math::vec4{widthScale, heightScale, 1.f, 1.f}, v, math::vec4{widthScale, heightScale, 0.f, 0.f});
}



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline void sr_world_to_screen_coords(math::vec4& v, const float widthScale, const float heightScale) noexcept
{
    const float wInv = 1.f / v.v[3];
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
inline size_t get_next_vertex(const SR_IndexBuffer* pIbo, size_t vId) noexcept
{
    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:  return *reinterpret_cast<const unsigned char*>(pIbo->element(vId));
        case VERTEX_DATA_SHORT: return *reinterpret_cast<const unsigned short*>(pIbo->element(vId));
        case VERTEX_DATA_INT:   return *reinterpret_cast<const unsigned int*>(pIbo->element(vId));
        default:
            LS_DEBUG_ASSERT(false);
            break;
    }
    return vId;
}



inline math::vec3_t<size_t> get_next_vertex3(const SR_IndexBuffer* pIbo, size_t vId) noexcept
{
    math::vec3_t<unsigned char> byteIds;
    math::vec3_t<unsigned short> shortIds;
    math::vec3_t<unsigned int> intIds;

    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:
            byteIds = *reinterpret_cast<const decltype(byteIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)byteIds;
            break;

        case VERTEX_DATA_SHORT:
            shortIds = *reinterpret_cast<const decltype(shortIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)shortIds;
            break;

        case VERTEX_DATA_INT:
            intIds = *reinterpret_cast<const decltype(intIds)*>(pIbo->element(vId));
            return (math::vec3_t<size_t>)intIds;
            break;

        default:
            LS_DEBUG_ASSERT(false);
    }

    return math::vec3_t<size_t>{0, 0, 0};
}



/*--------------------------------------
 * Cull backfaces of a triangle
--------------------------------------*/
inline LS_INLINE bool backface_visible(const math::vec4 screenCoords[SR_SHADER_MAX_SCREEN_COORDS]) noexcept
{
    //return (0.f <= math::dot(math::vec4{0.f, 0.f, 1.f, 1.f}, math::cross(screenCoords[1]-screenCoords[0], screenCoords[2]-screenCoords[0])));
    const math::mat3 det{
        math::vec3{screenCoords[0][0], screenCoords[0][1], screenCoords[0][3]},
        math::vec3{screenCoords[1][0], screenCoords[1][1], screenCoords[1][3]},
        math::vec3{screenCoords[2][0], screenCoords[2][1], screenCoords[2][3]}
    };
    return (0.f < math::determinant(det));
}



/*--------------------------------------
 * Cull frontfaces of a triangle
--------------------------------------*/
inline LS_INLINE bool frontface_visible(const math::vec4 screenCoords[SR_SHADER_MAX_SCREEN_COORDS]) noexcept
{
    //return (0.f >= math::dot(math::vec4{0.f, 0.f, 1.f, 1.f}, math::cross(screenCoords[1]-screenCoords[0], screenCoords[2]-screenCoords[0])));
    const math::mat3 det{
        math::vec3{screenCoords[0][0], screenCoords[0][1], screenCoords[0][3]},
        math::vec3{screenCoords[1][0], screenCoords[1][1], screenCoords[1][3]},
        math::vec3{screenCoords[2][0], screenCoords[2][1], screenCoords[2][3]}
    };
    return (0.f > math::determinant(det));
}



/*--------------------------------------
 * Cull only triangle outside of the screen
--------------------------------------*/
inline LS_INLINE SR_ClipStatus face_visible(const math::vec4 clipCoords[SR_SHADER_MAX_WORLD_COORDS]) noexcept
{
    #if SR_VERTEX_CLIPPING_ENABLED != 0
        if(-clipCoords[0][3] <= clipCoords[0][0] && clipCoords[0][3] >= clipCoords[0][0]
        && -clipCoords[0][3] <= clipCoords[0][1] && clipCoords[0][3] >= clipCoords[0][1]
        && -clipCoords[0][3] <= clipCoords[0][2] && clipCoords[0][3] >= clipCoords[0][2])
        {
            if(-clipCoords[1][3] <= clipCoords[1][0] && clipCoords[1][3] >= clipCoords[1][0]
            && -clipCoords[1][3] <= clipCoords[1][1] && clipCoords[1][3] >= clipCoords[1][1]
            && -clipCoords[1][3] <= clipCoords[1][2] && clipCoords[1][3] >= clipCoords[1][2])
            {
                if(-clipCoords[2][3] <= clipCoords[2][0] && clipCoords[2][3] >= clipCoords[2][0]
                && -clipCoords[2][3] <= clipCoords[2][1] && clipCoords[2][3] >= clipCoords[2][1]
                && -clipCoords[2][3] <= clipCoords[2][2] && clipCoords[2][3] >= clipCoords[2][2])
                {
                    return SR_TRIANGLE_FULLY_VISIBLE;
                }
            }
        }

        if (clipCoords[0][3] >= 1.f || clipCoords[1][3] >= 1.f || clipCoords[2][3] >= 1.f)
        {
            return SR_TRIANGLE_PARTIALLY_VISIBLE;
        }
    #else
        if (math::min(clipCoords[0][3], clipCoords[1][3], clipCoords[2][3]) >= 0.f)
        {
            // something's visible, but near-plane clipping might say otherwise
            return SR_TRIANGLE_PARTIALLY_VISIBLE;
        }
    #endif /* SR_VERTEX_CLIPPING_ENABLED */

    return SR_TRIANGLE_NOT_VISIBLE;
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
    // Sync Point 1 indicates that all triangles have been sorted.
    // Since we only have two sync points for the bins to process, a negative
    // value for mFragProcessors will indicate all threads are ready to process
    // fragments. A zero-value for mFragProcessors indicates we can continue
    // processing vertices.
    const int_fast64_t   syncPoint1 = -mNumThreads-1;
    int_fast64_t         tileId     = mFragProcessors->fetch_add(1, std::memory_order_acq_rel);

    #if defined(LS_OS_UNIX)
        struct timespec sleepAmt;
        sleepAmt.tv_sec = 0;
        sleepAmt.tv_nsec = 1;
    #endif

    // Sort the bins based on their depth.
    if (tileId == mNumThreads-1u)
    {
        #if 1
        const uint_fast64_t maxElements = math::min<uint64_t>(mBinsUsed->load(std::memory_order_consume), SR_SHADER_MAX_PRIM_BINS);

        // Blended fragments get sorted back-to-front for correct coloring.
        if (mShader->fragment_shader().blend == SR_BLEND_OFF)
        {
            ls::utils::sort_quick_iterative<uint32_t>(mBinIds, maxElements, [&](uint32_t a, uint32_t b)->bool {
                return mFragBins[a] > mFragBins[b];
            });
        }
        else
        {
            // Sort opaque objects from front-to-back to fortify depth testing.
            ls::utils::sort_quick_iterative<uint32_t>(mBinIds, maxElements, [&](uint32_t a, uint32_t b)->bool {
                return mFragBins[a] < mFragBins[b];
            });
        }
        #endif

        // Let all threads know they can process fragments.
        mFragProcessors->store(syncPoint1, std::memory_order_release);
    }
    else
    {
        while (mFragProcessors->load(std::memory_order_consume) > 0)
        {
            // Wait until the bins are sorted
            #if defined(LS_ARCH_X86)
                _mm_pause();
            #elif defined(LS_OS_LINUX) || defined(LS_OS_ANDROID)
                clock_nanosleep(CLOCK_MONOTONIC, 0, &sleepAmt, nullptr);
            #elif defined(LS_OS_OSX) || defined(LS_OS_IOS) || defined(LS_OS_IOS_SIM)
                nanosleep(&sleepAmt, nullptr);
            #else
                std::this_thread::yield();
            #endif
        }
    }

    SR_FragmentProcessor fragTask{
        (uint16_t)tileId,
        mMesh.mode,
        (uint32_t)mNumThreads,
        (float)(mFboW - 1),
        (float)(mFboH - 1),
        math::min<uint64_t>(mBinsUsed->load(std::memory_order_consume), SR_SHADER_MAX_PRIM_BINS),
        mShader,
        mFbo,
        mBinIds,
        mFragBins,
        mVaryings + tileId * SR_SHADER_MAX_VARYING_VECTORS * SR_SHADER_MAX_FRAG_QUEUES,
        mFragQueues + tileId
    };

    fragTask.execute();

    // Indicate to all threads we can now process more vertices
    tileId = mFragProcessors->fetch_add(1, std::memory_order_acq_rel);

    // Wait for the last thread to reset the number of available bins.
    while (mFragProcessors->load(std::memory_order_consume) < 0)
    {
        if (tileId == -2)
        {
            mBinsUsed->store(0, std::memory_order_release);
            mFragProcessors->store(0, std::memory_order_release);

        }
        else
        {
            // wait until all fragments are rendered across the other threads
            #if defined(LS_OS_LINUX) || defined(LS_OS_ANDROID)
                clock_nanosleep(CLOCK_MONOTONIC, 0, &sleepAmt, nullptr);
            #elif defined(LS_OS_OSX) || defined(LS_OS_IOS) || defined(LS_OS_IOS_SIM)
                nanosleep(&sleepAmt, nullptr);
            #elif defined(LS_ARCH_X86)
                _mm_pause();
            #else
                std::this_thread::yield();
            #endif
        }
    }
}



/*--------------------------------------
 * Publish a vertex to a fragment thread
--------------------------------------*/
void SR_VertexProcessor::push_bin(
    float fboW,
    float fboH,
    math::vec4* const screenCoords,
    const math::vec4* varyings
) const noexcept
{
    const uint_fast32_t numVaryings = mShader->get_num_varyings();
    unsigned numVerts;
    const SR_Mesh m = mMesh;
    std::atomic_uint_fast64_t* const pLocks = mBinsUsed;

    const math::vec4& p0 = screenCoords[0];
    const math::vec4& p1 = screenCoords[1];
    const math::vec4& p2 = screenCoords[2];
    float bboxMinX;
    float bboxMinY;
    float bboxMaxX;
    float bboxMaxY;

    // calculate the bounds of the tile which a certain thread will be
    // responsible for

    // render points through whichever tile/thread they appear in
    switch (m.mode & (RENDER_MODE_POINTS|RENDER_MODE_LINES|RENDER_MODE_TRIANGLES))
    {
        case RENDER_MODE_POINTS:
            numVerts = 1;
            bboxMinX = p0[0];
            bboxMaxX = p0[0];
            bboxMinY = p0[1];
            bboxMaxY = p0[1];
            break;

        case RENDER_MODE_LINES:
            // establish a bounding box to detect overlap with a thread's tiles
            numVerts = 2;
            bboxMinX = math::min(p0[0], p1[0]);
            bboxMinY = math::min(p0[1], p1[1]);
            bboxMaxX = math::max(p0[0], p1[0]);
            bboxMaxY = math::max(p0[1], p1[1]);
            break;

        case RENDER_MODE_TRIANGLES:
            // establish a bounding box to detect overlap with a thread's tiles
            numVerts = 3;
            bboxMinX = math::min(p0[0], p1[0], p2[0]);
            bboxMinY = math::min(p0[1], p1[1], p2[1]);
            bboxMaxX = math::max(p0[0], p1[0], p2[0]);
            bboxMaxY = math::max(p0[1], p1[1], p2[1]);
            break;

        default:
            return;
    }

    int isPrimVisible = (bboxMaxX >= 0.f && fboW >= bboxMinX && bboxMaxY >= 0.f && fboH >= bboxMinY);
    isPrimVisible = isPrimVisible && (bboxMaxX-math::ceil(bboxMinX) > 0.0f) && (bboxMaxY-math::ceil(bboxMinY) > 0.f);

    if (isPrimVisible)
    {
        SR_FragmentBin* const pFragBins = mFragBins;

        // Check if the output bin is full
        uint_fast64_t binId;

        // Attempt to grab a bin index. Flush the bins if they've filled up.
        while ((binId = pLocks->fetch_add(1, std::memory_order_acq_rel)) >= SR_SHADER_MAX_PRIM_BINS)
        {
            flush_bins();
        }

        // place a triangle into the next available bin
        mBinIds[binId] = (uint32_t)binId;

        // Copy all per-vertex coordinates and varyings to the fragment bins
        // which will need the data for interpolation. The perspective-divide
        // is only used for rendering triangles.
        SR_FragmentBin& bin = pFragBins[binId];
        bin.mScreenCoords[0] = p0;
        bin.mScreenCoords[1] = p1;
        bin.mScreenCoords[2] = p2;

        const float denom = 1.f / ((p0[0]-p2[0])*(p1[1]-p0[1]) - (p0[0]-p1[0])*(p2[1]-p0[1]));
        bin.mBarycentricCoords[0] = denom*math::vec4(p1[1]-p2[1], p2[1]-p0[1], p0[1]-p1[1], 0.f);
        bin.mBarycentricCoords[1] = denom*math::vec4(p2[0]-p1[0], p0[0]-p2[0], p1[0]-p0[0], 0.f);
        bin.mBarycentricCoords[2] = denom*math::vec4(
            p1[0]*p2[1] - p2[0]*p1[1],
            p2[0]*p0[1] - p0[0]*p2[1],
            p0[0]*p1[1] - p1[0]*p0[1],
            0.f
        );

        while (numVerts--)
        {
            const unsigned offset = numVerts * SR_SHADER_MAX_VARYING_VECTORS;
            for (unsigned i = numVaryings; i--;)
            {
                bin.mVaryings[i+offset] = varyings[i+offset];
            }
        }
    }

    while (pLocks->load(std::memory_order_consume) >= SR_SHADER_MAX_PRIM_BINS)
    {
        flush_bins();
    }
}



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
    ls::math::vec4 vertCoords[SR_SHADER_MAX_SCREEN_COORDS],
    ls::math::vec4 pVaryings[SR_SHADER_MAX_VARYING_VECTORS * SR_SHADER_MAX_SCREEN_COORDS]) noexcept
{
    const SR_VertexShader vertShader    = mShader->mVertShader;
    const unsigned        numVarys      = (unsigned)vertShader.numVaryings;
    const float           fboW          = (float)mFboW;
    const float           fboH          = (float)mFboH;
    const float           widthScale    = fboW * 0.5f;
    const float           heightScale   = fboH * 0.5f;
    constexpr unsigned    numTempVerts  = 9; // at most 9 vertices should be generated
    unsigned              numTotalVerts = 3;
    math::vec4            tempVerts     [numTempVerts];
    math::vec4            newVerts      [numTempVerts];
    math::vec4            tempVarys     [numTempVerts * SR_SHADER_MAX_VARYING_VECTORS];
    math::vec4            newVarys      [numTempVerts * SR_SHADER_MAX_VARYING_VECTORS];
    const math::vec4      clipEdges[]  = {
        { 0.f,  0.f, -1.f, 1.f},
        { 0.f, -1.f,  0.f, 1.f},
        {-1.f,  0.f,  0.f, 1.f},
        { 0.f,  0.f,  1.f, 1.f},
        { 0.f,  1.f,  0.f, 1.f},
        { 1.f,  0.f,  0.f, 1.f}
    };

    const auto _copy_verts = [](int maxVerts, const math::vec4* inVerts, math::vec4* outVerts) noexcept->void
    {
        for (unsigned i = maxVerts; i--;)
        {
            outVerts[i] = inVerts[i];
        }
    };

    const auto _interpolate_varyings = [&numVarys](const math::vec4* inVarys, math::vec4* outVarys, int fromIndex, int toIndex, float amt)->void
    {
        const math::vec4* pV0 = inVarys + fromIndex * SR_SHADER_MAX_VARYING_VECTORS;
        const math::vec4* pV1 = inVarys + toIndex * SR_SHADER_MAX_VARYING_VECTORS;

        for (unsigned i = numVarys; i--;)
        {
            *outVarys++ = math::mix(*pV0++, *pV1++, amt);
        }
    };

    _copy_verts(3, vertCoords, newVerts);
    _copy_verts(numVarys, pVaryings, newVarys);
    _copy_verts(numVarys, pVaryings+SR_SHADER_MAX_VARYING_VECTORS, newVarys+SR_SHADER_MAX_VARYING_VECTORS);
    _copy_verts(numVarys, pVaryings+SR_SHADER_MAX_VARYING_VECTORS*2, newVarys+SR_SHADER_MAX_VARYING_VECTORS*2);

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

    if (numTotalVerts == 3)
    {
        math::vec4& v0 = newVerts[0];
        math::vec4& v1 = newVerts[1];
        math::vec4& v2 = newVerts[2];

        sr_perspective_divide(v0);
        sr_perspective_divide(v1);
        sr_perspective_divide(v2);

        sr_world_to_screen_coords_divided(v0, widthScale, heightScale);
        sr_world_to_screen_coords_divided(v1, widthScale, heightScale);
        sr_world_to_screen_coords_divided(v2, widthScale, heightScale);

        //push_bin(fboW, fboH, newVerts, pVaryings);
        push_bin(fboW, fboH, newVerts, newVarys);
        return;
    }

    tempVerts[0] = newVerts[0];
    _copy_verts(numVarys, newVarys, tempVarys);
    sr_perspective_divide(tempVerts[0]);
    sr_world_to_screen_coords_divided(tempVerts[0], widthScale, heightScale);

    for (unsigned i = numTotalVerts-2; i--;)
    {
        unsigned j = i+1;
        unsigned k = i+2;

        tempVerts[1] = newVerts[j];
        _copy_verts(numVarys, newVarys+(j*SR_SHADER_MAX_VARYING_VECTORS), tempVarys+(1*SR_SHADER_MAX_VARYING_VECTORS));

        tempVerts[2] = newVerts[k];
        _copy_verts(numVarys, newVarys+(k*SR_SHADER_MAX_VARYING_VECTORS), tempVarys+(2*SR_SHADER_MAX_VARYING_VECTORS));

        math::vec4& v1 = tempVerts[1];
        math::vec4& v2 = tempVerts[2];

        sr_perspective_divide(v1);
        sr_perspective_divide(v2);

        sr_world_to_screen_coords_divided(v1, widthScale, heightScale);
        sr_world_to_screen_coords_divided(v2, widthScale, heightScale);

        //push_bin(fboW, fboH, newVerts+i, pVaryings);
        push_bin(fboW, fboH, tempVerts, tempVarys);
    }
}



/*--------------------------------------
 * Process Vertices
--------------------------------------*/
void SR_VertexProcessor::execute() noexcept
{
    ls::math::vec4          vertCoords  [SR_SHADER_MAX_SCREEN_COORDS];
    math::vec4              pVaryings   [SR_SHADER_MAX_VARYING_VECTORS * SR_SHADER_MAX_SCREEN_COORDS];
    const SR_VertexShader   vertShader  = mShader->mVertShader;
    const SR_CullMode       cullMode    = vertShader.cullMode;
    const auto              shader      = vertShader.shader;
    const SR_UniformBuffer* pUniforms   = mShader->mUniforms;
    const SR_VertexArray&   vao         = mContext->vao(mMesh.vaoId);
    const SR_VertexBuffer&  vbo         = mContext->vbo(vao.get_vertex_buffer());
    const float             fboW        = (float)mFboW;
    const float             fboH        = (float)mFboH;
    const float             widthScale  = fboW * 0.5f;
    const float             heightScale = fboH * 0.5f;
    const size_t            numVerts    = mMesh.elementEnd - mMesh.elementBegin;
    size_t                  begin;
    size_t                  end;
    const SR_IndexBuffer*   pIbo;

    #if SR_VERTEX_CACHING_ENABLED
        SR_PTVCache ptvCache{shader, pUniforms, &vao, &vbo};
        const size_t numVaryings = vertShader.numVaryings;
    #endif

    if (vao.has_index_buffer())
    {
        pIbo = &mContext->ibo(vao.get_index_buffer());
    }

    LS_PREFETCH(pUniforms, LS_PREFETCH_ACCESS_RW, LS_PREFETCH_LEVEL_L1);
    LS_PREFETCH(pVaryings, LS_PREFETCH_ACCESS_RW, LS_PREFETCH_LEVEL_L1);

    if (mMesh.mode & (RENDER_MODE_POINTS | RENDER_MODE_INDEXED_POINTS))
    {
        const bool usingIndices = mMesh.mode == RENDER_MODE_INDEXED_POINTS;

        sr_calc_indexed_parition<1>(numVerts, mNumThreads, mThreadId, begin, end);
        begin += mMesh.elementBegin;
        end += mMesh.elementBegin;

        for (size_t i = begin; i < end; ++i)
        {
            const size_t vertId0 = usingIndices ? get_next_vertex(pIbo, i) : i;

            vertCoords[0] = shader(vertId0, vao, vbo, pUniforms, pVaryings);

            if (vertCoords[0][3] > 0.f)
            {
                sr_world_to_screen_coords(vertCoords[0], widthScale, heightScale);
                push_bin(fboW, fboH, vertCoords, pVaryings);
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_LINES || mMesh.mode == RENDER_MODE_INDEXED_LINES)
    {
        const bool usingIndices = mMesh.mode == RENDER_MODE_INDEXED_LINES;

        sr_calc_indexed_parition<2>(numVerts, mNumThreads, mThreadId, begin, end);
        begin += mMesh.elementBegin;
        end += mMesh.elementBegin;

        for (size_t i = begin; i < end; i += 2)
        {
            const size_t index0  = i;
            const size_t index1  = i + 1;
            const size_t vertId0 = usingIndices ? get_next_vertex(pIbo, index0) : index0;
            const size_t vertId1 = usingIndices ? get_next_vertex(pIbo, index1) : index1;

            vertCoords[0] = shader(vertId0, vao, vbo, pUniforms, pVaryings);
            vertCoords[1] = shader(vertId1, vao, vbo, pUniforms, pVaryings + SR_SHADER_MAX_VARYING_VECTORS);

            if (vertCoords[0][3] >= 0.f && vertCoords[1][3] >= 0.f)
            {
                sr_world_to_screen_coords(vertCoords[0], widthScale, heightScale);
                sr_world_to_screen_coords(vertCoords[1], widthScale, heightScale);

                push_bin(fboW, fboH, vertCoords, pVaryings);
            }
        }
    }
    else if (mMesh.mode & (RENDER_MODE_TRIANGLES | RENDER_MODE_INDEXED_TRIANGLES | RENDER_MODE_TRI_WIRE | RENDER_MODE_INDEXED_TRI_WIRE))
    {
        const int usingIndices = mMesh.mode & ((RENDER_MODE_INDEXED_TRIANGLES | RENDER_MODE_INDEXED_TRI_WIRE) ^ (RENDER_MODE_TRIANGLES | RENDER_MODE_TRI_WIRE));

        sr_calc_indexed_parition<3>(numVerts, mNumThreads, mThreadId, begin, end);
        begin += mMesh.elementBegin;
        end += mMesh.elementBegin;

        for (size_t i = begin; i < end; i += 3)
        {
            const math::vec3_t<size_t>&& vertId = usingIndices ? get_next_vertex3(pIbo, i) : math::vec3_t<size_t>{i, i+1, i+2};

            #if SR_VERTEX_CACHING_ENABLED
                ptvCache.query_and_update(vertId[0], vertCoords[0], pVaryings, numVaryings);
                ptvCache.query_and_update(vertId[1], vertCoords[1], pVaryings + SR_SHADER_MAX_VARYING_VECTORS, numVaryings);
                ptvCache.query_and_update(vertId[2], vertCoords[2], pVaryings + SR_SHADER_MAX_VARYING_VECTORS * 2, numVaryings);
            #else
                vertCoords[0] = shader(vertId[0], vao, vbo, pUniforms, pVaryings);
                vertCoords[1] = shader(vertId[1], vao, vbo, pUniforms, pVaryings + SR_SHADER_MAX_VARYING_VECTORS);
                vertCoords[2] = shader(vertId[2], vao, vbo, pUniforms, pVaryings + (SR_SHADER_MAX_VARYING_VECTORS * 2));
            #endif

            if ((cullMode == SR_CULL_BACK_FACE && !backface_visible(vertCoords))
            ||  (cullMode == SR_CULL_FRONT_FACE && !frontface_visible(vertCoords)))
            {
                continue;
            }
            // Clip-space culling
            const SR_ClipStatus visStatus = face_visible(vertCoords);
            if (visStatus == SR_TRIANGLE_NOT_VISIBLE)
            {
                continue;
            }

            #if SR_VERTEX_CLIPPING_ENABLED == 0
                sr_perspective_divide(vertCoords[0]);
                sr_perspective_divide(vertCoords[1]);
                sr_perspective_divide(vertCoords[2]);

                sr_world_to_screen_coords_divided(vertCoords[0], widthScale, heightScale);
                sr_world_to_screen_coords_divided(vertCoords[1], widthScale, heightScale);
                sr_world_to_screen_coords_divided(vertCoords[2], widthScale, heightScale);

                push_bin(fboW, fboH, vertCoords, pVaryings);
            #else
                if (visStatus == SR_TRIANGLE_FULLY_VISIBLE)
                {
                    sr_perspective_divide(vertCoords[0]);
                    sr_perspective_divide(vertCoords[1]);
                    sr_perspective_divide(vertCoords[2]);

                    sr_world_to_screen_coords_divided(vertCoords[0], widthScale, heightScale);
                    sr_world_to_screen_coords_divided(vertCoords[1], widthScale, heightScale);
                    sr_world_to_screen_coords_divided(vertCoords[2], widthScale, heightScale);

                    push_bin(fboW, fboH, vertCoords, pVaryings);
                }
                else
                {
                    clip_and_process_tris(vertCoords, pVaryings);
                }
            #endif
        }
    }

    mBusyProcessors->fetch_sub(1, std::memory_order_acq_rel);
    while (mBusyProcessors->load(std::memory_order_consume) > 0)
    {
        #if defined(LS_ARCH_X86)
            _mm_pause();
        #endif

        if (mFragProcessors->load(std::memory_order_consume))
        {
            flush_bins();
        }
    }

    if (mBinsUsed->load(std::memory_order_consume))
    {
        flush_bins();
    }
}
