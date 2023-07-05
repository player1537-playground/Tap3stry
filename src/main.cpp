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

static void dief(const char *fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "Error: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    std::exit(EXIT_FAILURE);
}

static void die(const char *msg) {
    return dief("%s", msg);
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

static size_t xWriteJPG(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
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
    if (!success) die("Failed to stbi_write_jpg_to_func");

    return context.offset;
}

static size_t xWritePNG(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
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
    if (!success) die("Failed to stbi_write_png_to_func");

    return context.offset;
}

static void xWriteBytes(const std::string &filename, size_t size, void *data) {
    std::FILE *file;
    file = fopen(filename.c_str(), "wb");
    if (!file) dief("Failed to fopen: %s", filename.c_str());

    std::size_t nwrite;
    nwrite = std::fwrite(data, 1, size, file);
    if (nwrite < size) dief("Failed to write all: %zu < %zu", nwrite, size);

    std::fclose(file);
}

static void *xReadBytes(const std::string &filename) {
    std::FILE *file;
    file = fopen(filename.c_str(), "rb");
    if (!file) dief("Failed to fopen: %s", filename.c_str());

    {
        int rv = std::fseek(file, 0, SEEK_END);
        if (rv) dief("Failed to fseek: %s", filename.c_str());
    }

    long nbyte;
    nbyte = std::ftell(file);
    if (nbyte < 0) dief("Failed to ftell: %s", filename.c_str());

    {
        int rv = std::fseek(file, 0, SEEK_SET);
        if (rv) dief("Failed to fseek: %s", filename.c_str());
    }

    void *data;
    data = new std::byte[nbyte];
    std::size_t nread = std::fread(data, sizeof(std::byte), nbyte, file);
    if (nread < nbyte) dief("Failed to read everything: %zu < %zu", nread, nbyte);

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
#   include "colormaps.h"
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
#   include "opacitymaps.h"
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
        static OSPVolume supernova = ({
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

        volume = supernova;
    
    } else {
        std::fprintf(stderr, "ERROR: Unknown volume: %s\n", name.c_str());

    }

    return volume;
}

static OSPVolumetricModel xNewVolumetricModel(const std::string &volumeName, const std::string &colorMapName, const std::string &opacityMapName) {
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


int main(int argc, const char **argv) {
    OSPError ospInitError = ospInit(&argc, argv);
    if (ospInitError) {
        dief("Failed to ospInit: %d", ospInitError);
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

    for (;;) {
        int imageResolution = 256;
        std::string volumeName = "supernova";
        std::string colorMapName = "viridis";
        std::string opacityMapName = "reverseRamp";
        float cameraPositionX = 1.0f;
        float cameraPositionY = 0.0f;
        float cameraPositionZ = 1.0f;
        float cameraUpX = 0.0f;
        float cameraUpY = 1.0f;
        float cameraUpZ = 0.0f;
        float cameraDirectionX = -1.0f;
        float cameraDirectionY =  0.0f;
        float cameraDirectionZ = -1.0f;
        int backgroundColorR = 0;
        int backgroundColorG = 0;
        int backgroundColorB = 0;
        int backgroundColorA = 0;
        int regionRow = 0;
        int regionRowCount = 1;
        int regionCol = 0;
        int regionColCount = 1;

        std::cin
            >> imageResolution
            >> volumeName
            >> colorMapName
            >> opacityMapName
            >> cameraPositionX >> cameraPositionY >> cameraPositionZ
            >> cameraUpX >> cameraUpY >> cameraUpZ
            >> cameraDirectionX >> cameraDirectionY >> cameraDirectionZ
            >> backgroundColorR >> backgroundColorG >> backgroundColorB >> backgroundColorA
            >> regionRow >> regionRowCount
            >> regionCol >> regionColCount
            ;

        OSPFrameBuffer frameBuffer;
        frameBuffer = ({
            OSPFrameBuffer frameBuffer;
            int size_x = imageResolution;
            int size_y = imageResolution;
            OSPFrameBufferFormat format = OSP_FB_RGBA8;
            uint32_t channels = OSP_FB_COLOR;
            frameBuffer = ospNewFrameBuffer(size_x, size_y, format, channels);

            xCommit(frameBuffer);
        });

        OSPRenderer renderer;
        renderer = ({
            OSPRenderer renderer;
            const char *type = "ao";
            renderer = ospNewRenderer(type);

            float backgroundColor[4] = {
                backgroundColorR / 255.0f,
                backgroundColorG / 255.0f,
                backgroundColorB / 255.0f,
                backgroundColorA / 255.0f,
            };
            ospSetParam(renderer, "backgroundColor", OSP_VEC4F, backgroundColor);

            xCommit(renderer);
        });

        OSPCamera camera;
        camera = ({
            OSPCamera camera;
            const char *type = "perspective";
            camera = ospNewCamera(type);

            float fovy[1] = { 90.0f };
            ospSetParam(camera, "fovy", OSP_FLOAT, fovy);

            float position[3] = {
                cameraPositionX,
                cameraPositionY,
                cameraPositionZ,
            };
            ospSetParam(camera, "position", OSP_VEC3F, position);

            float up[3] = {
                cameraUpX,
                cameraUpY,
                cameraUpZ,
            };
            ospSetParam(camera, "up", OSP_VEC3F, up);

            float direction[3] = {
                cameraDirectionX,
                cameraDirectionY,
                cameraDirectionZ,
            };
            ospSetParam(camera, "direction", OSP_VEC3F, direction);

            float imageStart[2] = {
                (regionCol + 0.0f) / regionColCount,  // left
                (regionRow + 1.0f) / regionRowCount,  // bottom
            };
            ospSetParam(camera, "imageStart", OSP_VEC2F, imageStart);

            float imageEnd[2] = {
                (regionCol + 1.0f) / regionColCount,  // right
                (regionRow + 0.0f) / regionRowCount,  // top
            };
            ospSetParam(camera, "imageEnd", OSP_VEC2F, imageEnd);

            xCommit(camera);
        });

        OSPWorld world;
        world = ({
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

            // OSPLight light;
            // light = ({
            //     OSPLight light;
            //     const char *type = "ambient";
            //     light = ospNewLight(type);

            //     float color[3] = { 1.0f, 1.0f, 1.0f };
            //     ospSetParam(light, "color", OSP_VEC3F, color);

            //     float intensity[1] = { 100.0f };
            //     ospSetParam(light, "intensity", OSP_FLOAT, intensity);

            //     xCommit(light);
            // });
            // ospSetObjectAsData(world, "light", OSP_LIGHT, light);

            xCommit(world);
        });

        ospRenderFrameBlocking(frameBuffer, renderer, camera, world);

        size_t imageLength;
        void *imageData;
        std::tie(imageLength, imageData) = ({
            const void *rgba;
            OSPFrameBufferChannel channel = OSP_FB_COLOR;
            rgba = ospMapFrameBuffer(frameBuffer, channel);

            size_t length;
            int width = imageResolution;
            int height = imageResolution;
            static size_t size = 0;
            static void *data = nullptr;
            length = xWriteJPG(rgba, width, height, &size, &data);

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
    }

    return 0;
}
