
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "lightsky/utils/Log.h"

#include "soft_render/SR_Geometry.hpp"
#include "soft_render/SR_ImgFilePPM.hpp"



/*------------------------------------------------------------------------------
 * Save Images
------------------------------------------------------------------------------*/
int sr_img_save_ppm(const coord_shrt_t w, const coord_shrt_t h, const SR_ColorRGB8* const colors, const char* const pFilename)
{
    if (w <= 0)
    {
        return -1;
    }

    if (h <= 0)
    {
        return -2;
    }

    std::ofstream f(pFilename, std::ofstream::out | std::ofstream::binary);
    if (!f.good())
    {
        return -3;
    }

    // Print the header
    // PPM images support color components with values up to 65535. We're
    // simple, so we only use 255 colors per pixel component.
    f << "P6\n" << w << ' ' << h << '\n' << 255 << '\n';

    const size_t numPixels = (size_t)w * (size_t)h;

    // iterate through the image height, then the width
    for (coord_shrt_t i = 0; i < h; ++i)
    {
        for(coord_shrt_t j = 0; j < w; ++j)
        {
            const SR_ColorRGB8 c = colors[numPixels - (w * i + j)];
            const SR_ColorRGB8 o = {c[2], c[1], c[0]};
            f.write(reinterpret_cast<const char*>(o.v), sizeof(SR_ColorRGB8));
        }
    }

    f.close();

    return 0;
}



/*------------------------------------------------------------------------------
 * Load Images
------------------------------------------------------------------------------*/
SR_ColorRGB8* sr_img_load_ppm(coord_shrt_t& w, coord_shrt_t& h, const char* const pFilename)
{
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t pixelMaxVal = 0;
    std::string readBuffer;
    std::string ppmType;
    std::ifstream f(pFilename, std::ofstream::in | std::ofstream::binary);

    // Read a single element from the PPM file
    const auto readNextItem = [&]()->std::string&
    {
        do {
            if (!f.good())
            {
                LS_LOG_ERR("Failed to read ", pFilename, ". End of file reached.");
                readBuffer.clear();
                return readBuffer;
            }

            f >> readBuffer;

            // Check for and discard any comments.
            std::string::size_type comment = readBuffer.find('#');

            if (comment != std::string::npos)
            {
                // read the rest of the line and discard all comments
                std::string discard{};
                std::getline(f, discard);
                readBuffer.resize(comment);
            }
        } while (!readBuffer.size());

        return readBuffer;
    };

    ppmType = std::move(readNextItem());
    if (ppmType.empty() || (ppmType != "P6" && ppmType != "P3"))
    {
        LS_LOG_ERR("Unknown PPM format in ", pFilename, ": ", ppmType);
        return nullptr;
    }

    // Exceptions can be removed if performance is critical
    try
    {
        width = std::stoull(readNextItem());
        height = std::stoull(readNextItem());
        pixelMaxVal = std::stoull(readNextItem());
    }
    catch (std::invalid_argument& ia)
    {
        LS_LOG_ERR("Unable to read value from PPM file: ", ia.what());
        return nullptr;
    }   
    catch (std::out_of_range& oor)
    {
        LS_LOG_ERR("Unable to read number from PPM file: ", oor.what());
        return nullptr;
    }

    if (width < 1 || width > (uint64_t)std::numeric_limits<coord_shrt_t>::max())
    {
        LS_LOG_ERR("Invalid PPM image width: ", width);
        return nullptr;
    }

    if (height < 1 || height > (uint64_t)std::numeric_limits<coord_shrt_t>::max())
    {
        LS_LOG_ERR("Invalid PPM image height: ", height);
        return nullptr;
    }

    // PPM images support up to 65536 values per pixel component
    if (pixelMaxVal > 65535)
    {
        LS_LOG_ERR("Unsupported maximum color value: ", pixelMaxVal);
        return nullptr;
    }

    // discard remaining whitespace
    int discard = isspace(f.peek());
    do
    {
        if (!f.good())
        {
            LS_LOG_ERR("Bag stream encountered while streaming buffer data from ", pFilename);
            return nullptr;
        }

        f.get();
    } while (isspace(discard));

    SR_ColorRGB8* const pImg = new SR_ColorRGB8[width * height];

    // iterate through the image height, then the width
    for (uint64_t i = 0; i < height; ++i)
    {
        for(uint64_t j = 0; j < width; ++j)
        {
            SR_ColorRGB8* p = pImg + (width * i + j);

            // PPM Images can be 8-bits or 16-bits per component.
            if (pixelMaxVal < 256)
            {
                f.read(reinterpret_cast<char*>(p->v), sizeof(uint8_t)*SR_ColorRGB8::num_components());
            }
            else
            {
                SR_ColorRGB16 p2;
                f.read(reinterpret_cast<char*>(p2.v), sizeof(uint16_t)*SR_ColorRGB16::num_components());
                *p = color_cast<uint8_t, uint16_t>(p2);
            }
        }
    }

    f.close();

    w = (coord_shrt_t)width;
    h = (coord_shrt_t)height;

    LS_LOG_MSG("Successfully loaded a ", w, 'x', h, " PPM image: ", pFilename);

    return pImg;
}


