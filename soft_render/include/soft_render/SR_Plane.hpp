
/******************************************************************************
 * @brief Simple plane utility functions.
******************************************************************************/
#ifndef SR_PLANE_HPP
#define SR_PLANE_HPP

#include "lightsky/math/fixed.h"
#include "lightsky/math/vec_utils.h"



/**
 * @brief Simple Plane wrapper.
 *
 * Planes can be described using four scalar coefficients. In this
 * case, we're using a 4D vector to describe a plane, where the coefficients,
 * "a, b, c, d," are referenced by the vector's indices, "0, 1, 2, 3,"
 * respectively.
 */
template <typename data_t>
using SR_PlaneType = ls::math::vec4_t<data_t>;

typedef SR_PlaneType<int32_t>          SR_Planei;
typedef SR_PlaneType<ls::math::medp_t> SR_Planex;
typedef SR_PlaneType<float>            SR_Planef;
typedef SR_PlaneType<double>           SR_Planed;
typedef SR_PlaneType<float>            SR_Plane;



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_coefficients(data_t a, data_t b, data_t c, data_t d) noexcept
{
    return SR_PlaneType<data_t>{a, b, c, d};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_normal(const ls::math::vec3_t<data_t>& normal, data_t d) noexcept
{
    return SR_PlaneType<data_t>{ls::math::vec4_cast(normal, d)};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_normal(const ls::math::vec4_t<data_t>& normal, data_t d) noexcept
{
    return SR_PlaneType<data_t>{ls::math::vec4_cast(ls::math::vec3_cast(normal), d)};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_normal_and_coefficient(const ls::math::vec4_t<data_t>& normalizedPt) noexcept
{
    return SR_PlaneType<data_t>{normalizedPt};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_point_and_normal(const ls::math::vec3_t<data_t>& p, const ls::math::vec3_t<data_t>& normal) noexcept
{
    return SR_PlaneType<data_t>{ls::math::vec4_cast(normal, -ls::math::dot(p, normal))};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_point_and_normal(const ls::math::vec4_t<data_t>& p, const ls::math::vec4_t<data_t>& normal) noexcept
{
    return SR_PlaneType<data_t>{ls::math::vec4_cast(ls::math::vec3_cast(normal), -ls::math::dot(p, normal))};
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_points(const ls::math::vec3_t<data_t>& p0, const ls::math::vec3_t<data_t>& p1, const ls::math::vec3_t<data_t>& p2) noexcept
{
    const ls::math::vec3&& normal = ls::math::normalize(ls::math::cross(p1-p0, p2-p0));
    return sr_plane_from_point_and_normal<data_t>(p0, normal);
}



template <typename data_t>
inline SR_PlaneType<data_t> sr_plane_from_points(const ls::math::vec4_t<data_t>& p0, const ls::math::vec4_t<data_t>& p1, const ls::math::vec4_t<data_t>& p2) noexcept
{
    const ls::math::vec4&& normal = ls::math::normalize(ls::math::cross(p1-p0, p2-p0));
    return sr_plane_from_point_and_normal<data_t>(p0, normal);
}



template <typename data_t>
inline bool sr_plane_intersect_line(const SR_PlaneType<data_t>& p, const ls::math::vec3_t<data_t>& l0, const ls::math::vec3_t<data_t>& l1, ls::math::vec3_t<data_t>& outIntersection) noexcept
{
    const data_t denom = ls::math::dot(p, l1-l0);
    if (denom == data_t{0})
    {
        outIntersection = l0;
        return false;
    }

    const data_t u = ls::math::dot(p, l0) / denom;
    outIntersection = l0 + u * (l1-l0);

    return true;
}



template <typename data_t>
inline bool sr_plane_intersect_line(const SR_PlaneType<data_t>& p, const ls::math::vec4_t<data_t>& l0, const ls::math::vec4_t<data_t>& l1, ls::math::vec4_t<data_t>& outIntersection) noexcept
{
    const ls::math::vec3_t<data_t>&& p3  = ls::math::vec3_cast(p);
    const ls::math::vec3_t<data_t>&& l30 = ls::math::vec3_cast(l0);
    const ls::math::vec3_t<data_t>&& l31 = ls::math::vec3_cast(l1);
    const data_t denom = ls::math::dot(p3, l31-l30);
    if (denom == data_t{0})
    {
        outIntersection = l0;
        return false;
    }

    const data_t u = ls::math::dot(p3, ls::math::vec3_cast(l0)) / denom;
    outIntersection = ls::math::vec4_cast(l30+u*(l31-l30), data_t{1});

    return true;
}



template <typename data_t>
inline ls::math::vec3_t<data_t> sr_plane_closest_point(const SR_PlaneType<data_t>& p, const ls::math::vec3_t<data_t>& v) noexcept
{
    const ls::math::vec3_t<data_t>&& p3 = ls::math::vec3_cast(p);
    return v - p3 * (ls::math::dot(p3, p3)+p[3]);
}



template <typename data_t>
inline ls::math::vec4_t<data_t> sr_plane_closest_point(const SR_PlaneType<data_t>& p, const ls::math::vec4_t<data_t>& v) noexcept
{
    ls::math::vec4_t<data_t>&& p4 = p;
    p4[3] = data_t{0};

    return v - p4 * (ls::math::dot(p4, p4)+p[3]);
}



template <typename data_t>
inline data_t sr_plane_dot_point(const SR_PlaneType<data_t>& p, const ls::math::vec3_t<data_t>& v) noexcept
{
    return ls::math::dot(ls::math::vec3_cast(p), v);
}



template <typename data_t>
inline data_t sr_plane_dot_point(const SR_PlaneType<data_t>& p, const ls::math::vec4_t<data_t>& v) noexcept
{
    return sr_plane_dot_point<data_t>(p, ls::math::vec3_cast(v));
}



template <typename data_t>
inline data_t sr_plane_dot_vec(const SR_PlaneType<data_t>& p, const ls::math::vec3_t<data_t>& v) noexcept
{
    return ls::math::dot(ls::math::vec3_cast(p), v) + p[3];
}



template <typename data_t>
inline data_t sr_plane_dot_vec(const SR_PlaneType<data_t>& p, const ls::math::vec4_t<data_t>& v) noexcept
{
    return sr_plane_dot_vec<data_t>(p, ls::math::vec3_cast(v));
}





#endif /* SR_PLANE_HPP */