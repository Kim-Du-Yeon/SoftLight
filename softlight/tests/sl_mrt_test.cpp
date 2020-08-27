
// Full-screen quad example using the "Compact YCoCg Frame Buffer" technique.

#include <thread>

#include "lightsky/math/vec_utils.h"
#include "lightsky/math/mat_utils.h"

#include "lightsky/utils/Log.h"
#include "lightsky/utils/StringUtils.h"
#include "lightsky/utils/Time.hpp"
#include "lightsky/utils/Tuple.h"

#include "softlight/SL_BoundingBox.hpp"
#include "softlight/SL_Color.hpp"
#include "softlight/SL_Context.hpp"
#include "softlight/SL_Framebuffer.hpp"
#include "softlight/SL_ImgFilePPM.hpp"
#include "softlight/SL_IndexBuffer.hpp"
#include "softlight/SL_KeySym.hpp"
#include "softlight/SL_Material.hpp"
#include "softlight/SL_Mesh.hpp"
#include "softlight/SL_RenderWindow.hpp"
#include "softlight/SL_Sampler.hpp"
#include "softlight/SL_SceneFileLoader.hpp"
#include "softlight/SL_Texture.hpp"
#include "softlight/SL_Transform.hpp"
#include "softlight/SL_UniformBuffer.hpp"
#include "softlight/SL_VertexArray.hpp"
#include "softlight/SL_VertexBuffer.hpp"
#include "softlight/SL_WindowBuffer.hpp"
#include "softlight/SL_WindowEvent.hpp"

namespace math = ls::math;
namespace utils = ls::utils;



#ifndef IMAGE_WIDTH
    #define IMAGE_WIDTH 1280
#endif /* IMAGE_WIDTH */

#ifndef IMAGE_HEIGHT
    #define IMAGE_HEIGHT 720
#endif /* IMAGE_HEIGHT */

#ifndef SL_TEST_MAX_THREADS
    #define SL_TEST_MAX_THREADS 1//(ls::math::max<unsigned>(std::thread::hardware_concurrency(), 2u) - 1u)
#endif /* SL_TEST_MAX_THREADS */

#ifndef SL_BENCHMARK_SCENE
    #define SL_BENCHMARK_SCENE 0
#endif /* SL_BENCHMARK_SCENE */



/*-----------------------------------------------------------------------------
 * Shader to display vertices with positions, UVs, normals, and a texture
-----------------------------------------------------------------------------*/
struct MeshTestUniforms
{
    const SL_Texture* pTexture;
    math::vec4        lightPos;
    SL_ColorRGBAf     lightCol;
    math::mat4        modelMatrix;
    math::mat4        mvMatrix;
    math::mat4        mvpMatrix;
};



/*--------------------------------------
 * Vertex Shader
--------------------------------------*/
math::vec4 _mrt_vert_shader(SL_VertexParam& param)
{
    typedef utils::Tuple<math::vec3, math::vec2, math::vec3> Vertex;
    const MeshTestUniforms* pUniforms = param.pUniforms->as<MeshTestUniforms>();
    const Vertex*           v         = param.pVbo->element<const Vertex>(param.pVao->offset(0, param.vertId));
    const math::vec4&&      vert      = math::vec4_cast(v->const_element<0>(), 1.f);
    const math::vec4&&      uv        = math::vec4_cast(v->const_element<1>(), 0.f, 0.f);
    const math::vec4&&      norm      = math::vec4_cast(v->const_element<2>(), 0.f);

    param.pVaryings[0] = pUniforms->modelMatrix * vert;
    param.pVaryings[1] = uv;
    param.pVaryings[2] = pUniforms->mvMatrix * norm;

    return pUniforms->mvpMatrix * vert;
}



SL_VertexShader mrt_vert_shader()
{
    SL_VertexShader shader;
    shader.numVaryings = 3;
    shader.cullMode    = SL_CULL_BACK_FACE;
    shader.shader      = _mrt_vert_shader;

    return shader;
}



