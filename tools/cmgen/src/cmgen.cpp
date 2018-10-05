/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <math/scalar.h>
#include <math/vec4.h>

#include <image/KtxBundle.h>

#include <imageio/BlockCompression.h>
#include <imageio/ImageDecoder.h>
#include <imageio/ImageEncoder.h>

#include <utils/Path.h>

#include <getopt/getopt.h>

#include "Cubemap.h"
#include "CubemapIBL.h"
#include "CubemapSH.h"
#include "CubemapUtils.h"
#include "Image.h"

using namespace math;
using namespace image;

// -----------------------------------------------------------------------------------------------

enum class ShFile {
    SH_NONE, SH_CROSS, SH_TEXT
};

static const size_t DFG_LUT_DEFAULT_SIZE = 128;
static const size_t IBL_DEFAULT_SIZE = 256;

enum class OutputType {
    FACES, KTX, EQUIRECT, OCTAHEDRON
};

static image::ImageEncoder::Format g_format = image::ImageEncoder::Format::PNG;
static OutputType g_type = OutputType::FACES;
static std::string g_compression;
static bool g_extract_faces = false;
static double g_extract_blur = 0.0;
static utils::Path g_extract_dir;

static size_t g_output_size = 0;

       bool g_quiet = false; // needed outside of this file
static bool g_debug = false;

static size_t g_sh_compute = 0;
static bool g_sh_output = false;
static bool g_sh_shader = false;
static bool g_sh_irradiance = false;
static ShFile g_sh_file = ShFile::SH_NONE;
static utils::Path g_sh_filename;
static std::unique_ptr<math::double3[]> g_coefficients;

static bool g_is_mipmap = false;
static utils::Path g_is_mipmap_dir;
static bool g_prefilter = false;
static utils::Path g_prefilter_dir;
static bool g_dfg = false;
static utils::Path g_dfg_filename;
static bool g_dfg_multiscatter = false;

static bool g_ibl_irradiance = false;
static utils::Path g_ibl_irradiance_dir;

static bool g_deploy = false;
static utils::Path g_deploy_dir;

static size_t g_num_samples = 1024;

static bool g_mirror = false;

// -----------------------------------------------------------------------------------------------

static void generateMipmaps(std::vector<Cubemap>& levels, std::vector<Image>& images);
static void sphericalHarmonics(const utils::Path& iname, const Cubemap& inputCubemap);
static void iblRoughnessPrefilter(const utils::Path& iname, const std::vector<Cubemap>& levels,
        const utils::Path& dir);
static void iblDiffuseIrradiance(const utils::Path& iname, const std::vector<Cubemap>& levels,
        const utils::Path& dir);
static void iblMipmapPrefilter(const utils::Path& iname, const std::vector<Image>& images,
        const std::vector<Cubemap>& levels, const utils::Path& dir);
static void iblLutDfg(const utils::Path& filename, size_t size, bool multiscatter = false);
static void extractCubemapFaces(const utils::Path& iname, const Cubemap& cm, const utils::Path& dir);
static void outputSh(std::ostream& out, const std::unique_ptr<math::double3[]>& sh, size_t numBands);
static void outputSpectrum(std::ostream& out, const std::unique_ptr<math::double3[]>& sh,
        size_t numBands);
static void saveImage(const std::string& path, ImageEncoder::Format format, const Image& image,
        const std::string& compression);
static LinearImage toLinearImage(const Image& image);
static void exportKtxFaces(KtxBundle& container, uint32_t miplevel, const Cubemap& cm);

// -----------------------------------------------------------------------------------------------

void generateUVGrid(Cubemap const& cml, size_t gridFrequency, size_t dim);

