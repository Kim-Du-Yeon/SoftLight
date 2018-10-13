
#ifndef SR_SHADER_HPP
#define SR_SHADER_HPP

#include <memory> // std::shared_ptr



/*-----------------------------------------------------------------------------
 * Forward Declarations
-----------------------------------------------------------------------------*/
namespace ls
{
namespace math
{
    template <typename num_type>
    struct vec3_t;

    template <typename num_type>
    union vec4_t;
} // end math namespace
} // end ls namespace


class SR_Context;
class SR_Framebuffer;
struct SR_Mesh;
class SR_Shader;
struct SR_UniformBuffer;
class SR_VertexArray;
class SR_VertexBuffer;

enum SR_RenderMode : uint16_t; // SR_Geometry.hpp



/*-----------------------------------------------------------------------------
 *
-----------------------------------------------------------------------------*/
struct SR_VertexShader
{
    uint8_t numVaryings;

    ls::math::vec4_t<float> (*shader)(
        const uint32_t           vertId,
        const SR_VertexArray&    vao,
        const SR_VertexBuffer&   vbo,
        const SR_UniformBuffer*  uniforms,
        ls::math::vec4_t<float>* varyings
    );
};



/*-----------------------------------------------------------------------------
 *
-----------------------------------------------------------------------------*/
struct SR_FragmentShader
{
    uint8_t numVaryings;

    uint8_t numOutputs;

    bool (*shader)(
        const ls::math::vec4_t<float>& fragCoord,
        const SR_UniformBuffer*        uniforms,
        const ls::math::vec4_t<float>* varyings,
        ls::math::vec4_t<float>*       outputs
    );
};



/*-----------------------------------------------------------------------------
 *
-----------------------------------------------------------------------------*/
class SR_Shader
{
    friend class SR_Context;
    friend struct SR_VertexProcessor;
    friend struct SR_FragmentProcessor;

  private:
    SR_VertexShader mVertShader;

    SR_FragmentShader mFragShader;

    // Shared pointers are only changed in the move and copy operators
    mutable std::shared_ptr<SR_UniformBuffer> mUniforms;


    SR_Shader(
        const SR_VertexShader& vertShader,
        const SR_FragmentShader& fragShader,
        const std::shared_ptr<SR_UniformBuffer>& pUniforms
    ) noexcept;

  public:
    ~SR_Shader() noexcept;

    SR_Shader(const SR_Shader& s) noexcept;

    SR_Shader(SR_Shader&& s) noexcept;

    SR_Shader& operator=(const SR_Shader& s) noexcept;

    SR_Shader& operator=(SR_Shader&& s) noexcept;

    uint8_t get_num_varyings() const noexcept;

    uint8_t get_num_fragment_outputs() const noexcept;

    const std::shared_ptr<SR_UniformBuffer> uniforms() const noexcept;

    const SR_VertexShader& vertex_shader() const noexcept;

    const SR_FragmentShader& fragment_shader() const noexcept;
};



/*--------------------------------------
 *
--------------------------------------*/
inline uint8_t SR_Shader::get_num_varyings() const noexcept
{
    return mVertShader.numVaryings;
}



/*--------------------------------------
 *
--------------------------------------*/
inline uint8_t SR_Shader::get_num_fragment_outputs() const noexcept
{
    return mFragShader.numOutputs;
}



/*--------------------------------------
 *
--------------------------------------*/
inline const std::shared_ptr<SR_UniformBuffer> SR_Shader::uniforms() const noexcept
{
    return mUniforms;
}



/*--------------------------------------
 *
--------------------------------------*/
inline const SR_VertexShader& SR_Shader::vertex_shader() const noexcept
{
    return mVertShader;
}



/*--------------------------------------
 *
--------------------------------------*/
inline const SR_FragmentShader& SR_Shader::fragment_shader() const noexcept
{
    return mFragShader;
}



#endif /* SR_SHADER_HPP */
