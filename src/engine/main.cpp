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

static size_t xToJPG(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
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

static void xWriteBytes(const std::string &filename, size_t size, void *data) {
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
    data = new std::byte[nbyte];
    std::size_t nread = std::fread(data, sizeof(std::byte), nbyte, file);
    if (nread < nbyte) xDie("Failed to read everything: %zu < %zu", nread, nbyte);

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
    data = ospNewSharedData(sharedData, dataType, numItems1, byteStride1, numItems2, byteStride2, numItems3, byteStride3);

    return xCommit(data);
}


static OSPVolume xNewVolume(const std::string &filename, OSPDataType dataType_, uint64_t d1, uint64_t d2, uint64_t d3) {
    OSPVolume volume;
    const char *type = "structuredRegular";
    volume = ospNewVolume(type);

    OSPData data;
    data = ({
        OSPData data;
        const void *sharedData = xReadBytes(filename);
        OSPDataType dataType = dataType_;
        uint64_t numItems1 = d1;
        uint64_t numItems2 = d2;
        uint64_t numItems3 = d3;
        data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);
    
        xCommit(data);
    });
    ospSetObject(volume, "data", data);

    float gridOrigin[3] = { -0.5f*d1, -0.5f*d2, -0.5f*d3 };
    ospSetParam(volume, "gridOrigin", OSP_VEC3F, gridOrigin);

    // float gridSpacing[3] = { 1.0f/d1, 1.0f/d2, 1.0f/d3 };
    // ospSetParam(volume, "gridSpacing", OSP_VEC3F, gridSpacing);

    // int cellCentered = 0;
    // ospSetParam(volume, "cellCentered", OSP_BOOL, &cellCentered);

    return volume;
}

static std::map<std::string, std::vector<std::tuple<float, float, float>>> colorMaps{
#   include "detail/colormaps.h"
};

static OSPData xNewColorMap(const std::string &name) {
    static std::map<std::string, OSPData> loadedMaps;

    if (loadedMaps.find(name) != loadedMaps.end()) {
        return loadedMaps[name];
    }

    if (!(colorMaps.find(name) != colorMaps.end())) {
        std::fprintf(stderr, "ERROR: Colormap not found: %s\n", name.c_str());
        return nullptr;
    }

    OSPData data;
    const void *sharedData = colorMaps[name].data();
    OSPDataType dataType = OSP_VEC3F;
    uint64_t numItems1 = colorMaps[name].size();
    uint64_t numItems2 = 1;
    uint64_t numItems3 = 1;
    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

    ospRetain(data);
    loadedMaps[name] = data;

    return data;
}

static std::map<std::string, std::vector<float>> opacityMaps{
#   include "detail/opacitymaps.h"
};

static OSPData xNewOpacityMap(const std::string &name) {
    static std::map<std::string, OSPData> loadedMaps;

    if (loadedMaps.find(name) != loadedMaps.end()) {
        return loadedMaps[name];
    }

    if (!(opacityMaps.find(name) != opacityMaps.end())) {
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
    ospRetain(data);

    loadedMaps[name] = data;

    return data;
}

static OSPTransferFunction xNewTransferFunction(const std::string &colorName, const std::string &opacityName) {
    OSPTransferFunction transferFunction;
    const char *type = "piecewiseLinear";
    transferFunction = ospNewTransferFunction(type);

    OSPData color;
    color = ({
        OSPData data;
        data = xNewColorMap(colorName);
        if (data == nullptr) {
            return nullptr;
        }
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "color", color);

    OSPData opacity;
    opacity = ({
        OSPData data;
        data = xNewOpacityMap(opacityName);
        if (data == nullptr) {
            return nullptr;
        }
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "opacity", opacity);

    return transferFunction;
}

static OSPVolume xGetVolume(const std::string &name) {
    OSPVolume volume;

    if (name == "supernova") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/E_1335.dat";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 432;
            uint64_t d2 = 432;
            uint64_t d3 = 432;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;

    } else if (name == "magnetic") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/magnetic-512-volume.raw";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 512;
            uint64_t d2 = 512;
            uint64_t d3 = 512;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;

    } else if (name == "teapot") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/teapot.raw";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 256;
            uint64_t d2 = 256;
            uint64_t d3 = 178;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;

    } else if (name == "tornado") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/interp8536.nc";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 280;
            uint64_t d2 = 490;
            uint64_t d3 = 490;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;

    } else if (name == "turbine") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/turbine_VMIN_EPS1.7_minPts40_X1589_Y698_Z1799_Full.raw";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 1589;
            uint64_t d2 = 698;
            uint64_t d3 = 1799;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;

    } else if (name == "turbulence") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/tacc-turbulence-256-volume.raw";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 256;
            uint64_t d2 = 256;
            uint64_t d3 = 256;
            volume = xNewVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;
    
    } else {
        std::fprintf(stderr, "ERROR: Unknown volume: %s\n", name.c_str());
        volume = nullptr;

    }

    return volume;
}