static void printUsage(char* name) {
    std::string exec_name(utils::Path(name).getName());
    std::string usage(
            "CMGEN is a command-line tool for generating SH and mipmap levels from an env map.\n"
            "Cubemaps and equirectangular formats are both supported, automatically detected \n"
            "according to the aspect ratio of the source image.\n"
            "\n"
            "Usages:\n"
            "    CMGEN [options] <input-file>\n"
            "    CMGEN [options] <uv[N]>\n"
            "\n"
            "Supported input formats:\n"
            "    PNG, 8 and 16 bits\n"
            "    Radiance (.hdr)\n"
            "    Photoshop (.psd), 16 and 32 bits\n"
            "    OpenEXR (.exr)\n"
            "\n"
            "Options:\n"
            "   --help, -h\n"
            "       Print this message\n\n"
            "   --license\n"
            "       Print copyright and license information\n\n"
            "   --quiet, -q\n"
            "       Quiet mode. Suppress all non-error output\n\n"
            "   --type=[cubemap|equirect|octahedron|ktx], -t [cubemap|equirect|octahedron|ktx]\n"
            "       Specify output type (default: cubemap)\n\n"
            "   --format=[exr|hdr|psd|rgbm|png|dds|ktx], -f [exr|hdr|psd|rgbm|png|dds|ktx]\n"
            "       Specify output file format. ktx implies -type=ktx.\n\n"
            "           For simplicity, KTX files are always encoded with 4-channel RGBM data\n"
            "   --compression=COMPRESSION, -c COMPRESSION\n"
            "       Format specific compression:\n"
            "           KTX:\n"
            "             astc_[fast|thorough]_[ldr|hdr]_WxH, where WxH is a valid block size\n"
            "             s3tc_rgba_dxt5\n"
            "             etc_FORMAT_METRIC_EFFORT\n"
            "               FORMAT is rgb8_alpha, srgb8_alpha, rgba8, or srgb8_alpha8\n"
            "               METRIC is rgba, rgbx, rec709, numeric, or normalxyz\n"
            "               EFFORT is an integer between 0 and 100\n"
            "           PNG: Ignored\n"
            "           PNG RGBM: Ignored\n"
            "           Radiance: Ignored\n"
            "           Photoshop: 16 (default), 32\n"
            "           OpenEXR: RAW, RLE, ZIPS, ZIP, PIZ (default)\n"
            "           DDS: 8, 16 (default), 32\n\n"
            "   --size=power-of-two, -s power-of-two\n"
            "       Size of the output cubemaps (base level), 256 by default\n\n"
            "   --deploy=dir, -x dir\n"
            "       Generate everything needed for deployment into <dir>\n\n"
            "   --extract=dir\n"
            "       Extract faces of the cubemap into <dir>\n\n"
            "   --extract-blur=roughness\n"
            "       Blurs the cubemap before saving the faces using the roughness blur\n\n"
            "   --no-mirror\n"
            "       Skip mirroring of generated cubemaps (for assets with mirroring already backed in)\n\n"
            "   --ibl-samples=numSamples\n"
            "       Number of samples to use for IBL integrations (default 1024)\n\n"
            "\n"
            "Private use only:\n"
            "   --ibl-dfg=filename.[exr|hdr|psd|png|rgbm|dds|h|hpp|c|cpp|inc|txt]\n"
            "       Compute the IBL DFG LUT\n\n"
            "   --ibl-dfg-multiscatter\n"
            "       If --ibl-dfg is set, computes the DFG for multi-scattering GGX\n\n"
            "   --ibl-is-mipmap=dir\n"
            "       Generate mipmap for pre-filtered importance sampling\n\n"
            "   --ibl-ld=dir\n"
            "       Roughness prefilter into <dir>\n\n"
            "   --ibl-irradiance=dir\n"
            "       Diffuse irradiance into <dir>\n\n"
            "   --sh=bands\n"
            "       SH decomposition of input cubemap\n\n"
            "   --sh-output=filename.[exr|hdr|psd|rgbm|png|dds|txt]\n"
            "       SH output format. The filename extension determines the output format\n\n"
            "   --sh-irradiance, -i\n"
            "       Irradiance SH coefficients\n\n"
            "   --sh-shader\n"
            "       Generate irradiance SH for shader code\n\n"
            "   --debug, -d\n"
            "       Generate extra data for debugging\n\n"
    );
    const std::string from("CMGEN");
    for (size_t pos = usage.find(from); pos != std::string::npos; pos = usage.find(from, pos)) {
         usage.replace(pos, from.length(), exec_name);
    }
    printf("%s", usage.c_str());
}

static void license() {
    std::cout <<
    #include "licenses/licenses.inc"
    ;
}

