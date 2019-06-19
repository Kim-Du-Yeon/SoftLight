
#include <utility> // std::move()

#include "soft_render/SR_VertexArray.hpp"



/*--------------------------------------
 * Destructor
--------------------------------------*/
SR_VertexArray::~SR_VertexArray() noexcept
{
    terminate();
}



/*--------------------------------------
 * Constructor
--------------------------------------*/
SR_VertexArray::SR_VertexArray() noexcept :
    mVboId{SR_INVALID_BUFFER_ID},
    mIboId{SR_INVALID_BUFFER_ID},
    mDimens{},
    mTypes{},
    mOffsets{},
    mStrides{}
{}



/*--------------------------------------
 * Copy Constructor
--------------------------------------*/
SR_VertexArray::SR_VertexArray(const SR_VertexArray& v) noexcept :
    mVboId{v.mVboId},
    mIboId{v.mIboId},
    mDimens{v.mDimens},
    mTypes{v.mTypes},
    mOffsets{v.mOffsets},
    mStrides{v.mStrides}
{}



/*--------------------------------------
 * Move Constructor
--------------------------------------*/
SR_VertexArray::SR_VertexArray(SR_VertexArray&& v) noexcept :
    mVboId{v.mVboId},
    mIboId{v.mIboId},
    mDimens{std::move(v.mDimens)},
    mTypes{std::move(v.mTypes)},
    mOffsets{std::move(v.mOffsets)},
    mStrides{std::move(v.mStrides)}
{
    v.mVboId = SR_INVALID_BUFFER_ID;
    v.mIboId = SR_INVALID_BUFFER_ID;
}



/*--------------------------------------
 * Copy Operator
--------------------------------------*/
SR_VertexArray& SR_VertexArray::operator=(const SR_VertexArray& v) noexcept
{
    if (this != &v)
    {
        mVboId = v.mVboId;
        mIboId = v.mIboId;

        mDimens = v.mDimens;
        mTypes = v.mTypes;
        mOffsets = v.mOffsets;
        mStrides = v.mStrides;
    }

    return *this;
}



/*--------------------------------------
 * Move Operator
--------------------------------------*/
SR_VertexArray& SR_VertexArray::operator=(SR_VertexArray&& v) noexcept
{
    if (this != &v)
    {
        mVboId = v.mVboId;
        v.mVboId = SR_INVALID_BUFFER_ID;

        mIboId = v.mIboId;
        v.mIboId = SR_INVALID_BUFFER_ID;

        mDimens = std::move(v.mDimens);
        mTypes = std::move(v.mTypes);
        mOffsets = std::move(v.mOffsets);
        mStrides = std::move(v.mStrides);
    }

    return *this;
}



/*--------------------------------------
 * Set the number of VBO bindings to monitor
--------------------------------------*/
int SR_VertexArray::set_num_bindings(std::size_t numBindings) noexcept
{
    if (numBindings == num_bindings())
    {
        return 0;
    }

    int ret = 0;

    // increased the number of bindings
    if (numBindings > mDimens.size())
    {
        ret = (int)(numBindings - mDimens.size());
    }
    else if (numBindings < mDimens.size())
    {
        // decreased the number of bindings
        ret = (int)(mDimens.size() - numBindings);
        ret = -ret;
    }

    mDimens.resize(numBindings);
    mTypes.resize(numBindings);
    mOffsets.resize(numBindings);
    mStrides.resize(numBindings);

    return ret;
}



/*--------------------------------------
 * Set the metadata of a VBO binding
--------------------------------------*/
void SR_VertexArray::set_binding(
    std::size_t bindId,
    ptrdiff_t offset,
    ptrdiff_t stride,
    SR_Dimension numDimens,
    SR_DataType vertType) noexcept
{
    mDimens[bindId]  = numDimens;
    mTypes[bindId]   = vertType;
    mOffsets[bindId] = offset;
    mStrides[bindId] = stride;
}



/*--------------------------------------
 * Remove a VBO binding
--------------------------------------*/
void SR_VertexArray::remove_binding(std::size_t bindId) noexcept
{
    mDimens.erase(mDimens.begin() + bindId);
    mTypes.erase(mTypes.begin() + bindId);
    mOffsets.erase(mOffsets.begin() + bindId);
    mStrides.erase(mStrides.begin() + bindId);
}



/*--------------------------------------
 * Clear all data assigned to *this.
--------------------------------------*/
void SR_VertexArray::terminate() noexcept
{
    mVboId = SR_INVALID_BUFFER_ID;
    mIboId = SR_INVALID_BUFFER_ID;
    mDimens.clear();
    mTypes.clear();
    mOffsets.clear();
    mStrides.clear();
}