static OSPGeometry xNewIsosurface(
    const std::string &volumeName
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
        volume = xGetVolume(volumeName);

        xCommit(volume);
    });
    ospSetObject(isosurface, "volume", volume);

    return isosurface;
}

static OSPGeometry xGetIsosurface(
    const std::string &volumeName,
    const std::vector<float> &isosurfaceValues
) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPGeometry> cache;
    Key key{volumeName};

    if (cache.find(key) == cache.end()) {
        OSPGeometry isosurface;
        isosurface = xNewIsosurface(volumeName);

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
    const std::vector<float> &isosurfaceValues
) {
    OSPGeometricModel model;
    model = ospNewGeometricModel(nullptr);

    OSPGeometry geometry = ({
        OSPGeometry isosurface;
        isosurface = xGetIsosurface(volumeName, isosurfaceValues);

        xCommit(isosurface);
    });
    ospSetObjectAsData(model, "geometry", OSP_GEOMETRY, geometry);

    return model;
}


static OSPVolumetricModel xNewVolumetricModel(
    const std::string &volumeName,
    const std::string &colorMapName,
    const std::string &opacityMapName
) {
    OSPVolumetricModel model;
    model = ospNewVolumetricModel(nullptr);

    OSPVolume volume;
    volume = ({
        OSPVolume volume;
        volume = xGetVolume(volumeName);
        if (volume == nullptr) {
            return nullptr;
        }

        xCommit(volume);
    });
    ospSetObject(model, "volume", volume);
    ospRelease(volume);

    OSPTransferFunction transferFunction;
    transferFunction = ({
        OSPTransferFunction transferFunction;
        transferFunction = xNewTransferFunction(colorMapName, opacityMapName);
        if (transferFunction == nullptr) {
            return nullptr;
        }

        float value[2] = { 0.0f, 0.130518f };
        ospSetParam(transferFunction, "value", OSP_BOX1F, value);

        xCommit(transferFunction);
    });
    ospSetObject(model, "transferFunction", transferFunction);
    ospRelease(transferFunction);

    return model;
}

static void xErrorCallback(void *userData, OSPError error, const char *errorDetails) {
    std::fprintf(stderr, "OSPError (%d): %s\n", (int)error, errorDetails);
}

static void xStatusCallback(void *userData, const char *messageText) {
    std::fprintf(stderr, "OSPStatus: %s\n", messageText);
}

static OSPWorld xNewWorld(
    const std::string &volumeName,
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
                    model = xNewVolumetricModel(volumeName, colorMapName, opacityMapName);
                    if (model == nullptr) {
                        return nullptr;
                    }

                    xCommit(model);
                });
                ospSetObjectAsData(group, "volume", OSP_VOLUMETRIC_MODEL, volume);
                ospRelease(volume);
            
            } else {
                OSPGeometricModel geometry;
                geometry = ({
                    OSPGeometricModel model;
                    model = xNewIsosurfaceModel(volumeName, isosurfaceValues);

                    xCommit(model);
                });
                ospSetObjectAsData(group, "geometry", OSP_GEOMETRIC_MODEL, geometry);
                ospRelease(geometry);

            }

            xCommit(group);
        });
        ospSetObject(instance, "group", group);
        ospRelease(group);

        xCommit(instance);
    });
    ospSetObjectAsData(world, "instance", OSP_INSTANCE, instance);
    ospRelease(instance);

    return world;
}

static OSPWorld xGetWorld(
    const std::string &volumeName,
    const std::string &colorMapName,
    const std::string &opacityMapName,
    const std::vector<float> &isosurfaceValues
) {
    using Key = std::tuple<std::string, std::string, std::string, bool>;
    static std::map<Key, OSPWorld> cache;

    Key key{volumeName, colorMapName, opacityMapName, isosurfaceValues.empty()};
    if (cache.find(key) == cache.end()) {
        OSPWorld world;
        world = xNewWorld(volumeName, colorMapName, opacityMapName, isosurfaceValues);
        if (world == nullptr) {
            return nullptr;
        }

        ospRetain(world);
        cache[key] = world;
    }

    return cache[key];
}