static int handleCommandLineArgments(int argc, char* argv[]) {
    static constexpr const char* OPTSTR = "hqidt:f:c:s:x:";
    static const struct option OPTIONS[] = {
            { "help",                       no_argument, nullptr, 'h' },
            { "license",                    no_argument, nullptr, 'l' },
            { "quiet",                      no_argument, nullptr, 'q' },
            { "type",                 required_argument, nullptr, 't' },
            { "format",               required_argument, nullptr, 'f' },
            { "compression",          required_argument, nullptr, 'c' },
            { "size",                 required_argument, nullptr, 's' },
            { "extract",              required_argument, nullptr, 'e' },
            { "extract-blur",         required_argument, nullptr, 'r' },
            { "sh",                   optional_argument, nullptr, 'z' },
            { "sh-output",            required_argument, nullptr, 'o' },
            { "sh-irradiance",              no_argument, nullptr, 'i' },
            { "sh-shader",                  no_argument, nullptr, 'b' },
            { "ibl-is-mipmap",        required_argument, nullptr, 'y' },
            { "ibl-ld",               required_argument, nullptr, 'p' },
            { "ibl-irradiance",       required_argument, nullptr, 'P' },
            { "ibl-dfg",              required_argument, nullptr, 'a' },
            { "ibl-dfg-multiscatter",       no_argument, nullptr, 'u' },
            { "ibl-samples",          required_argument, nullptr, 'k' },
            { "deploy",               required_argument, nullptr, 'x' },
            { "no-mirror",                  no_argument, nullptr, 'm' },
            { "debug",                      no_argument, nullptr, 'd' },
            { nullptr, 0, 0, 0 }  // termination of the option list
    };
    int opt;
    int option_index = 0;
    int num_sh_bands = 3;
    bool format_specified = false;
    bool type_specified = false;
    bool ktx_format_requested = false;
    while ((opt = getopt_long(argc, argv, OPTSTR, OPTIONS, &option_index)) >= 0) {
        std::string arg(optarg ? optarg : "");
        switch (opt) {
            default:
            case 'h':
                printUsage(argv[0]);
                exit(0);
                break;
            case 'l':
                license();
                exit(0);
                break;
            case 'q':
                g_quiet = true;
                break;
            case 't':
                if (arg == "cubemap") {
                    g_type = OutputType::FACES;
                    type_specified = true;
                }
                if (arg == "ktx") {
                    g_type = OutputType::KTX;
                    type_specified = true;
                }
                if ((arg == "equirect") || (arg == "equirectangular")) {
                    g_type = OutputType::EQUIRECT;
                    type_specified = true;
                }
                if (arg == "octahedron") {
                    g_type = OutputType::OCTAHEDRON;
                    type_specified = true;
                }
                break;
            case 'f':
                if (arg == "png") {
                    g_format = ImageEncoder::Format::PNG;
                    format_specified = true;
                }
                if (arg == "hdr") {
                    g_format = ImageEncoder::Format::HDR;
                    format_specified = true;
                }
                if (arg == "rgbm") {
                    g_format = ImageEncoder::Format::RGBM;
                    format_specified = true;
                }
                if (arg == "exr") {
                    g_format = ImageEncoder::Format::EXR;
                    format_specified = true;
                }
                if (arg == "psd") {
                    g_format = ImageEncoder::Format::PSD;
                    format_specified = true;
                }
                if (arg == "dds") {
                    g_format = ImageEncoder::Format::DDS_LINEAR;
                    format_specified = true;
                }
                if (arg == "ktx") {
                    ktx_format_requested = true;
                    format_specified = true;
                }
                break;
            case 'c':
                g_compression = arg;
                break;
            case 's':
                g_output_size = std::stoul(arg);
                if (!isPOT(g_output_size)) {
                    std::cerr << "output size must be a power of two" << std::endl;
                    exit(0);
                }
                break;
            case 'z':
                g_sh_compute = 1;
                g_sh_output = true;
                try {
                    num_sh_bands = std::stoi(arg);
                } catch (std::invalid_argument &e) {
                    // keep default value
                }
                break;
            case 'o':
                g_sh_compute = 1;
                g_sh_output = true;
                g_sh_file = ShFile::SH_CROSS;
                g_sh_filename = arg;
                if (g_sh_filename.getExtension() == "txt") {
                    g_sh_file = ShFile::SH_TEXT;
                }
                break;
            case 'i':
                g_sh_compute = 1;
                g_sh_irradiance = true;
                break;
            case 'b':
                g_sh_compute = 1;
                g_sh_irradiance = true;
                g_sh_shader = true;
                break;
            case 'e':
                g_extract_dir = arg;
                g_extract_faces = true;
                break;
            case 'r':
                g_extract_blur = std::stod(arg);
                if (g_extract_blur < 0 || g_extract_blur > 1) {
                    std::cerr << "roughness (blur) parameter must be between 0.0 and 1.0" <<
                    std::endl;
                    exit(0);
                }
                break;
            case 'y':
                g_is_mipmap = true;
                g_is_mipmap_dir = arg;
                break;
            case 'p':
                g_prefilter = true;
                g_prefilter_dir = arg;
                break;
            case 'P':
                g_ibl_irradiance = true;
                g_ibl_irradiance_dir = arg;
                break;
            case 'a':
                g_dfg = true;
                g_dfg_filename = arg;
                break;
            case 'u':
                g_dfg_multiscatter = true;
                break;
            case 'k':
                g_num_samples = (size_t)std::stoi(arg);
                break;
            case 'x':
                g_deploy = true;
                g_deploy_dir = arg;
                break;
            case 'd':
                g_debug = true;
                break;
            case 'm':
                g_mirror = true;
                break;
        }
    }

    if (ktx_format_requested) {
        g_type = OutputType::KTX;
        type_specified = true;
    }

    if (g_deploy && !type_specified) {
        g_type = OutputType::FACES;
    }

    if (g_deploy && !format_specified) {
        g_format = ImageEncoder::Format::RGBM;
    }

    if (num_sh_bands && g_sh_compute) {
        g_sh_compute = (size_t) num_sh_bands;
    }
    return optind;
}