/*--------------------------------------
 * Fragment Shader
--------------------------------------*/
bool _mrt_frag_shader(SL_FragmentParam& fragParams)
{
    const MeshTestUniforms* pUniforms = fragParams.pUniforms->as<MeshTestUniforms>();
    const math::vec4&       pos       = fragParams.pVaryings[0];
    const math::vec4&       uv        = fragParams.pVaryings[1];
    const math::vec4&       norm      = math::normalize(fragParams.pVaryings[2]);
    const SL_Texture*       albedo    = pUniforms->pTexture;

    // normalize the texture colors to within (0.f, 1.f)
    math::vec3_t<uint8_t>&& pixel8 = sl_sample_trilinear<SL_ColorRGB8, SL_WrapMode::EDGE>(*albedo, uv[0], uv[1]);

    // Light direction calculation
    math::vec4&& lightDir = math::normalize(pUniforms->lightPos - pos);

    // Diffuse light calculation
    const float lightAngle = math::max(math::dot(lightDir, norm), 0.f);

    // CPU Cache load effects make it faster to read the texture data after
    // accounting for some latency.
    math::vec4_t<uint8_t>&& pixelF = math::vec4_cast<uint8_t>(pixel8, 255);
    math::vec4&&            pixel  = color_cast<float, uint8_t>(pixelF);

    const math::vec4&& composite = pixel + pUniforms->lightCol * lightAngle;
    const math::vec4&& output = math::clamp(composite, math::vec4{0.f}, math::vec4{1.f});

    fragParams.pOutputs[0] = output;
    fragParams.pOutputs[1] = pos;
    fragParams.pOutputs[2] = uv;
    fragParams.pOutputs[3] = norm;

    return true;
}



SL_FragmentShader mrt_frag_shader()
{
    SL_FragmentShader shader;
    shader.numVaryings = 3;
    shader.numOutputs  = 4;
    shader.blend       = SL_BLEND_OFF;
    shader.depthTest   = SL_DEPTH_TEST_ON;
    shader.depthMask   = SL_DEPTH_MASK_ON;
    shader.shader      = _mrt_frag_shader;

    return shader;
}



/*-----------------------------------------------------------------------------
 * Create the context for a demo scene
-----------------------------------------------------------------------------*/
utils::Pointer<SL_SceneGraph> mesh_test_create_context()
{
    int retCode = 0;

    SL_SceneFileLoader meshLoader;
    utils::Pointer<SL_SceneGraph> pGraph{new SL_SceneGraph{}};
    SL_Context& context = pGraph->mContext;

    size_t depthId = context.create_texture();

    size_t texRgbId  = context.create_texture();
    size_t texPosId  = context.create_texture();
    size_t texUvId   = context.create_texture();
    size_t texNormId = context.create_texture();
    size_t fboId     = context.create_framebuffer();

    SL_Texture& texDepth = context.texture(depthId);
    retCode = texDepth.init(SL_ColorDataType::SL_COLOR_R_16U, IMAGE_WIDTH, IMAGE_HEIGHT, 1);
    assert(retCode == 0);

    SL_Texture& texRgb = context.texture(texRgbId);
    retCode = texRgb.init(SL_ColorDataType::SL_COLOR_RGBA_8U, IMAGE_WIDTH, IMAGE_HEIGHT, 1);
    assert(retCode == 0);

    SL_Texture& texPos = context.texture(texPosId);
    retCode = texPos.init(SL_ColorDataType::SL_COLOR_RGBA_8U, IMAGE_WIDTH, IMAGE_HEIGHT, 1);
    assert(retCode == 0);

    SL_Texture& texUv = context.texture(texUvId);
    retCode = texUv.init(SL_ColorDataType::SL_COLOR_RGBA_8U, IMAGE_WIDTH, IMAGE_HEIGHT, 1);
    assert(retCode == 0);

    SL_Texture& texNorm = context.texture(texNormId);
    retCode = texNorm.init(SL_ColorDataType::SL_COLOR_RGBA_8U, IMAGE_WIDTH, IMAGE_HEIGHT, 1);
    assert(retCode == 0);

    SL_Framebuffer& fbo = context.framebuffer(fboId);
    retCode = fbo.reserve_color_buffers(4);
    assert(retCode == 0);

    retCode = fbo.attach_color_buffer(0, texRgb);
    assert(retCode == 0);

    retCode = fbo.attach_color_buffer(1, texPos);
    assert(retCode == 0);

    retCode = fbo.attach_color_buffer(2, texUv);
    assert(retCode == 0);

    retCode = fbo.attach_color_buffer(3, texNorm);
    assert(retCode == 0);

    retCode = fbo.attach_depth_buffer(texDepth);
    assert(retCode == 0);

    const std::array<size_t, 4> attachIds{0, 1, 2, 3};
    const std::array<const SL_ColorRGBAd, 4> colors{
        SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
        SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
        SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
        SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0}
    };
    context.clear_framebuffer(0, attachIds, colors, 0.0);

    retCode = fbo.valid();
    assert(retCode == 0);

    retCode = meshLoader.load("testdata/african_head/african_head.obj");
    assert(retCode != 0);

    retCode = pGraph->import(meshLoader.data());
    assert(retCode == 0);

    // Always make sure the scene graph is updated before rendering
    pGraph->mCurrentTransforms[1].move(math::vec3{0.f, 30.f, 0.f});
    pGraph->mCurrentTransforms[1].scale(math::vec3{5.f});
    pGraph->update();

    const SL_VertexShader&&   vertShader = mrt_vert_shader();
    const SL_FragmentShader&& fragShader = mrt_frag_shader();

    size_t uboId = context.create_ubo();
    SL_UniformBuffer& ubo = context.ubo(uboId);
    MeshTestUniforms* pUniforms = ubo.as<MeshTestUniforms>();

    pUniforms->lightPos = math::vec4{20.f, 100.f, 20.f, 0.f};
    pUniforms->lightCol = math::vec4{0.125f, 0.09f, 0.08f, 1.f};
    pUniforms->modelMatrix = math::mat4{1.f};
    pUniforms->mvMatrix = math::mat4{1.f};
    pUniforms->mvpMatrix = math::mat4{1.f};
    size_t testShaderId = context.create_shader(vertShader, fragShader, uboId);

    assert(testShaderId == 0);
    (void)testShaderId;

    (void)retCode;

    return pGraph;
}



