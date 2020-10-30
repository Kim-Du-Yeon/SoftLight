
#include <iostream>
#include <vector>

#include "softlight/SL_Octree.hpp"


int main()
{
    typedef SL_Octree<int, 16> OctreeType;
    OctreeType octree{ls::math::vec3{0.f, 0.f, 0.f}, 512.f};

    // insert the world node
    octree.insert(ls::math::vec3{0.f, 0.f, 0.f}, 512.f, 0);

    octree.insert(ls::math::vec3{-25.f,   3.f,   -10.f},  3.f, 1);
    octree.insert(ls::math::vec3{ 25.f,   3.f,    18.f},  2.f, 2);
    octree.insert(ls::math::vec3{-6.f,   -64.f,  -181.f}, 3.f, 3);
    octree.insert(ls::math::vec3{ 9.f,    426.f, -10.f},  5.f, 4);
    octree.insert(ls::math::vec3{-100.f, -129.f,  10.f},  3.f, 5);
    octree.insert(ls::math::vec3{-6.f,   -37.f,  -10.f},  1.f, 6);
    octree.insert(ls::math::vec3{-52.f,   3.f,    10.f},  3.f, 7);
    octree.insert(ls::math::vec3{-25.f,   4.f,   -9.f},  1.f, 8);

    std::cout
        << "\nTree breadth: " << octree.breadth()
        << "\nTree depth: " << octree.depth()
        << '\n'
        << std::endl;

    const ls::math::vec3 subTreePos{-4.f, -36.f, -12.f};
    SL_Octree<int, 16>* pSubtree = octree.find(subTreePos);
    std::cout
        << "Found sub-tree:"
        << "\n\tLocation: " << subTreePos[0] << ',' << subTreePos[1] << ',' << subTreePos[2]
        << "\n\tDepth:    " << pSubtree->depth()
        << "\n\tElements: " << pSubtree->size()
        << std::endl;

    for (int data : pSubtree->data())
    {
        std::cout << "\t" << data << std::endl;
    }

    std::cout << "\nIterating: " << std::endl;

    octree.iterate([](const OctreeType* pTree)->bool {
        const ls::math::vec4& pos = pTree->origin();
        bool amPositive = 0x07 != (0x07 & ls::math::sign_mask(pos));

        if (!amPositive)
        {
            return false;
        }

        if (pTree->size())
        {
            std::cout << "\tFound objects at " << pos[0] << ',' << pos[1] << ',' << pos[2] << std::endl;
        }

        for (int data : pTree->data())
        {
            std::cout << "\t\tObject: " << data << std::endl;
        }

        return true;
    });

    return 0;
}