int main(int argc, char* argv[]) {
    int option_index = handleCommandLineArgments(argc, argv);
    int num_args = argc - option_index;
    if (!g_dfg && num_args < 1) {
        printUsage(argv[0]);
        return 1;
    }

    if (g_dfg) {
        if (!g_quiet) {
            std::cout << "Generating IBL DFG LUT..." << std::endl;
        }
        size_t size = g_output_size ? g_output_size : DFG_LUT_DEFAULT_SIZE;
        iblLutDfg(g_dfg_filename, size, g_dfg_multiscatter);
        if (num_args < 1) return 0;
    }

    std::string command(argv[option_index]);
    utils::Path iname(command);

    if (g_deploy) {
        utils::Path out_dir = g_deploy_dir + iname.getNameWithoutExtension();

        // generate pre-scaled irradiance sh to text file
        g_sh_compute = 3;
        g_sh_shader = true;
        g_sh_irradiance = true;
        g_sh_filename = out_dir + "sh.txt";
        g_sh_file = ShFile::SH_TEXT;
        g_sh_output = true;

        // faces
        g_extract_dir = g_deploy_dir;
        g_extract_faces = true;

        // prefilter
        g_prefilter = true;
        g_prefilter_dir = g_deploy_dir;
    }

    if (g_debug) {
        if (g_prefilter && !g_is_mipmap) {
            g_is_mipmap = true;
            g_is_mipmap_dir = g_prefilter_dir;
        }
    }

    // Images store the actual data
    std::vector<Image> images;

    // Cubemaps are just views on Images
    std::vector<Cubemap> levels;

    if (iname.exists()) {
        if (!g_quiet) {
            std::cout << "Decoding image..." << std::endl;
        }
        std::ifstream input_stream(iname.getPath(), std::ios::binary);
        LinearImage linputImage = ImageDecoder::decode(input_stream, iname.getPath());
        if (!linputImage.isValid()) {
            std::cerr << "Unable to open image: " << iname.getPath() << std::endl;
            exit(1);
        }
        if (linputImage.getChannels() != 3) {
            std::cerr << "Input image must be RGB (3 channels)! This image has "
                      << linputImage.getChannels() << " channels." << std::endl;
            exit(1);
        }

        // Convert from LinearImage to the deprecated Image object which is used throughout cmgen.
        std::unique_ptr<uint8_t[]> buf(new uint8_t[
                linputImage.getWidth() * linputImage.getHeight() * sizeof(float3)]);
        const size_t width = linputImage.getWidth(), height = linputImage.getHeight();
        const size_t bpp = sizeof(float) * 3, bpr = bpp * width;
        memcpy(buf.get(), linputImage.getPixelRef(), height * bpr);
        Image inputImage(std::move(buf), width, height, bpr, bpp);

        CubemapUtils::clamp(inputImage);

        if ((isPOT(width) && (width * 3 == height * 4)) ||
            (isPOT(height) && (height * 3 == width * 4))) {
            // This is cross cubemap
            size_t dim = g_output_size ? g_output_size : IBL_DEFAULT_SIZE;
            if (!g_quiet) {
                std::cout << "Loading cross... " << std::endl;
            }

            Image temp;
            Cubemap cml = CubemapUtils::create(temp, dim);
            CubemapUtils::crossToCubemap(cml, inputImage);
            images.push_back(std::move(temp));
            levels.push_back(std::move(cml));
        } else if (width == 2 * height) {
            // we assume a spherical (equirectangular) image, which we will convert to a cross image
            size_t dim = g_output_size ? g_output_size : IBL_DEFAULT_SIZE;
            if (!g_quiet) {
                std::cout << "Converting equirectangular image... " << std::endl;
            }
            Image temp;
            Cubemap cml = CubemapUtils::create(temp, dim);
            CubemapUtils::equirectangularToCubemap(cml, inputImage);
            images.push_back(std::move(temp));
            levels.push_back(std::move(cml));
        } else {
            std::cerr << "Aspect ratio not supported: " << width << "x" << height << std::endl;
            std::cerr << "Supported aspect ratios:" << std::endl;
            std::cerr << "  2:1, lat/long or equirectangular" << std::endl;
            std::cerr << "  3:4, vertical cross (height must be power of two)" << std::endl;
            std::cerr << "  4:3, horizontal cross (width must be power of two)" << std::endl;
            exit(0);
        }
    } else {
        if (!g_quiet) {
            std::cout << iname << " does not exist; generating UV grid..." << std::endl;
        }

        size_t dim = g_output_size ? g_output_size : IBL_DEFAULT_SIZE;
        Image temp;
        Cubemap cml = CubemapUtils::create(temp, dim);

        unsigned int p = 0;
        std::string name = iname.getNameWithoutExtension();
        if (sscanf(name.c_str(), "uv%u", &p) == 1) {
            CubemapUtils::generateUVGrid(cml, p, p);
        } else if (sscanf(name.c_str(), "u%u", &p) == 1) {
            CubemapUtils::generateUVGrid(cml, p, 1);
        } else if (sscanf(name.c_str(), "v%u", &p) == 1) {
            CubemapUtils::generateUVGrid(cml, 1, p);
        } else if (sscanf(name.c_str(), "brdf%u", &p) == 1) {
            double linear_roughness = sq(p / std::log2(dim));
            CubemapIBL::brdf(cml, linear_roughness);
        } else {
            CubemapUtils::generateUVGrid(cml, 1, 1);
        }

        images.push_back(std::move(temp));
        levels.push_back(std::move(cml));
    }

    // we mirror by default -- the mirror option in fact un-mirrors.
    g_mirror = !g_mirror;
    if (g_mirror) {
        if (!g_quiet) {
            std::cout << "Mirroring..." << std::endl;
        }
        Image temp;
        Cubemap cml = CubemapUtils::create(temp, levels[0].getDimensions());
        CubemapUtils::mirrorCubemap(cml, levels[0]);
        std::swap(levels[0], cml);
        std::swap(images[0], temp);
    } else {
        if (!g_quiet) {
            std::cout << "Skipped mirroring." << std::endl;
        }
    }

    // make the cubemap seamless
    levels[0].makeSeamless();

    // Now generate all the mipmap levels
    generateMipmaps(levels, images);

    if (g_sh_compute) {
        if (!g_quiet) {
            std::cout << "Spherical harmonics..." << std::endl;
        }
        Cubemap const& cm(levels[0]);
        sphericalHarmonics(iname, cm);
    }

    if (g_is_mipmap) {
        if (!g_quiet) {
            std::cout << "IBL mipmaps for prefiltered importance sampling..." << std::endl;
        }
        iblMipmapPrefilter(iname, images, levels, g_is_mipmap_dir);
    }

    if (g_prefilter) {
        if (!g_quiet) {
            std::cout << "IBL prefiltering..." << std::endl;
        }
        iblRoughnessPrefilter(iname, levels, g_prefilter_dir);
    }

    if (g_ibl_irradiance) {
        if (!g_quiet) {
            std::cout << "IBL diffuse irradiance..." << std::endl;
        }
        iblDiffuseIrradiance(iname, levels, g_ibl_irradiance_dir);
    }

    if (g_extract_faces) {
        Cubemap const& cm(levels[0]);
        if (g_extract_blur != 0) {
            if (!g_quiet) {
                std::cout << "Blurring..." << std::endl;
            }
            const double linear_roughness = g_extract_blur * g_extract_blur;
            const size_t dim = g_output_size ? g_output_size : cm.getDimensions();
            Image image;
            Cubemap blurred = CubemapUtils::create(image, dim);
            CubemapIBL::roughnessFilter(blurred, levels, linear_roughness, g_num_samples);
            if (!g_quiet) {
                std::cout << "Extract faces..." << std::endl;
            }
            extractCubemapFaces(iname, blurred, g_extract_dir);
        } else {
            if (!g_quiet) {
                std::cout << "Extract faces..." << std::endl;
            }
            extractCubemapFaces(iname, cm, g_extract_dir);
        }
    }

    return 0;
}

