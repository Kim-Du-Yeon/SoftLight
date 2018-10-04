
#include "lightsky/setup/Api.h" // LS_IMPERATIVE

#include "lightsky/utils/Copy.h" // fast_memset

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



/*-----------------------------------------------------------------------------
 * Anonymous Helper Functions
-----------------------------------------------------------------------------*/
namespace math = ls::math;



namespace
{



/*--------------------------------------
 * Convert world coordinates to screen coordinates (temporary until NDC clipping is added)
--------------------------------------*/
inline math::vec4 world_to_screen_3(const math::vec4& v, const float widthScale, const float heightScale) noexcept
{
    const float wInv = 1.f / v.v[3];
    math::vec4&& temp = v * wInv;

    temp[0] = widthScale  + temp[0] * widthScale;
    temp[1] = heightScale + temp[1] * heightScale;

    return math::vec4{temp[0], temp[1], temp[2], 0.f};
}



/*--------------------------------------
 * Get the next vertex from an IBO
--------------------------------------*/
inline uint32_t get_next_vertex(const SR_IndexBuffer* pIbo, uint32_t vId) noexcept
{
    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:  return *reinterpret_cast<const unsigned char*>(pIbo->element(vId));
        case VERTEX_DATA_SHORT: return *reinterpret_cast<const unsigned short*>(pIbo->element(vId));
        case VERTEX_DATA_INT:   return *reinterpret_cast<const unsigned int*>(pIbo->element(vId));
        default:
            assert(false);
            break;
    }
    return vId;
}



inline math::vec4_t<uint32_t> get_next_vertex3(const SR_IndexBuffer* pIbo, uint32_t vId) noexcept
{
    math::vec4_t<uint32_t> ret;

    switch (pIbo->type())
    {
        case VERTEX_DATA_BYTE:
            ret[0] = *reinterpret_cast<const unsigned char*>(pIbo->element(vId+0));
            ret[1] = *reinterpret_cast<const unsigned char*>(pIbo->element(vId+1));
            ret[2] = *reinterpret_cast<const unsigned char*>(pIbo->element(vId+2));
            break;

        case VERTEX_DATA_SHORT:
            ret[0] = *reinterpret_cast<const unsigned short*>(pIbo->element(vId+0));
            ret[1] = *reinterpret_cast<const unsigned short*>(pIbo->element(vId+1));
            ret[2] = *reinterpret_cast<const unsigned short*>(pIbo->element(vId+2));
            break;

        case VERTEX_DATA_INT:
            ret[0] = *reinterpret_cast<const unsigned int*>(pIbo->element(vId+0));
            ret[1] = *reinterpret_cast<const unsigned int*>(pIbo->element(vId+1));
            ret[2] = *reinterpret_cast<const unsigned int*>(pIbo->element(vId+2));
            break;

        default:
            abort();
            break;
    }

    return ret;
}



/*--------------------------------------
 * Cull backfaces of a triangle
--------------------------------------*/
inline bool cull_triangle(const math::vec4* screenCoords, const math::vec4* worldCoords) noexcept
{
    // Re-enable the commented section to test screen-space culling
    return /*worldCoords[0][0] < worldCoords[0][3] &&
           worldCoords[0][1] < worldCoords[0][3] &&
           worldCoords[0][2] < worldCoords[0][3] &&
           worldCoords[1][0] < worldCoords[1][3] &&
           worldCoords[1][1] < worldCoords[1][3] &&
           worldCoords[1][2] < worldCoords[1][3] &&
           worldCoords[2][0] < /worldCoords[2][3] &&
           worldCoords[2][1] < worldCoords[2][3] &&
           worldCoords[2][2] < worldCoords[2][3] &&
           worldCoords[0][0] > -worldCoords[0][3] &&
           worldCoords[0][1] > -worldCoords[0][3] &&
           worldCoords[0][2] > -worldCoords[0][3] &&
           worldCoords[1][0] > -worldCoords[1][3] &&
           worldCoords[1][1] > -worldCoords[1][3] &&
           worldCoords[1][2] > -worldCoords[1][3] &&
           worldCoords[2][0] > -worldCoords[2][3] &&
           worldCoords[2][1] > -worldCoords[2][3] &&
           worldCoords[2][2] > -worldCoords[2][3] &&*/
           worldCoords[0][3] > 0.f &&
           worldCoords[1][3] > 0.f &&
           worldCoords[2][3] > 0.f &&
           (0.f < math::dot(math::vec4{0.f, 0.f, 1.f, 0.f}, math::normalize(math::cross(screenCoords[1]-screenCoords[0], screenCoords[2]-screenCoords[0]))));
}



} // end anonymous namespace



