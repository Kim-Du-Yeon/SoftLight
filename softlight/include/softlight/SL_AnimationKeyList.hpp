
#ifndef SL_ANIMATION_KEY_LIST_HPP
#define SL_ANIMATION_KEY_LIST_HPP

#include <utility> // std::move

#include "lightsky/setup/Macros.h" // LS_DECLARE_CLASS_TYPE

#include "lightsky/math/scalar_utils.h" // floor()

#include "lightsky/utils/Assertions.h"
#include "lightsky/utils/Copy.h" // fast_memset()
#include "lightsky/utils/Pointer.h" // aligned_alloc, AlignedDeleter

#include "softlight/SL_AnimationProperty.hpp"



/*-----------------------------------------------------------------------------
 * SL_Animation Modes
-----------------------------------------------------------------------------*/
enum SL_AnimationFlag : unsigned
{
    SL_ANIM_FLAG_NONE = 0x00, // no interpolation, should be performed.
    SL_ANIM_FLAG_IMMEDIATE = 0x01, // immediately jump from frame to frame.
    SL_ANIM_FLAG_INTERPOLATE = 0x02, // linearly interpolate between the current and next frame.
    SL_ANIM_FLAG_REPEAT = 0x04, // repeat an SL_Animation.

    SL_ANIM_FLAG_DEFAULT = SL_ANIM_FLAG_INTERPOLATE
};


/*-----------------------------------------------------------------------------
 * SL_Animation Key Frame Helper Class (for interpolating animations).
 *
 * FIXME: Animations do not play if only two keyframes are present. At least
 * 3 frames are necessary for an SL_Animation to play.
-----------------------------------------------------------------------------*/
template<typename data_t>
class SL_AnimationKeyList
{
  private:
    /**
     * @brief numPositions contains the total number of position keys.
     */
    size_t mNumFrames;

    /**
     * @brief positionTimes contains the keyframe times of a particular
     * animation's positions.
     */
    ls::utils::Pointer<SL_AnimPrecision[], ls::utils::AlignedDeleter> mKeyTimes;

    /**
     * @brief keyData contains a list of variables which can be
     * interpolated during an animation.
     */
    ls::utils::Pointer<data_t[], ls::utils::AlignedDeleter> mKeyData;

  public:
    /*
     * Destructor
     *
     * Frees all dynamic memory usage from *this.
     */
    ~SL_AnimationKeyList() noexcept;

    /**
     * Constructor
     *
     * Initializes all internal members to their default values. No dynamic
     * memory is allocated at this time.
     */
    SL_AnimationKeyList() noexcept;

    /**
     * Copy Constructor
     *
     * Copies all data from the input parameter into *this.
     *
     * @param k
     * A constant reference to another AnimationKeyList type which contains
     * keyframe data.
     */
    SL_AnimationKeyList(const SL_AnimationKeyList& k) noexcept;

    /**
     * Move Constructor
     *
     * Moves all data from the input parameter into *this. No dynamic
     * allocations are performed.
     *
     * @param k
     * An r-value reference to another AnimationKeyList type which contains
     * keyframe data.
     */
    SL_AnimationKeyList(SL_AnimationKeyList&& k) noexcept;

    /**
     * Copy Operator
     *
     * Copies all data from the input parameter into *this.
     *
     * @param k
     * A constant reference to another AnimationKeyList type which contains
     * keyframe data.
     *
     * @return A reference to *this.
     */
    SL_AnimationKeyList& operator=(const SL_AnimationKeyList& k) noexcept;

    /**
     * Move Operator
     *
     * Moves all data from the input parameter into *this. No dynamic
     * allocations are performed.
     *
     * @param k
     * An r-value reference to another AnimationKeyList type which contains
     * keyframe data.
     *
     * @return A reference to *this.
     */
    SL_AnimationKeyList& operator=(SL_AnimationKeyList&& k) noexcept;

    /**
     * Free all dynamic memory from *this and return the internal
     * members to their default values.
     */
    void clear() noexcept;

    /**
     * Retrieve the number of keyframes in *this.
     *
     * @return A size_t type, containing the current number of key frames
     * contained in *this.
     */
    size_t size() const noexcept;

    /**
     * Initialize and allocate an array of keyframes for *this to use.
     *
     * This method will clear any old keyframes which previously existed in
     * *this.
     *
     * This method is not reentrant.
     *
     * @param keyCount
     * The desired number of keyframes to allocate.
     *
     * @return TRUE if the internal array of keyframes was successfully
     * allocated, FALSE if not.
     */
    bool init(const size_t keyCount) noexcept;