void generateMipmaps(std::vector<Cubemap>& levels, std::vector<Image>& images) {
    Image temp;
    const Cubemap& base(levels[0]);
    size_t dim = base.getDimensions();
    size_t mipLevel = 0;
    while (dim > 1) {
        dim >>= 1;
        Cubemap dst = CubemapUtils::create(temp, dim);
        const Cubemap& src(levels[mipLevel++]);
        CubemapUtils::downsampleCubemapLevelBoxFilter(dst, src);
        dst.makeSeamless();
        images.push_back(std::move(temp));
        levels.push_back(std::move(dst));
    }
}

void sphericalHarmonics(const utils::Path& iname, const Cubemap& inputCubemap) {
    std::unique_ptr<math::double3[]> sh;
    if (g_sh_shader) {
        sh = CubemapSH::computeIrradianceSH3Bands(inputCubemap);
    } else {
        sh = CubemapSH::computeSH(inputCubemap, g_sh_compute, g_sh_irradiance);
    }

    if (g_sh_output) {
        outputSh(std::cout, sh, g_sh_compute);
    }

    if (g_sh_file != ShFile::SH_NONE || g_debug) {
        Image image;
        const size_t dim = g_output_size ? g_output_size : inputCubemap.getDimensions();
        Cubemap cm = CubemapUtils::create(image, dim);

        if (g_sh_file != ShFile::SH_NONE) {
            utils::Path outputDir(g_sh_filename.getAbsolutePath().getParent());
            if (!outputDir.exists()) {
                outputDir.mkdirRecursive();
            }

            if (g_sh_shader) {
                CubemapSH::renderPreScaledSH3Bands(cm, sh);
            } else {
                CubemapSH::renderSH(cm, sh, g_sh_compute);
            }

            if (g_sh_file == ShFile::SH_CROSS) {
                saveImage(g_sh_filename, ImageEncoder::chooseFormat(g_sh_filename.getName()),
                        image, g_compression);
            }
            if (g_sh_file == ShFile::SH_TEXT) {
                std::ofstream outputStream(g_sh_filename, std::ios::trunc);
                outputSh(outputStream, sh, g_sh_compute);
            }
        }

        if (g_debug) {
            utils::Path outputDir(g_sh_filename.getAbsolutePath().getParent());
            if (!outputDir.exists()) {
                outputDir.mkdirRecursive();
            }

            { // save a file with what we just calculated (radiance or irradiance)
                std::string basename = iname.getNameWithoutExtension();
                utils::Path filePath =
                        outputDir + (basename + "_sh" + (g_sh_irradiance ? "_i" : "_r") + ".hdr");
                saveImage(filePath, ImageEncoder::Format::HDR, image, "");
            }

            { // save a file with the "other one" (irradiance or radiance)
                sh = CubemapSH::computeSH(inputCubemap, g_sh_compute, !g_sh_irradiance);
                CubemapSH::renderSH(cm, sh, g_sh_compute);
                std::string basename = iname.getNameWithoutExtension();
                utils::Path filePath =
                        outputDir + (basename + "_sh" + (!g_sh_irradiance ? "_i" : "_r") + ".hdr");
                saveImage(filePath, ImageEncoder::Format::HDR, image, "");
            }
        }
    }
    // Stash the computed coefficients in case we need to use them at a later stage (e.g. KTX gen)
    g_coefficients = std::move(sh);
}

void outputSh(std::ostream& out,
        const std::unique_ptr<math::double3[]>& sh, size_t numBands) {
    for (ssize_t l=0 ; l<numBands ; l++) {
        for (ssize_t m=-l ; m<=l ; m++) {
            size_t i = CubemapSH::getShIndex(m, (size_t) l);
            std::string name = "L" + std::to_string(l) + std::to_string(m);
            if (g_sh_irradiance) {
                name.append(", irradiance");
            }
            if (g_sh_shader) {
                name.append(", pre-scaled base");
            }
            out << "("
                << std::fixed << std::setprecision(15) << std::setw(18) << sh[i].r << ", "
                << std::fixed << std::setprecision(15) << std::setw(18) << sh[i].g << ", "
                << std::fixed << std::setprecision(15) << std::setw(18) << sh[i].b
                << "); // " << name
                << std::endl;
        }
    }
}

void outputSpectrum(std::ostream& out,
        const std::unique_ptr<math::double3[]>& sh, size_t numBands) {
    // We assume a symetrical function (i.e. m!=0 terms are zero)
    for (ssize_t l=0 ; l<numBands ; l++) {
        size_t i = CubemapSH::getShIndex(0, (size_t) l);
        double L = dot(sh[i], double3{ 0.2126, 0.7152, 0.0722 });
        out << std::fixed << std::setprecision(15) << std::setw(18) << sq(L) << std::endl;
    }
}