/*-----------------------------------------------------------------------------
 * Render a scene
-----------------------------------------------------------------------------*/
void mesh_test_render(SL_SceneGraph* pGraph, const math::mat4& projectionMat, const math::mat4& viewMat)
{
    const math::mat4&& vpMatrix = projectionMat * viewMat;
    SL_Context&       context   = pGraph->mContext;
    MeshTestUniforms* pUniforms = context.ubo(0).as<MeshTestUniforms>();

    for (size_t i = 1; i < pGraph->mNodes.size(); ++i)
    {
        SL_SceneNode& n = pGraph->mNodes[i];

        // Only mesh nodes should be sent for rendering.
        if (n.type != NODE_TYPE_MESH)
        {
            continue;
        }

        const math::mat4& modelMat = pGraph->mModelMatrices[n.nodeId];
        const size_t numNodeMeshes = pGraph->mNumNodeMeshes[n.dataId];
        const utils::Pointer<size_t[]>& meshIds = pGraph->mNodeMeshes[n.dataId];

        pUniforms->modelMatrix = modelMat;
        pUniforms->mvMatrix = math::mat4{math::mat3{viewMat}} * modelMat;
        pUniforms->mvpMatrix   = vpMatrix * modelMat;

        for (size_t meshId = 0; meshId < numNodeMeshes; ++meshId)
        {
            const size_t          nodeMeshId = meshIds[meshId];
            const SL_Mesh&        m          = pGraph->mMeshes[nodeMeshId];
            const SL_Material&    material   = pGraph->mMaterials[m.materialId];
            pUniforms->pTexture = material.pTextures[SL_MATERIAL_TEXTURE_AMBIENT];

            // NOTE: Always validate your IDs in production
            constexpr size_t shaderId = 0;
            constexpr size_t fboid    = 0;

            context.draw(m, shaderId, fboid);
        }
    }
}