    /**
     * Determine if there are keyframes in *this to use for animation.
     *
     * @return TRUE if *this object contains at least one keyframe to use
     * of FALSE if not.
     */
    bool valid() const noexcept;

    /**
     * Retrieve the time difference between the initial keyframe and final
     * keyframe in *this.
     *
     * @note All animations in the library use percentage values between
     * 0.0 and 1.0, inclusive, for time representation unless documented
     * otherwise.
     *
     * @return A floating-point value containing the time difference
     * between the starting and ending keyframes.
     */
    SL_AnimPrecision duration() const noexcept;

    /**
     * Retrieve the time of the starting keyframe in *this.
     *
     * @return A floating-point value within the range (0.0, 1.0) which
     * determines when a particular keyframe should be used to start an
     * animation.
     */
    SL_AnimPrecision start_time() const noexcept;

    /**
     * Set the time of the starting keyframe in *this.
     *
     * @param startOffset
     * A floating-point value within the range (0.0, 1.0) which determines
     * when a particular keyframe should be used to start an animation.
     */
    void start_time(const SL_AnimPrecision startOffset) noexcept;

    /**
     * Retrieve the time of the final keyframe in *this.
     *
     * @return A floating-point value within the range (0.0, 1.0) which
     * determines when a particular keyframe should be used to end an
     * animation.
     */
    SL_AnimPrecision end_time() const noexcept;

    /**
     * Retrieve the time of a single keyframe from *this.
     *
     * @return A floating-point value within the range (0.0, 1.0) which
     * determines when a particular keyframe should be used in an
     * animation.
     */
    SL_AnimPrecision frame_time(const size_t keyIndex) const noexcept;

    /**
     * Retrieve the data of a particular keyframe.
     *
     * This method will raise an assertion if the index is out of range of
     * any available keys.
     *
     * @param keyIndex
     * An array index to the desired keyframe.
     *
     * @return A constant reference to the data within a keyframe.
     */
    const data_t& frame_data(const size_t keyIndex) const noexcept;

    /**
     * Retrieve the data of a particular keyframe.
     *
     * This method will raise an assertion if the index is out of range of
     * any available keys.
     *
     * @param keyIndex
     * An array index to the desired keyframe.
     *
     * @return A reference to the data within a keyframe.
     */
    data_t& frame_data(const size_t keyIndex) noexcept;

    /**
     * Retrieve the data of the first keyframe in *this.
     *
     * This method will raise an assertion if there are no available
     * frames to retrieve data from.
     *
     * @return A reference to the initial keyframe's data.
     */
    const data_t& start_data() const noexcept;

    /**
     * Retrieve the data of the last keyframe in *this.
     *
     * This method will raise an assertion if there are no available
     * frames to retrieve data from.
     *
     * @return A reference to the last keyframe's data.
     */
    const data_t& end_data() const noexcept;

    /**
     * Assign data to a particular frame in *this.
     *
     * This method will raise an assertion if the index is out of range of
     * any available keys.
     *
     * @param keyIndex
     * An array index to the desired keyframe.
     *
     * @param frameTime
     * A time of a single frame, represented as a percentage of an entire
     * animation, in the range (0.0, 1.0).
     *
     * @param frameData
     * A constant reference to the data which will be used for a keyframe
     * at a perticular time.
     */
    void frame(const size_t frameIndex, const SL_AnimPrecision frameTime, const data_t& frameData) noexcept;

    /**
     * Retrieve the interpolation between two keyframes closest to the
     * percentage of an overall animation's length.
     *
     * @param percent
     * A floating-point value, representing the overall time that has
     * elapsed in an animation.
     *
     * @param animFlags
     * A set of flags which determines how the output data will be
     * interpolated.
     *
     * @return The interpolation between two animation frames at a given
     * time of an animation.
     */
    data_t interpolated_data(SL_AnimPrecision percent, const SL_AnimationFlag animFlags) const noexcept;