static OSPFrameBuffer xNewFrameBuffer(int width, int height) {
    OSPFrameBuffer frameBuffer;
    OSPFrameBufferFormat format = OSP_FB_RGBA8;
    uint32_t channels = OSP_FB_COLOR;
    frameBuffer = ospNewFrameBuffer(width, height, format, channels);

    return frameBuffer;
}

static OSPFrameBuffer xGetFrameBuffer(int width, int height) {
    using Key = std::tuple<int, int>;
    static std::map<Key, OSPFrameBuffer> cache;
    Key key = std::make_tuple(width, height);

    if (cache.find(key) == cache.end()) {
        OSPFrameBuffer frameBuffer;
        frameBuffer = xNewFrameBuffer(width, height);
        ospRetain(frameBuffer);
        cache[key] = frameBuffer;
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

    return renderer;
}

static OSPRenderer xGetRenderer(
    const std::string &type,
    float backgroundColor[4]
) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPRenderer> cache;

    Key key = std::make_tuple(type);
    if (cache.find(key) == cache.end()) {
        OSPRenderer renderer;
        renderer = xNewRenderer(type);

        ospRetain(renderer);
        cache[key] = renderer;
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
            auto colorMapName = xRead<std::string>();
            auto opacityMapName = xRead<std::string>();
            std::vector<float> isosurfaceValues(xRead<size_t>());
            for (size_t i=0, n=isosurfaceValues.size(); i<n; ++i) {
                isosurfaceValues[i] = xRead<float>();
            }
            world = xGetWorld(volumeName, colorMapName, opacityMapName, isosurfaceValues);
            if (world == nullptr) {
                continue;
            }

            xCommit(world);
        });

        continue;
    
    } else if (key == "camera") {
        camera = ({
            OSPCamera camera;
            const char *type = "perspective";
            float position[3] = {
                xRead<float>(),
                xRead<float>(),
                xRead<float>(),
            };
            float up[3] = {
                xRead<float>(),
                xRead<float>(),
                xRead<float>(),
            };
            float direction[3] = {
                xRead<float>(),
                xRead<float>(),
                xRead<float>(),
            };
            float imageStart[2] = {
                xRead<float>(),  // left
                xRead<float>(),  // bottom
            };
            float imageEnd[2] = {
                xRead<float>(),  // right
                xRead<float>(),  // top
            };
            camera = xGetCamera(type, position, up, direction, imageStart, imageEnd);

            xCommit(camera);
        });

        continue;
    
    } else if (key == "renderer") {
        renderer = ({
            OSPRenderer renderer;
            const char *type = "ao";
            float backgroundColor[4] = {
                xRead<int>() / 255.0f,
                xRead<int>() / 255.0f,
                xRead<int>() / 255.0f,
                xRead<int>() / 255.0f,
            };
            renderer = xGetRenderer(type, backgroundColor);

            xCommit(renderer);
        });
        continue;

    } else if (key == "render") {
        int width;
        int height;
        std::tie(frameBuffer, width, height) = ({
            OSPFrameBuffer frameBuffer;
            auto width = xRead<int>();
            auto height = xRead<int>();
            frameBuffer = xGetFrameBuffer(width, height);

            std::make_tuple(xCommit(frameBuffer), width, height);
        });

        using Clock = std::chrono::steady_clock;

        Clock::time_point beforeRender = Clock::now();
        ospRenderFrameBlocking(frameBuffer, renderer, camera, world);
        Clock::time_point afterRender = Clock::now();

        Clock::time_point beforeEncode = Clock::now();

        size_t imageLength;
        void *imageData;
        std::tie(imageLength, imageData) = ({
            const void *rgba;
            OSPFrameBufferChannel channel = OSP_FB_COLOR;
            rgba = ospMapFrameBuffer(frameBuffer, channel);

            size_t length;
            static size_t size = 0;
            static void *data = nullptr;
            length = xToJPG(rgba, width, height, &size, &data);

            // const char *filename = "out.jpg";
            // xWriteBytes(filename, length, data);
            // std::fprintf(stdout, "Wrote %zu bytes to %s\n", length, filename);

            // stbi_write_png("out.png", 256, 256, 4, rgba, 0);

            ospUnmapFrameBuffer(rgba, frameBuffer);

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