/*-----------------------------------------------------------------------------
 * Render a scene
-----------------------------------------------------------------------------*/
int main()
{
    int retCode = 0;
    (void)retCode;

    utils::Pointer<SL_RenderWindow> pWindow{std::move(SL_RenderWindow::create())};
    utils::Pointer<SL_WindowBuffer> pRenderBuf{SL_WindowBuffer::create()};
    if (pWindow->init(IMAGE_WIDTH, IMAGE_HEIGHT))
    {
        LS_LOG_ERR("Unable to initialize a window.");
        return -1;
    }
    else if (!pWindow->run())
    {
        LS_LOG_ERR("Unable to run the test window!");
        pWindow->destroy();
        return -2;
    }
    else if (pRenderBuf->init(*pWindow, pWindow->width(), pWindow->height()) != 0 || pWindow->set_title("Mesh Test") != 0)
    {
        LS_LOG_ERR("Unable to resize the test window buffer!");
        pWindow->destroy();
        return -3;
    }

    pWindow->set_keys_repeat(false); // text mode
    pWindow->set_mouse_capture(false);

    utils::Pointer<SL_SceneGraph>    pGraph{std::move(mesh_test_create_context())};
    ls::utils::Clock<float>          timer;
    SL_Transform                     viewMatrix;
    SL_WindowEvent                   evt;
    math::mat4         projMatrix     = math::infinite_perspective(LS_DEG2RAD(80.f), (float)pWindow->width()/(float)pWindow->height(), 0.01f);
    SL_Context&        context        = pGraph->mContext;
    int                shouldQuit     = 0;
    int                numFrames      = 0;
    int                totalFrames    = 0;
    float              secondsCounter = 0.f;
    float              tickTime       = 0.f;
    unsigned           activeColor    = 1;

    viewMatrix.type(SL_TransformType::SL_TRANSFORM_TYPE_VIEW_ARC_LOCKED_Y);
    viewMatrix.extract_transforms(math::look_at(math::vec3{10.f, 30.f, 70.f}, math::vec3{0.f, 20.f, 0.f}, math::vec3{0.f, 1.f, 0.f}));
    viewMatrix.apply_transform();

    timer.start();

    context.num_threads(SL_TEST_MAX_THREADS);

    while (!shouldQuit)
    {
        pWindow->update();

        if (pWindow->has_event())
        {
            pWindow->pop_event(&evt);

            if (evt.type == SL_WinEventType::WIN_EVENT_RESIZED)
            {
                std::cout<< "Window resized: " << evt.window.width << 'x' << evt.window.height << std::endl;
                pRenderBuf->terminate();
                pRenderBuf->init(*pWindow, pWindow->width(), pWindow->height());

                context.texture(0).init(context.texture(0).type(), pWindow->width(), pWindow->height());
                context.texture(1).init(context.texture(1).type(), pWindow->width(), pWindow->height());
                context.texture(2).init(context.texture(2).type(), pWindow->width(), pWindow->height());
                context.texture(3).init(context.texture(2).type(), pWindow->width(), pWindow->height());
                context.texture(4).init(context.texture(2).type(), pWindow->width(), pWindow->height());

                projMatrix = math::infinite_perspective(LS_DEG2RAD(60.f), (float)pWindow->width()/(float)pWindow->height(), 0.01f);
            }
            else if (evt.type == SL_WinEventType::WIN_EVENT_KEY_UP)
            {
                const SL_KeySymbol keySym = evt.keyboard.keysym;
                if (keySym == SL_KeySymbol::KEY_SYM_ESCAPE)
                {
                    LS_LOG_MSG("Escape button pressed. Exiting.");
                    shouldQuit = true;
                }
                else if (keySym == SL_KeySymbol::KEY_SYM_LEFT)
                {
                    activeColor = (activeColor-1 >= 1) ? (activeColor-1) : 4;
                }
                else if (keySym == SL_KeySymbol::KEY_SYM_RIGHT)
                {
                    activeColor = (activeColor+1 <= 4) ? (activeColor+1) : 1;
                }
            }
            else if (evt.type == SL_WinEventType::WIN_EVENT_CLOSING)
            {
                LS_LOG_MSG("Window close event caught. Exiting.");
                shouldQuit = true;
            }
        }
        else
        {
            timer.tick();
            tickTime = timer.tick_time().count();
            secondsCounter += tickTime;

            viewMatrix.rotate(math::vec3{-0.5f*tickTime, 0.f, 0.f});
            viewMatrix.apply_transform();

            constexpr std::array<size_t, 4> attachIds{0, 1, 2, 3};
            constexpr std::array<const SL_ColorRGBAd, 4> colors{
                SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
                SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
                SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0},
                SL_ColorRGBAd{0.0, 0.0, 0.0, 1.0}
            };
            context.clear_framebuffer(0, attachIds, colors, 0.0);

            mesh_test_render(pGraph.get(), projMatrix, viewMatrix.transform());

            context.blit(*pRenderBuf, activeColor);
            pWindow->render(*pRenderBuf);

            ++numFrames;
            ++totalFrames;

            if (secondsCounter >= 1.f)
            {
                LS_LOG_MSG("FPS: ", utils::to_str((float)numFrames / secondsCounter));
                numFrames = 0;
                secondsCounter = 0.f;
            }

            #if SL_BENCHMARK_SCENE
                if (totalFrames >= 3600)
                {
                    shouldQuit = true;
                }
            #endif
        }

        // All events handled. Now check on the state of the window.
        if (pWindow->state() == WindowStateInfo::WINDOW_CLOSING)
        {
            LS_LOG_MSG("Window close state encountered. Exiting.");
            shouldQuit = true;
        }
    }

    pRenderBuf->terminate();
    return pWindow->destroy();
}