    /**
     * Calculate the percent of interpolation which is required to mix the
     * data between two animation frames.
     *
     * @param totalAnimPercent
     * The overall percent of time elapsed in an animation.
     *
     * @param outCurrFrame
     * A reference to an integer, which will contain the array index of the
     * current frame in *this which should be used for interpolation.
     *
     * @param outNextFrame
     * A reference to an integer, which will contain the array index of the
     * next frame in *this which should be used for interpolation.
     *
     * @return A percentage, which should be used to determine the amount
     * of interpolation between the frames at 'outCurrFrane' and
     * 'outNextFrame.' This return value may be greater than the standard
     * range (0.0, 1.0), In such a case, it's up to the function caller to
     * determine what to do.
     */
    SL_AnimPrecision calc_frame_interpolation(
        const SL_AnimPrecision totalAnimPercent,
        size_t& outCurrFrame,
        size_t& outNextFrame
    ) const noexcept;
};



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>::~SL_AnimationKeyList() noexcept
{
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>::SL_AnimationKeyList() noexcept :
    mNumFrames{0},
    mKeyTimes{nullptr},
    mKeyData{nullptr}
{}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>::SL_AnimationKeyList(const SL_AnimationKeyList& a) noexcept :
    mNumFrames{0},
    mKeyTimes{nullptr},
    mKeyData{nullptr}
{
    *this = a;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>::SL_AnimationKeyList(SL_AnimationKeyList&& a) noexcept :
    mNumFrames{a.mNumFrames},
    mKeyTimes{std::move(a.mKeyTimes)},
    mKeyData{std::move(a.mKeyData)}
{
    a.mNumFrames = 0;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>& SL_AnimationKeyList<data_t>::operator=(const SL_AnimationKeyList& k) noexcept
{
    if (this == &k)
    {
        return *this;
    }

    if (!k.mNumFrames)
    {
        if (mNumFrames)
        {
            clear();
        }
        return *this;
    }

    if (k.mNumFrames != mNumFrames)
    {
        mKeyTimes.reset((SL_AnimPrecision*)ls::utils::aligned_malloc(sizeof(SL_AnimPrecision) * k.mNumFrames));
        mKeyData.reset((data_t*)ls::utils::aligned_malloc(sizeof(data_t) * k.mNumFrames));
    }

    if (!mKeyTimes || !mKeyData)
    {
        clear();
        return *this;
    }

    mNumFrames = k.mNumFrames;

    for (size_t i = 0; i < mNumFrames; ++i)
    {
        mKeyTimes[i] = k.mKeyTimes[i];
        mKeyData[i] = k.mKeyData[i];
    }

    return *this;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
SL_AnimationKeyList<data_t>& SL_AnimationKeyList<data_t>::operator=(SL_AnimationKeyList&& k) noexcept
{
    if (this == &k)
    {
        return *this;
    }

    mNumFrames = k.mNumFrames;
    k.mNumFrames = 0;

    mKeyTimes = std::move(k.mKeyTimes);
    mKeyData = std::move(k.mKeyData);

    return *this;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
void SL_AnimationKeyList<data_t>::clear() noexcept
{
    mNumFrames = 0;
    mKeyTimes.reset();
    mKeyData.reset();
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline size_t SL_AnimationKeyList<data_t>::size() const noexcept
{
    return mNumFrames;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
bool SL_AnimationKeyList<data_t>::init(const size_t keyCount) noexcept
{
    if (!keyCount)
    {
        if (mNumFrames)
        {
            clear();
        }
        return true;
    }

    if (keyCount != mNumFrames)
    {
        mKeyTimes.reset((SL_AnimPrecision*)ls::utils::aligned_malloc(sizeof(SL_AnimPrecision) * keyCount));
        mKeyData.reset((data_t*)ls::utils::aligned_malloc(sizeof(data_t) * keyCount));
    }

    if (!mKeyTimes || !mKeyData)
    {
        clear();
        return false;
    }

    mNumFrames = keyCount;

    for (size_t i = 0; i < keyCount; ++i)
    {
        mKeyTimes[i] = SL_AnimPrecision{0};
        mKeyData[i] = data_t{0};
    }

    return true;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline bool SL_AnimationKeyList<data_t>::valid() const noexcept
{
    return mNumFrames > 0;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline SL_AnimPrecision SL_AnimationKeyList<data_t>::duration() const noexcept
{
    return end_time() - start_time();
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline SL_AnimPrecision SL_AnimationKeyList<data_t>::start_time() const noexcept
{
    return mNumFrames ? mKeyTimes[0] : SL_AnimPrecision{0};
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
void SL_AnimationKeyList<data_t>::start_time(const SL_AnimPrecision startOffset) noexcept
{
    LS_DEBUG_ASSERT(startOffset >= SL_AnimPrecision{0.0});
    LS_DEBUG_ASSERT(startOffset < SL_AnimPrecision{1.0}); // because somewhere, someone hasn't read the documentation

    const SL_AnimPrecision currentOffset = start_time();
    const SL_AnimPrecision newOffset = currentOffset - startOffset;

    for (size_t i = 0; i < mNumFrames; ++i)
    {
        mKeyTimes[i] = ls::math::clamp<SL_AnimPrecision>(mKeyTimes[i] - newOffset, 0.0, 1.0);
    }
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline SL_AnimPrecision SL_AnimationKeyList<data_t>::end_time() const noexcept
{
    return mNumFrames ? mKeyTimes[mNumFrames - 1] : SL_AnimPrecision{0};
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline SL_AnimPrecision SL_AnimationKeyList<data_t>::frame_time(const size_t keyIndex) const noexcept
{
    LS_DEBUG_ASSERT(keyIndex < mNumFrames);
    return mKeyTimes[keyIndex];
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline const data_t& SL_AnimationKeyList<data_t>::frame_data(const size_t keyIndex) const noexcept
{
    LS_DEBUG_ASSERT(keyIndex < mNumFrames);
    return mKeyData[keyIndex];
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline data_t& SL_AnimationKeyList<data_t>::frame_data(const size_t keyIndex) noexcept
{
    LS_DEBUG_ASSERT(keyIndex < mNumFrames);
    return mKeyData[keyIndex];
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline const data_t& SL_AnimationKeyList<data_t>::start_data() const noexcept
{
    LS_DEBUG_ASSERT(mNumFrames > 0);
    return mKeyData[0];
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline const data_t& SL_AnimationKeyList<data_t>::end_data() const noexcept
{
    LS_DEBUG_ASSERT(mNumFrames > 0);
    return mKeyData[mNumFrames - 1];
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
inline void SL_AnimationKeyList<data_t>::frame(
    const size_t frameIndex,
    const SL_AnimPrecision frameTime,
    const data_t& frameData
) noexcept
{
    LS_DEBUG_ASSERT(mNumFrames > 0);
    mKeyTimes[frameIndex] = frameTime;
    mKeyData[frameIndex] = frameData;
}



/*-------------------------------------
 * Frame difference interpolator
-------------------------------------*/
template<typename data_t>
inline SL_AnimPrecision SL_AnimationKeyList<data_t>::calc_frame_interpolation(
    const SL_AnimPrecision totalAnimPercent,
    size_t& outCurrFrame,
    size_t& outNextFrame
) const noexcept
{
    LS_DEBUG_ASSERT(mNumFrames > 0);

    outCurrFrame = 0;
    outNextFrame = 1;

    // If there's one thing I hate more in hot code paths than branches, it's
    // loops. One day I'll find out how to get rid of this. The more key frames
    // that are in an animation channel, the longer this will take to run.
    while (mKeyTimes[outNextFrame] <= totalAnimPercent && outNextFrame < mNumFrames - 1)
    {
        outCurrFrame++;
        outNextFrame++;
    }

    const SL_AnimPrecision currTime = mKeyTimes[outCurrFrame];
    const SL_AnimPrecision nextTime = mKeyTimes[outNextFrame];
    const SL_AnimPrecision frameDelta = nextTime - currTime;
    const SL_AnimPrecision ret = SL_AnimPrecision{1} - ((nextTime - totalAnimPercent) / frameDelta);

    return ret;
}



/*-------------------------------------
-------------------------------------*/
template<typename data_t>
data_t SL_AnimationKeyList<data_t>::interpolated_data(SL_AnimPrecision, const SL_AnimationFlag) const noexcept
{
    LS_ASSERT(false);
    return data_t{};
}



template<>
ls::math::vec3_t<float> SL_AnimationKeyList<ls::math::vec3_t < float>>::interpolated_data(SL_AnimPrecision percent, const SL_AnimationFlag animFlags) const noexcept;

template<>
ls::math::quat_t<float> SL_AnimationKeyList<ls::math::quat_t < float>>::interpolated_data(SL_AnimPrecision percent, const SL_AnimationFlag animFlags) const noexcept;



/*-----------------------------------------------------------------------------
 * Pre-Compiled Template Specializations
-----------------------------------------------------------------------------*/
LS_DECLARE_CLASS_TYPE(SL_AnimationKeyListVec3, SL_AnimationKeyList, ls::math::vec3_t<float>);
LS_DECLARE_CLASS_TYPE(SL_AnimationKeyListQuat, SL_AnimationKeyList, ls::math::quat_t<float>);


#endif /* SL_ANIMATION_KEY_LIST_HPP */