/*-----------------------------------------------------------------------------
 * SR_VertexProcessor Class
-----------------------------------------------------------------------------*/
/*-------------------------------------
 * Execute a fragment processor
-------------------------------------*/
void SR_VertexProcessor::flush_fragments(uint_fast32_t tileId, uint_fast32_t numBins) const noexcept
{
    // divide the screen into equal parts which can then be rendered by all
    // available fragment threads.
    const uint_fast32_t numTiles = mNumTiles;

    uint_fast32_t cols;
    uint_fast32_t rows;
    sr_calc_frag_tiles<uint_fast32_t>(numTiles, cols, rows);

    const uint_fast32_t fboW = mFboW / cols;
    const uint_fast32_t fboH = mFboH / rows;
    const uint_fast32_t tileX = fboW * (tileId % cols);
    const uint_fast32_t tileY = fboH * ((tileId / cols) % rows);

    SR_FragmentProcessor fragTask;
    fragTask.mShader  = mShader;
    fragTask.mFbo     = mFbo;
    fragTask.mBins    = mFragBins[tileId].data();
    fragTask.mBinId   = 0;
    fragTask.mNumBins = numBins;
    fragTask.mFboX0   = (float)tileX;
    fragTask.mFboY0   = (float)tileY;
    fragTask.mFboX1   = (float)(fboW + tileX - 1);
    fragTask.mFboY1   = (float)(fboH + tileY - 1);
    fragTask.mMode    = mMesh.mode;

    fragTask.execute();

    // indicate the bin is available for pushing
    mBinsUsed[tileId].store(1, std::memory_order_relaxed);
}



/*--------------------------------------
 * Publish a vertex to a fragment thread
--------------------------------------*/
void SR_VertexProcessor::push_fragments(
    uint32_t fboW,
    uint32_t fboH,
    ls::math::vec4* screenCoords,
    ls::math::vec4* worldCoords,
    const ls::math::vec4* varyings
) const noexcept
{
    const SR_Mesh m = mMesh;
    std::atomic_uint_fast32_t* const pLocks = mBinsUsed;
    std::array<SR_FragmentBin, SR_SHADER_MAX_FRAG_BINS>* const pFragBins = mFragBins;

    // Copy all per-vertex coordinates and varyings to the fragment bins which
    // will need the data for interpolation
    SR_FragmentBin bin;
    ls::utils::fast_memcpy(bin.mScreenCoords, screenCoords, sizeof(bin.mScreenCoords));
    bin.mPerspDivide = math::rcp(math::vec4{worldCoords[0][3], worldCoords[1][3], worldCoords[2][3], 1.f});
    ls::utils::fast_memcpy(bin.mVaryings, varyings, sizeof(bin.mVaryings));

    // divide the screen into equal parts which can then be rendered by all
    // available fragment threads.
    const uint32_t numTiles = mNumTiles;
    uint32_t cols;
    uint32_t rows;

    sr_calc_frag_tiles<uint32_t>(numTiles, cols, rows);
    fboW /= cols;
    fboH /= rows;

    const math::vec4& p0 = screenCoords[0];
    const math::vec4& p1 = screenCoords[1];
    const math::vec4& p2 = screenCoords[2];
    float bboxMinX = -1.f;
    float bboxMinY = -1.f;
    float bboxMaxX = -1.f;
    float bboxMaxY = -1.f;

    // calculate the bounds of the tile which a certain thread will be
    // responsible for

    // render points through whichever tile/thread they appear in
    if (m.mode & RENDER_MODE_POINTS)
    {
        bboxMinX = p0[0];
        bboxMaxX = p0[0];
        bboxMinY = p0[1];
        bboxMaxY = p0[1];
    }
    else if (m.mode & RENDER_MODE_LINES)
    {
        // establish a bounding box to detect overlap with a thread's tiles
        bboxMinX = math::min(p0[0], p1[0]);
        bboxMinY = math::min(p0[1], p1[1]);
        bboxMaxX = math::max(p0[0], p1[0]);
        bboxMaxY = math::max(p0[1], p1[1]);
    }
    else if (m.mode & RENDER_MODE_TRIANGLES)
    {
        // establish a bounding box to detect overlap with a thread's tiles
        bboxMinX = math::min(p0[0], p1[0], p2[0]);
        bboxMinY = math::min(p0[1], p1[1], p2[1]);
        bboxMaxX = math::max(p0[0], p1[0], p2[0]);
        bboxMaxY = math::max(p0[1], p1[1], p2[1]);
    }

    // render all lines & triangles
    for (uint16_t threadId = 0; threadId < numTiles; ++threadId)
    {
        const uint32_t x0 = fboW * (threadId % cols);
        const uint32_t y0 = fboH * ((threadId / cols) % rows);
        const uint32_t x1 = fboW + x0;
        const uint32_t y1 = fboH + y0;
        const int isFragVisible = (bboxMaxX >= (float)x0 && (float)x1 >= bboxMinX && bboxMaxY >= (float)y0 && (float)y1 >= bboxMinY);

        if (isFragVisible)
        {
            // Check if the  output bin is full
            uint_fast32_t binId = pLocks[threadId].fetch_add(1, std::memory_order_relaxed);

            if (binId >= SR_SHADER_MAX_FRAG_BINS)
            {
                // Execute a fragment processors on the full bin. Don't do
                // anything if another thread stole the sentinel bin first.
                if (binId == SR_SHADER_MAX_FRAG_BINS)
                {
                    flush_fragments(threadId, SR_SHADER_MAX_FRAG_BINS);
                    binId = 0;
                }
                else
                {
                    while (pLocks[threadId].load(std::memory_order_relaxed) >= SR_SHADER_MAX_FRAG_BINS);
                    binId = pLocks[threadId].fetch_add(1, std::memory_order_relaxed);
                }
            }

            // carry one
            pFragBins[threadId][binId] = bin;
        }
    }
}



