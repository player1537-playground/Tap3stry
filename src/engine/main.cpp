//std
#include <cstdarg> // std::va_list, va_start, va_end
#include <cstdlib> // std::exit
#include <cstdio> // std::fprintf, std::vfprintf, std::fopen, std::ftell, std::fseek, std::fclose, stderr
#include <cstring> // std::memcpy
#include <string> // std::string
#include <vector> // std::vector
#include <tuple> // std::make_tuple, std::tie
#include <iostream> // std::cin
#include <map> // std::map
#include <chrono> // std::chrono

//ospray
#include <ospray/ospray.h>
#include <ospray/ospray_util.h>

//stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

static void xDie(const char *fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "Error: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    std::exit(EXIT_FAILURE);
}

struct stbiContext {
    size_t offset;
    size_t *size;
    void **data;
};

static void stbiCallback(void *context_, void *data, int size) {
    stbiContext *context = static_cast<stbiContext *>(context_);

    while (context->offset + size >= *context->size) {
        *context->size *= 2;
        *context->data = std::realloc(*context->data, *context->size);
    }

    void *dest = static_cast<std::byte *>(*context->data) + context->offset;
    const void *src = data;
    std::size_t count = size;
    std::memcpy(dest, src, count);

    context->offset += size;
}

static size_t __attribute__((unused)) xToJPG(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
    if (*outsize == 0) {
        *outsize = 1024;
        *outdata = std::malloc(*outsize);
    }

    stbiContext context;
    context.offset = 0;
    context.size = outsize;
    context.data = outdata;

    int success;
    stbi_write_func *func = stbiCallback;
    void *context_ = &context;
    int w = width;
    int h = height;
    int comp = 4;
    const void *data = rgba;
    int quality = 95;
    success = stbi_write_jpg_to_func(func, context_, w, h, comp, data, quality);
    if (!success) xDie("Failed to stbi_write_jpg_to_func");

    return context.offset;
}

static size_t xToPNG(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
    if (*outsize == 0) {
        *outsize = 1024;
        *outdata = std::malloc(*outsize);
    }

    stbiContext context;
    context.offset = 0;
    context.size = outsize;
    context.data = outdata;

    int success;
    stbi_write_func *func = stbiCallback;
    void *context_ = &context;
    int w = width;
    int h = height;
    int comp = 4;
    const void *data = rgba;
    int stride = 0;
    success = stbi_write_png_to_func(func, context_, w, h, comp, data, stride);
    if (!success) xDie("Failed to stbi_write_png_to_func");

    return context.offset;
}

static void __attribute__((unused)) xWriteBytes(const std::string &filename, size_t size, void *data) {
    std::FILE *file;
    file = fopen(filename.c_str(), "wb");
    if (!file) xDie("Failed to fopen: %s", filename.c_str());

    std::size_t nwrite;
    nwrite = std::fwrite(data, 1, size, file);
    if (nwrite < size) xDie("Failed to write all: %zu < %zu", nwrite, size);

    std::fclose(file);
}

static void *xReadBytes(const std::string &filename) {
    std::FILE *file;
    file = fopen(filename.c_str(), "rb");
    if (!file) xDie("Failed to fopen: %s", filename.c_str());

    {
        int rv = std::fseek(file, 0, SEEK_END);
        if (rv) xDie("Failed to fseek: %s", filename.c_str());
    }

    long nbyte;
    nbyte = std::ftell(file);
    if (nbyte < 0) xDie("Failed to ftell: %s", filename.c_str());

    {
        int rv = std::fseek(file, 0, SEEK_SET);
        if (rv) xDie("Failed to fseek: %s", filename.c_str());
    }

    void *data;
    data = new uint8_t[nbyte];
    for (size_t i=0, n=nbyte; i<n; ++i) {
        static_cast<uint8_t *>(data)[i] = 0x33;
    }
    std::size_t nread = std::fread(data, 1, nbyte, file);
    if (static_cast<std::size_t>(nread) < static_cast<std::size_t>(nbyte)) {
        xDie("Failed to read everything: %zu < %zu", nread, nbyte);
    }

    std::fclose(file);

    return data;
}

template <class T>
static T xCommit(T& t) {
    ospCommit(t);
    return t;
}