void iblMipmapPrefilter(const utils::Path& iname,
        const std::vector<Image>& images, const std::vector<Cubemap>& levels,
        const utils::Path& dir) {
    utils::Path outputDir(dir.getAbsolutePath() + iname.getNameWithoutExtension());
    if (!outputDir.exists()) {
        outputDir.mkdirRecursive();
    }

    const size_t numLevels = levels.size();
    for (size_t level=0 ; level<numLevels ; level++) {
        Cubemap const& dst(levels[level]);
        Image const& img(images[level]);
        if (g_debug) {
            ImageEncoder::Format debug_format = ImageEncoder::Format::HDR;
            std::string ext = ImageEncoder::chooseExtension(debug_format);
            std::string basename = iname.getNameWithoutExtension();
            utils::Path filePath = outputDir + (basename + "_is_m" + (std::to_string(level) + ext));
            saveImage(filePath, debug_format, img, g_compression);
        }

        std::string ext = ImageEncoder::chooseExtension(g_format);

        if (g_type == OutputType::EQUIRECT) {
            size_t dim = dst.getDimensions();
            std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * 2 * dim * sizeof(float3)]);
            Image image(std::move(buf), dim * 2, dim, dim * 2 * sizeof(float3), sizeof(float3));
            CubemapUtils::cubemapToEquirectangular(image, dst);
            std::string filename = outputDir + ("is_m" + std::to_string(level) + ext);
            saveImage(filename, g_format, image, g_compression);
            continue;
        }

        if (g_type == OutputType::OCTAHEDRON) {
            size_t dim = dst.getDimensions();
            std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * dim * sizeof(float3)]);
            Image image(std::move(buf), dim, dim, dim * sizeof(float3), sizeof(float3));
            CubemapUtils::cubemapToOctahedron(image, dst);
            std::string filename = outputDir + ("is_m" + std::to_string(level) + ext);
            saveImage(filename, g_format, image, g_compression);
            continue;
        }

        for (size_t i = 0; i < 6; i++) {
            Cubemap::Face face = (Cubemap::Face)i;
            std::string filename = outputDir
                    + ("is_m" + std::to_string(level) + "_" + CubemapUtils::getFaceName(face) + ext);
            saveImage(filename, g_format, dst.getImageForFace(face), g_compression);
        }
    }
}

void iblRoughnessPrefilter(const utils::Path& iname,
        const std::vector<Cubemap>& levels, const utils::Path& dir) {
    utils::Path outputDir(dir.getAbsolutePath() + iname.getNameWithoutExtension());
    if (!outputDir.exists()) {
        outputDir.mkdirRecursive();
    }

    // DEBUG: enable this to generate prefilter mipmaps at full resolution
    // (of course, they're not mimaps at this point)
    // This is useful for debugging.
    const bool DEBUG_FULL_RESOLUTION = false;

    const size_t baseExp = __builtin_ctz(g_output_size ? g_output_size : IBL_DEFAULT_SIZE);
    size_t numSamples = g_num_samples;
    const size_t numLevels = baseExp + 1;

    // It's convenient to create an empty KTX bundle on the stack in this scope, regardless of
    // whether KTX is requested. It does not consume memory if empty.
    KtxBundle container(numLevels, 1, true);
    container.info() = {
        .endianness = KtxBundle::ENDIAN_DEFAULT,
        .glType = KtxBundle::UNSIGNED_BYTE,
        .glTypeSize = 4,
        .glFormat = KtxBundle::RGBA,
        .glInternalFormat = KtxBundle::RGBA,
        .glBaseInternalFormat = KtxBundle::RGBA,
        .pixelWidth = 1U << baseExp,
        .pixelHeight = 1U << baseExp,
        .pixelDepth = 0,
    };

    for (ssize_t i = baseExp; i >= 0; --i) {
        const size_t dim = 1U << (DEBUG_FULL_RESOLUTION ? baseExp : i);
        const size_t level = baseExp - i;
        if (level >= 2) {
            // starting at level 2, we increase the number of samples per level
            // this helps as the filter gets wider, and since there are 4x less work
            // per level, this doesn't slow things down a lot.
            if (!DEBUG_FULL_RESOLUTION) {
                numSamples *= 2;
            }
        }

        const double lod = saturate(level / (numLevels - 1.0));
        // map the lod to a linear_roughness,  here we're using ^2, but other mappings are possible.
        // ==> lod = sqrt(linear_roughness)
        const double linear_roughness = lod * lod;
        if (!g_quiet) {
            std::cout << "Level " << level <<
                    std::setprecision(3)
                    << ", roughness(lin) = " << linear_roughness
                    << ", roughness = " << sqrt(linear_roughness)
                    << std::endl;
        }
        Image image;
        Cubemap dst = CubemapUtils::create(image, dim);
        CubemapIBL::roughnessFilter(dst, levels, linear_roughness, numSamples);
        dst.makeSeamless();

        if (g_debug) {
            ImageEncoder::Format debug_format = ImageEncoder::Format::HDR;
            std::string ext = ImageEncoder::chooseExtension(debug_format);
            std::string basename = iname.getNameWithoutExtension();
            utils::Path filePath = outputDir + (basename + "_roughness_m" + (std::to_string(level) + ext));
            saveImage(filePath, debug_format, image, g_compression);
        }

        std::string ext = ImageEncoder::chooseExtension(g_format);

        if (g_type == OutputType::KTX) {
            exportKtxFaces(container, level, dst);
            continue;
        }

        if (g_type == OutputType::EQUIRECT) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * 2 * dim * sizeof(float3)]);
            Image image(std::move(buf), dim * 2, dim, dim * 2 * sizeof(float3), sizeof(float3));
            CubemapUtils::cubemapToEquirectangular(image, dst);
            std::string filename = outputDir + ("m" + std::to_string(level) + ext);
            saveImage(filename, g_format, image, g_compression);
            continue;
        }

        if (g_type == OutputType::OCTAHEDRON) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * dim * sizeof(float3)]);
            Image image(std::move(buf), dim, dim, dim * sizeof(float3), sizeof(float3));
            CubemapUtils::cubemapToOctahedron(image, dst);
            std::string filename = outputDir + ("m" + std::to_string(level) + ext);
            saveImage(filename, g_format, image, g_compression);
            continue;
        }

        for (size_t j = 0; j < 6; j++) {
            Cubemap::Face face = (Cubemap::Face) j;
            std::string filename = outputDir
                    + ("m" + std::to_string(level) + "_" + CubemapUtils::getFaceName(face) + ext);
            saveImage(filename, g_format, dst.getImageForFace(face), g_compression);
        }
    }

    if (g_type == OutputType::KTX) {
        using namespace std;
        if (g_coefficients) {
            ostringstream sstr;
            for (ssize_t l = 0; l < g_sh_compute; l++) {
                for (ssize_t m = -l; m <= l; m++) {
                    auto v = g_coefficients[CubemapSH::getShIndex(m, (size_t) l)];
                    sstr << v.r << " " << v.g << " " << v.b << "\n";
                }
            }
            container.setMetadata("sh", sstr.str().c_str());
        }
        vector<uint8_t> fileContents(container.getSerializedLength());
        container.serialize(fileContents.data(), fileContents.size());
        string filename = iname.getNameWithoutExtension() + "_ibl.ktx";
        auto fullpath = outputDir + filename;
        ofstream outputStream(fullpath.c_str(), ios::out | ios::binary);
        outputStream.write((const char*) fileContents.data(), fileContents.size());
        outputStream.close();
    }
}