/*--------------------------------------
 * Process Vertices
--------------------------------------*/
void SR_VertexProcessor::execute() noexcept
{
    const SR_VertexShader   vertShader   = mShader->mVertShader;
    const SR_UniformBuffer* pUniforms    = mShader->mUniforms.get();
    const size_t            numVaryings  = vertShader.numVaryings;
    const SR_VertexArray&   vao          = mContext->vao(mMesh.vaoId);
    const SR_VertexBuffer&  vbo          = mContext->vbo(vao.get_vertex_buffer());
    uint16_t                fboW         = mFboW;
    uint16_t                fboH         = mFboH;
    const float             widthScale   = (float)fboW * 0.5f;
    const float             heightScale  = (float)fboH * 0.5f;
    uint32_t                begin        = mMesh.elementBegin;
    uint32_t                end          = mMesh.elementEnd;
    const SR_IndexBuffer*   pIbo         = nullptr;
    ls::math::vec4          worldCoords  [SR_SHADER_MAX_WORLD_COORDS];
    ls::math::vec4          screenCoords [SR_SHADER_MAX_SCREEN_COORDS];
    math::vec4              pVaryings    [SR_SHADER_MAX_VARYING_VECTORS * SR_SHADER_MAX_SCREEN_COORDS];

    if (vao.has_index_buffer())
    {
        pIbo = &mContext->ibo(vao.get_index_buffer());
    }

    if (mMesh.mode == RENDER_MODE_INDEXED_POINTS)
    {
        begin += mTileId;
        const uint32_t step = mNumTiles;
        for (uint32_t i = begin; i < end; i += step)
        {
            const uint32_t vertId = get_next_vertex(pIbo, i);
            worldCoords[0] = vertShader.shader(vertId, vao, vbo, pUniforms, pVaryings);

            if (worldCoords[0][3] > 0.f)
            {
                screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
                push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_POINTS)
    {
        begin += mTileId;
        const uint32_t step = mNumTiles;

        for (uint32_t i = begin; i < end; i += step)
        {
            worldCoords[0] = vertShader.shader(i, vao, vbo, pUniforms, pVaryings);

            if (worldCoords[0][3] > 0.f)
            {
                screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
                push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_INDEXED_LINES)
    {
        // 3 vertices per set of lines
        begin += mTileId * 3u;
        const uint32_t step = mNumTiles * 3u;

        for (uint32_t i = begin; i < end; i += step)
        {
            for (uint32_t j = 0; j < 3; ++j)
            {
                const uint32_t index0  = i + j;
                const uint32_t index1  = i + ((j + 1) % 3);
                const uint32_t vertId0 = get_next_vertex(pIbo, index0);
                const uint32_t vertId1 = get_next_vertex(pIbo, index1);
                worldCoords[0]         = vertShader.shader(vertId0, vao, vbo, pUniforms, pVaryings);
                worldCoords[1]         = vertShader.shader(vertId1, vao, vbo, pUniforms, pVaryings + numVaryings);

                if (worldCoords[0][3] > 0.f && worldCoords[1][3] > 0.f)
                {
                    screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
                    screenCoords[1] = world_to_screen_3(worldCoords[1], widthScale, heightScale);
                    push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
                }
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_LINES)
    {
        // 3 vertices per set of lines
        begin += mTileId * 3u;
        const uint32_t step = mNumTiles * 3u;
        for (uint32_t i = begin; i < end; i += step)
        {
            for (uint32_t j = 0; j < 3; ++j)
            {
                const uint32_t vertId0 = i + j;
                const uint32_t vertId1 = i + ((j + 1) % 3);
                worldCoords[0]         = vertShader.shader(vertId0, vao, vbo, pUniforms, pVaryings);
                worldCoords[1]         = vertShader.shader(vertId1, vao, vbo, pUniforms, pVaryings + numVaryings);

                if (worldCoords[0][3] > 0.f && worldCoords[1][3] > 0.f)
                {
                    screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
                    screenCoords[1] = world_to_screen_3(worldCoords[1], widthScale, heightScale);
                    push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
                }
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_INDEXED_TRIANGLES)
    {
        // 3 vertices per triangle
        begin += mTileId * 3u;
        const uint32_t step = mNumTiles * 3u;
        for (uint32_t i = begin; i < end; i += step)
        {
            const math::vec4_t<uint32_t>&& vertId = get_next_vertex3(pIbo, i);

            worldCoords[0]  = vertShader.shader(vertId[0], vao, vbo, pUniforms, pVaryings);
            worldCoords[1]  = vertShader.shader(vertId[1], vao, vbo, pUniforms, pVaryings + numVaryings);
            worldCoords[2]  = vertShader.shader(vertId[2], vao, vbo, pUniforms, pVaryings + (numVaryings * 2));
            screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
            screenCoords[1] = world_to_screen_3(worldCoords[1], widthScale, heightScale);
            screenCoords[2] = world_to_screen_3(worldCoords[2], widthScale, heightScale);

            if (cull_triangle(screenCoords, worldCoords))
            {
                push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
            }
        }
    }
    else if (mMesh.mode == RENDER_MODE_TRIANGLES)
    {
        // 3 vertices per triangle
        begin += mTileId * 3u;
        const uint32_t step = mNumTiles * 3u;
        for (uint32_t i = begin; i < end; i += step)
        {
            const uint32_t vertId0 = i;
            const uint32_t vertId1 = i+1;
            const uint32_t vertId2 = i+2;

            worldCoords[0]  = vertShader.shader(vertId0, vao, vbo, pUniforms, pVaryings);
            worldCoords[1]  = vertShader.shader(vertId1, vao, vbo, pUniforms, pVaryings + numVaryings);
            worldCoords[2]  = vertShader.shader(vertId2, vao, vbo, pUniforms, pVaryings + (numVaryings * 2));
            screenCoords[0] = world_to_screen_3(worldCoords[0], widthScale, heightScale);
            screenCoords[1] = world_to_screen_3(worldCoords[1], widthScale, heightScale);
            screenCoords[2] = world_to_screen_3(worldCoords[2], widthScale, heightScale);

            if (cull_triangle(screenCoords, worldCoords))
            {
                push_fragments(fboW, fboH, screenCoords, worldCoords, pVaryings);
            }
        }
    }

    // Wait for the other fragment processors
    const uint_fast32_t tileId = mBusyProcessors->fetch_sub(1, std::memory_order_relaxed) - 1;

    while (mBusyProcessors->load(std::memory_order_relaxed));

    flush_fragments(tileId, mBinsUsed[tileId].load(std::memory_order_relaxed));
}