template <class T>
static T xRetain(T& t) {
    ospRetain(t);
    return t;
}

static OSPData xNewSharedData(const void *sharedData, OSPDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3) {
    OSPData data;
    int64_t byteStride1 = 0;
    int64_t byteStride2 = 0;
    int64_t byteStride3 = 0;
    std::fprintf(stderr, "ospNewSharedData: %p, %d, %lu, %ld, %lu, %ld, %lu, %ld)\n",
        sharedData, dataType, numItems1, byteStride1, numItems2, byteStride2, numItems3, byteStride3);
    data = ospNewSharedData(sharedData, dataType, numItems1, byteStride1, numItems2, byteStride2, numItems3, byteStride3);

    return xCommit(data);
}

static std::map<
    std::tuple<std::string, int>,
    std::tuple<
        std::string,
        std::tuple<int, int, int>,
        std::tuple<float, float>
    >
> volumes = {
#   include "detail/volumes.h"
};

static OSPVolume xNewVolume(const std::string &name, int timestep) {
    using Key = std::tuple<std::string, int>;

    Key key{name, timestep};
    if (volumes.find(key) == volumes.end()) {
        std::fprintf(stderr, "ERROR: Unknown volume! %s, %d\n", name.c_str(), timestep);
        return nullptr;
    }

    std::string filename;
    std::tuple<float, float, float> dimensions;
    std::tie(filename, dimensions, std::ignore) = volumes[key];
    
    int d1, d2, d3;
    std::tie(d1, d2, d3) = dimensions;

    OSPVolume volume;
    const char *type = "structuredRegular";
    volume = ospNewVolume(type);

    OSPData data;
    data = ({
        OSPData data;
        const void *sharedData = xReadBytes(filename);
        OSPDataType dataType = OSP_FLOAT;
        uint64_t numItems1 = d1;
        uint64_t numItems2 = d2;
        uint64_t numItems3 = d3;
        data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);
    
        xCommit(data);
    });
    ospSetObject(volume, "data", data);

    float gridOrigin[3] = { -0.5f*d1, -0.5f*d2, -0.5f*d3 };
    ospSetParam(volume, "gridOrigin", OSP_VEC3F, gridOrigin);

    float densityScale[] = { 0.1 };
    ospSetParam(volume, "densityScale", OSP_FLOAT, densityScale);

    // float gridSpacing[3] = { 1.0f/d1, 1.0f/d2, 1.0f/d3 };
    // ospSetParam(volume, "gridSpacing", OSP_VEC3F, gridSpacing);

    // int cellCentered = 0;
    // ospSetParam(volume, "cellCentered", OSP_BOOL, &cellCentered);

    return volume;
}

static std::map<std::string, std::vector<float>> colorMaps{
#   include "detail/colormaps.h"
};

static OSPData xNewColorMap(const std::string &name) {
    if (colorMaps.find(name) == colorMaps.end()) {
        std::fprintf(stderr, "ERROR: Colormap not found: %s\n", name.c_str());
        return nullptr;
    }

    OSPData data;
    const void *sharedData = colorMaps[name].data();
    OSPDataType dataType = OSP_VEC3F;
    uint64_t numItems1 = colorMaps[name].size() / 3;
    uint64_t numItems2 = 1;
    uint64_t numItems3 = 1;
    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

    return data;
}

static OSPData xGetColorMap(const std::string &name) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPData> cache;

    Key key{name};
    if (cache.find(key) == cache.end()) {
        OSPData data;
        data = xNewColorMap(name);
        if (data == nullptr) {
            return nullptr;
        }

        cache[key] = xRetain(data);
    }

    return cache[key];
}

static std::map<std::string, std::vector<float>> opacityMaps{
#   include "detail/opacitymaps.h"
};

static OSPData xNewOpacityMap(const std::string &name) {
    if (opacityMaps.find(name) == opacityMaps.end()) {
        std::fprintf(stderr, "ERROR: Opacity map not found: %s\n", name.c_str());
        return nullptr;
    }

    OSPData data;
    const void *sharedData = opacityMaps[name].data();
    OSPDataType dataType = OSP_FLOAT;
    uint64_t numItems1 = opacityMaps[name].size();
    uint64_t numItems2 = 1;
    uint64_t numItems3 = 1;
    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

    return data;
}