void iblDiffuseIrradiance(const utils::Path& iname,
        const std::vector<Cubemap>& levels, const utils::Path& dir) {
    utils::Path outputDir(dir.getAbsolutePath() + iname.getNameWithoutExtension());
    if (!outputDir.exists()) {
        outputDir.mkdirRecursive();
    }

    const size_t baseExp = __builtin_ctz(g_output_size ? g_output_size : IBL_DEFAULT_SIZE);
    size_t numSamples = g_num_samples;
    const size_t dim = 1U << baseExp;
    Image image;
    Cubemap dst = CubemapUtils::create(image, dim);
    CubemapIBL::diffuseIrradiance(dst, levels, numSamples);

    std::string ext = ImageEncoder::chooseExtension(g_format);
    for (size_t j = 0; j < 6; j++) {
        Cubemap::Face face = (Cubemap::Face) j;
        std::string filename = outputDir + ("i_" + CubemapUtils::getFaceName(face) + ext);
        saveImage(filename, g_format, dst.getImageForFace(face), g_compression);
    }

    if (g_debug) {
        ImageEncoder::Format debug_format = ImageEncoder::Format::HDR;
        std::string basename = iname.getNameWithoutExtension();
        std::string ext = ImageEncoder::chooseExtension(debug_format);
        utils::Path filePath = outputDir + (basename + "_diffuse_irradiance" + ext);
        saveImage(filePath, debug_format, image, "");

        // this generates SHs from the importance-sampled version above. This is just used
        // to compare the resuts and see if the later is better.
        Image image;
        Cubemap cm = CubemapUtils::create(image, dim);
        auto sh = CubemapSH::computeSH(dst, g_sh_compute, false);
        CubemapSH::renderSH(cm, sh, g_sh_compute);
        filePath = outputDir + (basename + "_diffuse_irradiance_sh" + ext);
        saveImage(filePath, debug_format, image, "");
    }
}

static bool isTextFile(const utils::Path& filename) {
    std::string extension(filename.getExtension());
    return extension == "h" || extension == "hpp" ||
           extension == "c" || extension == "cpp" ||
           extension == "inc" || extension == "txt";
}

static bool isIncludeFile(const utils::Path& filename) {
    std::string extension(filename.getExtension());
    return extension == "inc";
}

void iblLutDfg(const utils::Path& filename, size_t size, bool multiscatter) {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[size*size*sizeof(float3)]);
    Image image(std::move(buf), size, size, size*sizeof(float3), sizeof(float3));
    CubemapIBL::DFG(image, multiscatter);

    utils::Path outputDir(filename.getAbsolutePath().getParent());
    if (!outputDir.exists()) {
        outputDir.mkdirRecursive();
    }

    if (isTextFile(filename)) {
        const bool isInclude = isIncludeFile(filename);
        std::ofstream outputStream(filename, std::ios::trunc);

        outputStream << "// generated with: cmgen --ibl-dfg=" << filename.c_str() << std::endl;
        outputStream << "// DFG LUT stored as an RG16F texture, in GL order" << std::endl;
        if (!isInclude) {
            outputStream << "const uint16_t DFG_LUT[] = {";
        }
        for (size_t y = 0; y < size; y++) {
            for (size_t x = 0; x < size; x++) {
                if (x % 4 == 0) outputStream << std::endl << "    ";
                const half2 d = half2(static_cast<float3*>(image.getPixelRef(x, size - 1 - y))->xy);
                const uint16_t r = *reinterpret_cast<const uint16_t*>(&d.r);
                const uint16_t g = *reinterpret_cast<const uint16_t*>(&d.g);
                outputStream << "0x" << std::setfill('0') << std::setw(4) << std::hex << r << ", ";
                outputStream << "0x" << std::setfill('0') << std::setw(4) << std::hex << g << ", ";
            }
        }
        if (!isInclude) {
            outputStream << std::endl << "};" << std::endl;
        }

        outputStream << std::endl;
        outputStream.flush();
        outputStream.close();
    } else {
        ImageEncoder::Format format = ImageEncoder::chooseFormat(filename.getName(), true);
        saveImage(filename, format, image, g_compression);
    }
}

