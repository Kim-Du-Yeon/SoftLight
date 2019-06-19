
#ifndef SR_VERTEXBUFFER_HPP
#define SR_VERTEXBUFFER_HPP

#include <cstddef> // ptrdiff_t
#include <memory>

#include "lightsky/utils/Copy.h"
#include "lightsky/utils/Pointer.h"

#include "soft_render/SR_Geometry.hpp"



class SR_VertexBuffer
{
  private:
      size_t mNumBytes;

    ls::utils::Pointer<unsigned char[], ls::utils::AlignedDeleter> mBuffer;

  public:
    ~SR_VertexBuffer() noexcept;

    SR_VertexBuffer() noexcept;

    SR_VertexBuffer(const SR_VertexBuffer& v) noexcept;

    SR_VertexBuffer(SR_VertexBuffer&& v) noexcept;

    SR_VertexBuffer& operator=(const SR_VertexBuffer& v) noexcept;

    SR_VertexBuffer& operator=(SR_VertexBuffer&& v) noexcept;

    int init(size_t numBytes, const void* pData = nullptr) noexcept;

    void terminate() noexcept;

    std::size_t num_bytes() const noexcept;

    template <typename data_type = unsigned char>
    data_type* element(const ptrdiff_t offset) noexcept;

    template <typename data_type = unsigned char>
    const data_type* element(const ptrdiff_t offset) const noexcept;

    void* data() noexcept;

    const void* data() const noexcept;

    void assign(const void* pInputData, ptrdiff_t offset, std::size_t numBytes) noexcept;

    bool valid() const noexcept;
};



/*--------------------------------------
 * Get the total number of bytes used by *this.
--------------------------------------*/
inline std::size_t SR_VertexBuffer::num_bytes() const noexcept
{
    return mNumBytes;
}



/*--------------------------------------
 * Retrieve a single element at an offset.
 *
 * The offset could be retrieved from a VAO
--------------------------------------*/
template <typename data_type>
inline data_type* SR_VertexBuffer::element(const ptrdiff_t offset) noexcept
{
    return reinterpret_cast<data_type*>(mBuffer.get() + offset);
}



/*--------------------------------------
 * Retrieve a single element at an offset (const).
 *
 * The offset could be retrieved from a VAO
--------------------------------------*/
template <typename data_type>
inline const data_type* SR_VertexBuffer::element(const ptrdiff_t offset) const noexcept
{
    return reinterpret_cast<const data_type*>(mBuffer.get() + offset);
}



/*--------------------------------------
 * Retrieve the daw data in *this
--------------------------------------*/
inline void* SR_VertexBuffer::data() noexcept
{
    return mBuffer.get();
}



/*--------------------------------------
 * Retrieve the daw data in *this (const)
--------------------------------------*/
inline const void* SR_VertexBuffer::data() const noexcept
{
    return mBuffer.get();
}



/*--------------------------------------
 * Assign pre-defined elements to *this
--------------------------------------*/
inline void SR_VertexBuffer::assign(const void* pInputData, ptrdiff_t offset, std::size_t numBytes) noexcept
{
    ls::utils::fast_memcpy(mBuffer.get()+offset, pInputData, numBytes);
}



/*--------------------------------------
 * Check if the data in *this has been initialized
--------------------------------------*/
inline bool SR_VertexBuffer::valid() const noexcept
{
    return mBuffer != nullptr;
}



#endif /* SR_VERTEXBUFFER_HPP */