static OSPData xGetOpacityMap(const std::string &name) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPData> cache;

    Key key{name};
    if (cache.find(key) == cache.end()) {
        OSPData data;
        data = xNewOpacityMap(name);
        if (data == nullptr) {
            return nullptr;
        }

        cache[key] = xRetain(data);
    }

    return cache[key];
}

static OSPTransferFunction xNewTransferFunction(
    const std::string &colorName,
    const std::string &opacityName
) {
    OSPTransferFunction transferFunction;
    const char *type = "piecewiseLinear";
    transferFunction = ospNewTransferFunction(type);

    OSPData color;
    color = ({
        OSPData data;
        data = xGetColorMap(colorName);
        if (data == nullptr) {
            return nullptr;
        }
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "color", color);

    OSPData opacity;
    opacity = ({
        OSPData data;
        data = xGetOpacityMap(opacityName);
        if (data == nullptr) {
            return nullptr;
        }
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "opacity", opacity);

    return transferFunction;
}

static OSPVolume xGetVolume(const std::string &name, int timestep) {
    using Key = std::tuple<std::string, int>;
    static std::map<Key, OSPVolume> cache;

    Key key{name, timestep};
    if (cache.find(key) == cache.end()) {
        cache[key] = ({
            OSPVolume volume;
            volume = xNewVolume(name, timestep);

            xRetain(volume);
        });
    }

    return cache[key];
}

static OSPGeometry xNewIsosurface(
    const std::string &volumeName,
    int timestep
) {
    OSPGeometry isosurface;
    isosurface = ({
        OSPGeometry geometry;
        const char *type = "isosurface";
        geometry = ospNewGeometry(type);
    });

    OSPVolume volume;
    volume = ({
        OSPVolume volume;
        volume = xGetVolume(volumeName, timestep);

        xCommit(volume);
    });
    ospSetObject(isosurface, "volume", volume);

    return isosurface;
}

static OSPGeometry xGetIsosurface(
    const std::string &volumeName,
    int timestep,
    const std::vector<float> &isosurfaceValues
) {
    using Key = std::tuple<std::string, int>;
    static std::map<Key, OSPGeometry> cache;
    Key key{volumeName, timestep};

    if (cache.find(key) == cache.end()) {
        OSPGeometry isosurface;
        isosurface = xNewIsosurface(volumeName, timestep);

        cache[key] = xRetain(isosurface);
    }

    OSPGeometry isosurface;
    isosurface = cache[key];

    OSPData isovalue;
    isovalue = ({
        OSPData data;
        const void *sharedData = isosurfaceValues.data();
        OSPDataType dataType = OSP_FLOAT;
        uint64_t numItems1 = isosurfaceValues.size();
        uint64_t numItems2 = 1;
        uint64_t numItems3 = 1;
        data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

        xCommit(data);
    });
    ospSetObject(isosurface, "isovalue", isovalue);
    ospRelease(isovalue);

    return isosurface;
}

static OSPGeometricModel xNewIsosurfaceModel(
    const std::string &volumeName,
    int timestep,
    const std::vector<float> &isosurfaceValues
) {
    OSPGeometricModel model;
    model = ospNewGeometricModel(nullptr);

    OSPGeometry geometry = ({
        OSPGeometry isosurface;
        isosurface = xGetIsosurface(volumeName, timestep, isosurfaceValues);

        xCommit(isosurface);
    });
    ospSetObject(model, "geometry", geometry);

    return model;
}


static OSPVolumetricModel xNewVolumetricModel(
    const std::string &volumeName,
    int timestep,
    const std::string &colorMapName,
    const std::string &opacityMapName
) {
    OSPVolumetricModel model;
    model = ospNewVolumetricModel(nullptr);

    OSPVolume volume;
    volume = ({
        OSPVolume volume;
        volume = xGetVolume(volumeName, timestep);
        if (volume == nullptr) {
            return nullptr;
        }

        xCommit(volume);
    });
    ospSetObject(model, "volume", volume);
    // ospRelease(volume);

    OSPTransferFunction transferFunction;
    transferFunction = ({
        OSPTransferFunction transferFunction;
        transferFunction = xNewTransferFunction(colorMapName, opacityMapName);
        if (transferFunction == nullptr) {
            return nullptr;
        }

        using Key = std::tuple<std::string, int>;
        Key key{volumeName, timestep};

        std::tuple<float, float> domain;
        std::tie(std::ignore, std::ignore, domain) = volumes[key];

        float lo, hi;
        std::tie(lo, hi) = domain;

        float value[2] = { lo, hi };
        ospSetParam(transferFunction, "value", OSP_BOX1F, value);

        xCommit(transferFunction);
    });
    ospSetObject(model, "transferFunction", transferFunction);
    // ospRelease(transferFunction);

    return model;
}

static void xErrorCallback(void *userData, OSPError error, const char *errorDetails) {
    (void)userData;

    std::fprintf(stderr, "OSPError (%d): %s\n", (int)error, errorDetails);
}

static void xStatusCallback(void *userData, const char *messageText) {
    (void)userData;

    std::fprintf(stderr, "OSPStatus: %s\n", messageText);
}

static OSPWorld xNewWorld(
    const std::string &volumeName,
    int timestep,
    const std::string &colorMapName,
    const std::string &opacityMapName,
    const std::vector<float> &isosurfaceValues
) {
    OSPWorld world;
    world = ospNewWorld();

    OSPInstance instance;
    instance = ({
        OSPInstance instance;
        instance = ospNewInstance(nullptr);

        OSPGroup group;
        group = ({
            OSPGroup group;
            group = ospNewGroup();

            if (isosurfaceValues.empty()) {
                OSPVolumetricModel volume;
                volume = ({
                    OSPVolumetricModel model;
                    model = xNewVolumetricModel(volumeName, timestep, colorMapName, opacityMapName);
                    if (model == nullptr) {
                        return nullptr;
                    }

                    xCommit(model);
                });
                ospSetObjectAsData(group, "volume", OSP_VOLUMETRIC_MODEL, volume);
                // ospRelease(volume);
            
            } else {
                OSPGeometricModel geometry;
                geometry = ({
                    OSPGeometricModel model;
                    model = xNewIsosurfaceModel(volumeName, timestep, isosurfaceValues);

                    OSPMaterial material;
                    material = ({
                        OSPMaterial material;
                        const char *dummy = nullptr;
                        const char *type = "obj";
                        material = ospNewMaterial(dummy, type);

                        xCommit(material);
                    });
                    ospSetObject(model, "material", material);

                    xCommit(model);
                });
                ospSetObjectAsData(group, "geometry", OSP_GEOMETRIC_MODEL, geometry);
                // ospRelease(geometry);

            }

            xCommit(group);
        });
        ospSetObject(instance, "group", group);
        // ospRelease(group);

        xCommit(instance);
    });
    ospSetObjectAsData(world, "instance", OSP_INSTANCE, instance);
    // ospRelease(instance);

    return world;
}

static OSPWorld xGetWorld(
    const std::string &volumeName,
    int timestep,
    const std::string &colorMapName,
    const std::string &opacityMapName,
    const std::vector<float> &isosurfaceValues
) {
    using Key = std::tuple<std::string, int, std::string, std::string, bool>;
    static std::map<Key, OSPWorld> cache;

    Key key{volumeName, timestep, colorMapName, opacityMapName, isosurfaceValues.empty()};
    if (cache.find(key) == cache.end()) {
        OSPWorld world;
        world = xNewWorld(volumeName, timestep, colorMapName, opacityMapName, isosurfaceValues);
        if (world == nullptr) {
            return nullptr;
        }

        cache[key] = xRetain(world);
    }

    return cache[key];
}


static OSPFrameBuffer xNewFrameBuffer(int width, int height) {
    OSPFrameBuffer frameBuffer;
    OSPFrameBufferFormat format = OSP_FB_SRGBA;
    uint32_t channels = OSP_FB_COLOR | OSP_FB_ACCUM;
    frameBuffer = ospNewFrameBuffer(width, height, format, channels);

    return frameBuffer;
}

static OSPFrameBuffer xGetFrameBuffer(int width, int height) {
    using Key = std::tuple<int, int>;
    static std::map<Key, OSPFrameBuffer> cache;
    Key key{width, height};

    if (cache.find(key) == cache.end()) {
        OSPFrameBuffer frameBuffer;
        frameBuffer = xNewFrameBuffer(width, height);

        cache[key] = xRetain(frameBuffer);
    }

    return cache[key];
}

static OSPCamera xNewCamera(
    const std::string &type
) {
    OSPCamera camera;
    camera = ospNewCamera(type.c_str());

    return camera;
}

static OSPCamera xGetCamera(
    const std::string &type,
    float position[3],
    float up[3],
    float direction[3],
    float imageStart[2],
    float imageEnd[2]
) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPCamera> cache;

    Key key = std::make_tuple(type);
    if (cache.find(key) == cache.end()) {
        OSPCamera camera;
        camera = xNewCamera(type);

        ospRetain(camera);
        cache[key] = camera;
    }

    OSPCamera camera;
    camera = cache[key];

    ospSetParam(camera, "position", OSP_VEC3F, position);
    ospSetParam(camera, "up", OSP_VEC3F, up);
    ospSetParam(camera, "direction", OSP_VEC3F, direction);
    ospSetParam(camera, "imageStart", OSP_VEC2F, imageStart);
    ospSetParam(camera, "imageEnd", OSP_VEC2F, imageEnd);

    return camera;
}

static OSPRenderer xNewRenderer(const std::string &type) {
    OSPRenderer renderer;
    renderer = ospNewRenderer(type.c_str());

    int pixelSamples[] = { 2 };
    ospSetParam(renderer, "pixelSamples", OSP_INT, pixelSamples);

    // int maxPathLength[] = { 60 };
    // ospSetParam(renderer, "maxPathLength", OSP_INT, maxPathLength);

    return renderer;
}

static OSPRenderer xGetRenderer(
    const std::string &type,
    float backgroundColor[4]
) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPRenderer> cache;

    Key key{type};
    if (cache.find(key) == cache.end()) {
        OSPRenderer renderer;
        renderer = xNewRenderer(type);

        cache[key] = xRetain(renderer);
    }

    OSPRenderer renderer;
    renderer = cache[key];

    ospSetParam(renderer, "backgroundColor", OSP_VEC4F, backgroundColor);

    return renderer;
}