void extractCubemapFaces(const utils::Path& iname, const Cubemap& cm, const utils::Path& dir) {
    utils::Path outputDir(dir.getAbsolutePath() + iname.getNameWithoutExtension());
    if (!outputDir.exists()) {
        outputDir.mkdirRecursive();
    }

    if (g_type == OutputType::KTX) {
        using namespace std;
        const uint32_t dim = cm.getDimensions();
        KtxBundle container(1, 1, true);
        container.info() = {
            .endianness = KtxBundle::ENDIAN_DEFAULT,
            .glType = KtxBundle::UNSIGNED_BYTE,
            .glTypeSize = 4,
            .glFormat = KtxBundle::RGBA,
            .glInternalFormat = KtxBundle::RGBA,
            .glBaseInternalFormat = KtxBundle::RGBA,
            .pixelWidth = dim,
            .pixelHeight = dim,
            .pixelDepth = 0,
        };
        exportKtxFaces(container, 0, cm);
        string filename = iname.getNameWithoutExtension() + "_skybox.ktx";
        auto fullpath = outputDir + filename;
        vector<uint8_t> fileContents(container.getSerializedLength());
        container.serialize(fileContents.data(), fileContents.size());
        ofstream outputStream(fullpath.c_str(), ios::out | ios::binary);
        outputStream.write((const char*) fileContents.data(), fileContents.size());
        outputStream.close();
        return;
    }

    std::string ext = ImageEncoder::chooseExtension(g_format);

    if (g_type == OutputType::EQUIRECT) {
        size_t dim = cm.getDimensions();
        std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * 2 * dim * sizeof(float3)]);
        Image image(std::move(buf), dim * 2, dim, dim * 2 * sizeof(float3), sizeof(float3));
        CubemapUtils::cubemapToEquirectangular(image, cm);
        std::string filename = outputDir + ("skybox" + ext);
        saveImage(filename, g_format, image, g_compression);
        return;
    }

    if (g_type == OutputType::OCTAHEDRON) {
        size_t dim = cm.getDimensions();
        std::unique_ptr<uint8_t[]> buf(new uint8_t[dim * dim * sizeof(float3)]);
        Image image(std::move(buf), dim, dim, dim * sizeof(float3), sizeof(float3));
        CubemapUtils::cubemapToOctahedron(image, cm);
        std::string filename = outputDir + ("skybox" + ext);
        saveImage(filename, g_format, image, g_compression);
        return;
    }

    for (size_t i = 0; i < 6; i++) {
        Cubemap::Face face = (Cubemap::Face) i;
        std::string filename(outputDir + (CubemapUtils::getFaceName(face) + ext));
        saveImage(filename, g_format, cm.getImageForFace(face), g_compression);
    }
}

// Converts a cmgen Image into a libimage LinearImage
static LinearImage toLinearImage(const Image& image) {
    LinearImage linearImage(image.getWidth(), image.getHeight(), 3);

    // Copy row by row since the image has padding.
    assert(image.getBytesPerPixel() == 12);
    const size_t w = image.getWidth(), h = image.getHeight();
    for (size_t row = 0; row < h; ++row) {
        float* dst = linearImage.getPixelRef(0, row);
        float const* src = static_cast<float const*>(image.getPixelRef(0, row));
        memcpy(dst, src, w * 12);
    }
    return linearImage;
}

static void saveImage(const std::string& path, ImageEncoder::Format format, const Image& image,
        const std::string& compression) {
    std::ofstream outputStream(path, std::ios::binary | std::ios::trunc);
    if (!ImageEncoder::encode(outputStream, format, toLinearImage(image), compression, path)) {
        exit(1);
    }
}

static void exportKtxFaces(KtxBundle& container, uint32_t miplevel, const Cubemap& cm) {
    CompressionConfig compression {};
    auto& info = container.info();
    if (!g_compression.empty()) {
        bool valid = parseOptionString(g_compression, &compression);
        if (!valid) {
            std::cerr << "Unrecognized compression: " << g_compression << std::endl;
            exit(1);
        }
        // The KTX spec says the following for compressed textures: glTypeSize should 1,
        // glFormat should be 0, and glBaseInternalFormat should be RED, RG, RGB, or RGBA.
        // The glInternalFormat field is the only field that specifies the actual format.
        info.glTypeSize = 1;
        info.glFormat = 0;
        info.glBaseInternalFormat = KtxBundle::RGBA;
    }

    const uint32_t dim = cm.getDimensions();
    for (uint32_t j = 0; j < 6; j++) {
        KtxBlobIndex blobIndex {(uint32_t) miplevel, 0, j};
        Cubemap::Face face;
        switch (j) {
            case 0: face = Cubemap::Face::PX; break;
            case 1: face = Cubemap::Face::NX; break;
            case 2: face = Cubemap::Face::PY; break;
            case 3: face = Cubemap::Face::NY; break;
            case 4: face = Cubemap::Face::PZ; break;
            case 5: face = Cubemap::Face::NZ; break;
        }
        LinearImage image = toLinearImage(cm.getImageForFace(face));

        if (compression.type != CompressionConfig::INVALID) {
            CompressedTexture tex = compressTexture(compression, fromLinearToRGBM(image));
            container.setBlob(blobIndex, tex.data.get(), tex.size);
            // TODO: assert that the format is consistent across all blobs?
            info.glInternalFormat = (uint32_t) tex.format;
            continue;
        }

        auto uintData = fromLinearToRGBM<uint8_t>(image);
        container.setBlob(blobIndex, uintData.get(), dim * dim * 4);
    }
}
