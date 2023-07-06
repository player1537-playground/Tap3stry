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

static OSPData xNewSharedData(const void *sharedData, OSPDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3) {
    OSPData data;
    int64_t byteStride1 = 0;
    int64_t byteStride2 = 0;
    int64_t byteStride3 = 0;
    data = ospNewSharedData(sharedData, dataType, numItems1, byteStride1, numItems2, byteStride2, numItems3, byteStride3);

    return xCommit(data);
}


static OSPVolume xNewCenteredVolume(const std::string &filename, OSPDataType dataType_, uint64_t d1, uint64_t d2, uint64_t d3) {
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

    float gridOrigin[3] = { -0.5f, -0.5f, -0.5f };
    ospSetParam(volume, "gridOrigin", OSP_VEC3F, gridOrigin);

    float gridSpacing[3] = { 1.0f/d1, 1.0f/d2, 1.0f/d3 };
    ospSetParam(volume, "gridSpacing", OSP_VEC3F, gridSpacing);

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
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "color", color);

    OSPData opacity;
    opacity = ({
        OSPData data;
        data = xNewOpacityMap(opacityName);
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "opacity", opacity);

    return transferFunction;
}

static OSPVolume xNewVolume(const std::string &name) {
    OSPVolume volume;

    if (name == "supernova") {
        static OSPVolume cache = ({
            OSPVolume volume;
            const char *filename = "/mnt/seenas2/data/standalone/data/E_1335.dat";
            OSPDataType dataType = OSP_FLOAT;
            uint64_t d1 = 432;
            uint64_t d2 = 432;
            uint64_t d3 = 432;
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

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
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

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
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

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
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

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
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

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
            volume = xNewCenteredVolume(filename, dataType, d1, d2, d3);

            ospRetain(volume);

            volume;
        });

        volume = cache;
    
    } else {
        std::fprintf(stderr, "ERROR: Unknown volume: %s\n", name.c_str());

    }

    return volume;
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
        volume = xNewVolume(volumeName);

        xCommit(volume);
    });
    ospSetObject(model, "volume", volume);
    ospRelease(volume);

    OSPTransferFunction transferFunction;
    transferFunction = ({
        OSPTransferFunction transferFunction;
        transferFunction = xNewTransferFunction(colorMapName, opacityMapName);

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
    const std::string &opacityMapName
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

            OSPVolumetricModel volume;
            volume = ({
                OSPVolumetricModel model;
                model = xNewVolumetricModel(volumeName, colorMapName, opacityMapName);

                xCommit(model);
            });
            ospSetObjectAsData(group, "volume", OSP_VOLUMETRIC_MODEL, volume);
            ospRelease(volume);

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

static OSPFrameBuffer xNewFrameBuffer(int width, int height) {
    static std::map<std::tuple<int, int>, OSPFrameBuffer> cache;
    auto key = std::make_tuple(width, height);

    if (cache.find(key) != cache.end()) {
        return cache[key];
    }

    OSPFrameBuffer frameBuffer;
    OSPFrameBufferFormat format = OSP_FB_RGBA8;
    uint32_t channels = OSP_FB_COLOR;
    frameBuffer = ospNewFrameBuffer(width, height, format, channels);

    ospRetain(frameBuffer);
    cache[key] = frameBuffer;

    return frameBuffer;
}

static OSPCamera xNewCamera(
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
        camera = ospNewCamera(type.c_str());

        ospRetain(camera);
        cache[key] = camera;
    }

    OSPCamera camera;
    camera = cache[key];

    float fovy[1] = { 90.0f };
    ospSetParam(camera, "fovy", OSP_FLOAT, fovy);

    ospSetParam(camera, "position", OSP_VEC3F, position);

    ospSetParam(camera, "up", OSP_VEC3F, up);

    ospSetParam(camera, "direction", OSP_VEC3F, direction);

    // float imageStart[2] = {
    //     xRead<float>(),  // left
    //     xRead<float>(),  // bottom
    //     // (regionCol + 0.0f) / regionColCount,  // left
    //     // 1.0f - (regionRow + 0.0f) / regionRowCount,  // bottom
    // };
    ospSetParam(camera, "imageStart", OSP_VEC2F, imageStart);

    // float imageEnd[2] = {
    //     xRead<float>(),  // right
    //     xRead<float>(),  // top
    //     // (regionCol + 1.0f) / regionColCount,  // right
    //     // 1.0f - (regionRow + 1.0f) / regionRowCount,  // top
    // };
    ospSetParam(camera, "imageEnd", OSP_VEC2F, imageEnd);
    
    return camera;
}

static OSPRenderer xNewRenderer(const std::string &type, float backgroundColor[4]) {
    using Key = std::tuple<std::string>;
    static std::map<Key, OSPRenderer> cache;

    Key key = std::make_tuple(type);
    if (cache.find(key) == cache.end()) {
        OSPRenderer renderer;
        renderer = ospNewRenderer(type.c_str());

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
            world = xNewWorld(volumeName, colorMapName, opacityMapName);

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
            camera = xNewCamera(type, position, up, direction, imageStart, imageEnd);

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
            renderer = xNewRenderer(type, backgroundColor);

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
            frameBuffer = xNewFrameBuffer(width, height);

            std::make_tuple(xCommit(frameBuffer), width, height);
        });

        ospRenderFrameBlocking(frameBuffer, renderer, camera, world);

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

        std::cout.write(reinterpret_cast<const char *>(&imageLength), sizeof(imageLength));
        std::cout.write(static_cast<const char *>(imageData), imageLength);
        std::cout.flush();
    
    } else {
        std::fprintf(stderr, "Unknown key: %s\n", key.c_str());
        continue;

    }

    return 0;
}