template <typename T>
T xRead(std::istream &is=std::cin) {
    T x;
    is >> x;
    return x;
}

int main(int argc, const char **argv) {
    OSPError ospInitError = ospInit(&argc, argv);
    if (ospInitError) {
        xDie("Failed to ospInit: %d", ospInitError);
    }

    OSPDevice device;
    device = ({
        OSPDevice device;
        device = ospGetCurrentDevice();

        OSPErrorCallback errorCallback = xErrorCallback;
        void *userData = nullptr;
        ospDeviceSetErrorCallback(device, errorCallback, userData);

        OSPStatusCallback statusCallback = xStatusCallback;
        ospDeviceSetStatusCallback(device, statusCallback, userData);

        ospDeviceCommit(device);
        device;
    });

    (void)device;

    OSPFrameBuffer frameBuffer;
    OSPWorld world;
    OSPRenderer renderer;
    OSPCamera camera;

    std::string key;
    while (std::cin >> key)
    if (0) {

    } else if (key == "world") {
        world = ({
            OSPWorld world;
            auto volumeName = xRead<std::string>();
            auto timestep = xRead<int>();
            auto colorMapName = xRead<std::string>();
            auto opacityMapName = xRead<std::string>();
            std::vector<float> isosurfaceValues(xRead<size_t>());
            for (size_t i=0, n=isosurfaceValues.size(); i<n; ++i) {
                isosurfaceValues[i] = xRead<float>();
            }
            world = xGetWorld(volumeName, timestep, colorMapName, opacityMapName, isosurfaceValues);
            if (world == nullptr) {
                std::fprintf(stderr, "world is null\n");
                continue;
            }

            xCommit(world);
        });

        continue;
    
    } else if (key == "camera") {
        camera = ({
            OSPCamera camera;
            const char *type = "perspective";
            float position[3];
            position[0] = xRead<float>();
            position[1] = xRead<float>();
            position[2] = xRead<float>();
            float up[3];
            up[0] = xRead<float>();
            up[1] = xRead<float>();
            up[2] = xRead<float>();
            float direction[3];
            direction[0] = xRead<float>();
            direction[1] = xRead<float>();
            direction[2] = xRead<float>();
            float imageStart[2];
            imageStart[0] = xRead<float>();  // left
            imageStart[1] = xRead<float>();  // bottom
            float imageEnd[2];
            imageEnd[0] = xRead<float>();  // right
            imageEnd[1] = xRead<float>();  // top
            camera = xGetCamera(type, position, up, direction, imageStart, imageEnd);

            xCommit(camera);
        });

        continue;
    
    } else if (key == "renderer") {
        renderer = ({
            OSPRenderer renderer;
            const char *type = "ao";
            float backgroundColor[4];
            backgroundColor[0] = xRead<int>() / 255.0f;
            backgroundColor[1] = xRead<int>() / 255.0f;
            backgroundColor[2] = xRead<int>() / 255.0f;
            backgroundColor[3] = xRead<int>() / 255.0f;
            renderer = xGetRenderer(type, backgroundColor);

            xCommit(renderer);
        });
        continue;

    } else if (key == "render") {
        auto width = xRead<int>();
        auto height = xRead<int>();
        frameBuffer = ({
            OSPFrameBuffer frameBuffer;
            frameBuffer = xGetFrameBuffer(width, height);

            xCommit(frameBuffer);
        });

        using Clock = std::chrono::steady_clock;

        Clock::time_point beforeRender = Clock::now();
        ospResetAccumulation(frameBuffer);
        ospRenderFrameBlocking(frameBuffer, renderer, camera, world);
        Clock::time_point afterRender = Clock::now();

        Clock::time_point beforeEncode = Clock::now();

        size_t imageLength;
        void *imageData;
        std::tie(imageLength, imageData) = ({
            const void *rgbaOriginal;
            OSPFrameBufferChannel channel = OSP_FB_COLOR;
            rgbaOriginal = ospMapFrameBuffer(frameBuffer, channel);

            std::vector<uint8_t> rgba(static_cast<const uint8_t *>(rgbaOriginal), static_cast<const uint8_t *>(rgbaOriginal) + 4 * width * height);
            // for (int i=0, n=width*height; i<n; ++i) {
            //     float ratio = rgba[4*i+3] / 255.0f;
            //     for (int j=0; j<4; ++j) {
            //         float f = rgba[4*i+j] * ratio + 0.0 * (1.0f - ratio);
            //         uint8_t u = f >= 255.0f ? 255 : f <= 0.0 ? 0 : static_cast<uint8_t>(f);
            //         rgba[4*i+j] = u;
            //     }
            // }

            size_t length;
            static size_t size = 4UL * 1024UL * 1024UL;
            static void *data = std::malloc(size);
            length = xToPNG(rgba.data(), width, height, &size, &data);

            // const char *filename = "out.jpg";
            // xWriteBytes(filename, length, data);
            // std::fprintf(stdout, "Wrote %zu bytes to %s\n", length, filename);

            // stbi_write_png("out.png", 256, 256, 4, rgba, 0);

            ospUnmapFrameBuffer(rgbaOriginal, frameBuffer);

            std::make_tuple(length, data);
        });

        Clock::time_point afterEncode = Clock::now();

        using TimeUnit = std::chrono::microseconds;
        size_t renderDuration = std::chrono::duration_cast<TimeUnit>(afterRender - beforeRender).count();
        size_t encodeDuration = std::chrono::duration_cast<TimeUnit>(afterEncode - beforeEncode).count();

        std::cout.write(reinterpret_cast<const char *>(&renderDuration), sizeof(renderDuration));
        std::cout.write(reinterpret_cast<const char *>(&encodeDuration), sizeof(encodeDuration));
        std::cout.write(reinterpret_cast<const char *>(&imageLength), sizeof(imageLength));
        std::cout.write(static_cast<const char *>(imageData), imageLength);
        std::cout.flush();
    
    } else {
        std::fprintf(stderr, "Unknown key: %s\n", key.c_str());
        continue;

    }

    return 0;
}
