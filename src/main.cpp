//std
#include <cstdarg> // std::va_list, va_start, va_end
#include <cstdlib> // std::exit
#include <cstdio> // std::fprintf, std::vfprintf, std::fopen, std::ftell, std::fseek, std::fclose, stderr
#include <cstring> // std::memcpy
#include <string> // std::string
#include <vector> // std::vector
#include <tuple> // std::make_tuple, std::tie

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

struct xjpegContext {
    size_t offset;
    size_t *size;
    void **data;
};

static void xjpegCallback(void *context_, void *data, int size) {
    xjpegContext *context = static_cast<xjpegContext *>(context_);

    if (context->offset + size > *context->size) {
        *context->size *= 2;
        *context->data = std::realloc(*context->data, *context->size);
    }

    void *dest = static_cast<std::byte *>(*context->data) + context->offset;
    const void *src = data;
    std::size_t count = size;
    std::memcpy(dest, src, count);

    context->offset += size;
}

static void xjpeg(const void *rgba, int width, int height, size_t *outsize, void **outdata) {
    if (*outsize == 0) {
        *outsize = 1024;
        *outdata = std::malloc(*outsize);
    }

    xjpegContext context;
    context.offset = 0;
    context.size = outsize;
    context.data = outdata;

    int success;
    stbi_write_func *func = xjpegCallback;
    void *context_ = &context;
    int w = width;
    int h = height;
    int comp = 4;
    const void *data = rgba;
    int quality = 95;
    success = stbi_write_jpg_to_func(func, context_, w, h, comp, data, quality);
    if (!success) die("Failed to stbi_write_jpg_to_func");
}

static void xwrite(const std::string &filename, size_t size, void *data) {
    std::FILE *file;
    file = fopen(filename.c_str(), "wb");
    if (!file) dief("Failed to fopen: %s", filename.c_str());

    std::size_t nwrite;
    nwrite = std::fwrite(data, 1, size, file);
    if (nwrite < size) dief("Failed to write all: %zu < %zu", nwrite, size);

    std::fclose(file);
}

static void *xread(const std::string &filename) {
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

    std::fprintf(stderr, "Read %zu bytes from %s\n", nbyte, filename.c_str());

    return data;
}

template <class T>
static T xCommit(T& t) {
    ospCommit(t);
    return t;
}

static OSPData xNewSharedData(const void *sharedData, OSPDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3) {
    std::fprintf(stderr, "xNewSharedData(%p, %zu, %zu, %zu)\n", sharedData, numItems1, numItems2, numItems3);

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
        const void *sharedData = xread(filename);
        OSPDataType dataType = dataType_;
        uint64_t numItems1 = d1;
        uint64_t numItems2 = d2;
        uint64_t numItems3 = d3;
        data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);
    
        xCommit(data);
    });
    ospSetObject(volume, "data", data);

    float gridOrigin[3] = { -0.5f, -0.5f, -0.5f };
    std::fprintf(stderr, "gridOrigin = %f %f %f\n", gridOrigin[0], gridOrigin[1], gridOrigin[2]);
    ospSetParam(volume, "gridOrigin", OSP_VEC3F, gridOrigin);

    float gridSpacing[3] = { 1.0f/d1, 1.0f/d2, 1.0f/d3 };
    std::fprintf(stderr, "gridSpacing = %f %f %f\n", gridSpacing[0], gridSpacing[1], gridSpacing[2]);
    ospSetParam(volume, "gridSpacing", OSP_VEC3F, gridSpacing);

    // int cellCentered = 0;
    // ospSetParam(volume, "cellCentered", OSP_BOOL, &cellCentered);

    return volume;
}

static OSPData xNewNamedColorMap(const std::string &name) {
    static float viridis[][3] = {
        { 0.267f, 0.005f, 0.329f },
        { 0.279f, 0.175f, 0.483f },
        { 0.230f, 0.322f, 0.546f },
        { 0.173f, 0.449f, 0.558f },
        { 0.128f, 0.567f, 0.551f },
        { 0.158f, 0.684f, 0.502f },
        { 0.369f, 0.789f, 0.383f },
        { 0.678f, 0.864f, 0.190f },
        { 0.993f, 0.906f, 0.144f },
    };

    static float greyscale[][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
    };

    OSPData data;
    const void *sharedData =
        name == "viridis" ? viridis :
        name == "greyscale" ? greyscale :
        nullptr;
    OSPDataType dataType = OSP_VEC3F;
    uint64_t numItems1 =
        name == "viridis" ? sizeof(viridis)/sizeof(*viridis) :
        name == "greyscale" ? sizeof(greyscale)/sizeof(*greyscale) :
        0;
    uint64_t numItems2 = 1;
    uint64_t numItems3 = 1;
    std::fprintf(stderr, "sharedData=%p numItems=%zu\n", sharedData, numItems1);
    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

    return data;
}

static OSPData xNewNamedOpacityMap(const std::string &name) {
    static float ramp[][1] = {
        { 0.0f },
        { 1.0f },
    };

    OSPData data;
    const void *sharedData =
        name == "ramp" ? ramp :
        nullptr;
    OSPDataType dataType = OSP_FLOAT;
    uint64_t numItems1 =
        name == "ramp" ? sizeof(ramp)/sizeof(*ramp) :
        0;
    uint64_t numItems2 = 1;
    uint64_t numItems3 = 1;
    std::fprintf(stderr, "sharedData=%p numItems=%zu\n", sharedData, numItems1);
    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

    return data;
}

static OSPTransferFunction xNewTransferFunction(const std::string &colorName, const std::string &opacityName) {
    OSPTransferFunction transferFunction;
    const char *type = "piecewiseLinear";
    transferFunction = ospNewTransferFunction(type);

    OSPData color;
    color = ({
        OSPData data;
        data = xNewNamedColorMap(colorName);
        
        xCommit(data);
    });
    ospSetObject(transferFunction, "color", color);

    OSPData opacity;
    opacity = ({
        OSPData data;
        data = xNewNamedOpacityMap(opacityName);
        
        xCommit(data);
    });
    std::fprintf(stderr, "opacity=%p\n", (void *)opacity);
    ospSetParam(transferFunction, "opacity", OSP_DATA, &opacity);

    return transferFunction;
}

void xErrorCallback(void *userData, OSPError error, const char *errorDetails) {
    std::fprintf(stderr, "OSPError (%d): %s\n", (int)error, errorDetails);
}

void xStatusCallback(void *userData, const char *messageText) {
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

        device;
    });

    OSPFrameBuffer frameBuffer;
    frameBuffer = ({
        OSPFrameBuffer frameBuffer;
        int size_x = 256;
        int size_y = 256;
        OSPFrameBufferFormat format = OSP_FB_SRGBA;
        uint32_t channels = OSP_FB_COLOR;
        frameBuffer = ospNewFrameBuffer(size_x, size_y, format, channels);

        xCommit(frameBuffer);
    });

    OSPRenderer renderer;
    renderer = ({
        OSPRenderer renderer;
        const char *type = "ao";
        renderer = ospNewRenderer(type);

        float backgroundColor[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        ospSetParam(renderer, "backgroundColor", OSP_VEC4F, backgroundColor);

        xCommit(renderer);
    });

    OSPCamera camera;
    camera = ({
        OSPCamera camera;
        const char *type = "perspective";
        camera = ospNewCamera(type);

        float fovy[1] = { 180.0f };
        ospSetParam(camera, "fovy", OSP_FLOAT, fovy);

        float position[3] = { 0.0f, 0.0f, 2.0f };
        ospSetParam(camera, "position", OSP_VEC3F, position);

        float up[3] = { 0.0f, 1.0f, 0.0f };
        ospSetParam(camera, "up", OSP_VEC3F, up);

        float direction[3] = { 0.0f, 0.0f, -1.0f };
        ospSetParam(camera, "direction", OSP_VEC3F, direction);

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

                std::vector<OSPVolumetricModel> *volModels = new std::vector<OSPVolumetricModel>();
                volModels->push_back(({
                    OSPVolumetricModel volModel;
                    volModel = ospNewVolumetricModel(nullptr);

                    OSPVolume volume;
                    volume = ({
                        OSPVolume volume;
                        const char *filename = "/mnt/seenas2/data/standalone/data/E_1335.dat";
                        OSPDataType dataType = OSP_FLOAT;
                        uint64_t d1 = 432;
                        uint64_t d2 = 432;
                        uint64_t d3 = 432;
                        volume = xNewVolume(filename, dataType, d1, d2, d3);

                        xCommit(volume);
                    });
                    ospSetObject(volModel, "volume", volume);

                    OSPTransferFunction transferFunction;
                    transferFunction = ({
                        OSPTransferFunction transferFunction;
                        const char *colorName = "greyscale";
                        const char *opacityName = "ramp";
                        transferFunction = xNewTransferFunction(colorName, opacityName);

                        float value[2] = { 0.0f, 255.0f };
                        ospSetParam(transferFunction, "value", OSP_BOX1F, value);

                        xCommit(transferFunction);
                    });
                    ospSetObject(volModel, "transferFunction", transferFunction);

                    xCommit(volModel);
                }));

                OSPData volume;
                volume = ({
                    OSPData data;
                    const void *sharedData = volModels->data();
                    OSPDataType dataType = OSP_VOLUMETRIC_MODEL;
                    uint64_t numItems1 = volModels->size();
                    uint64_t numItems2 = 1;
                    uint64_t numItems3 = 1;
                    data = xNewSharedData(sharedData, dataType, numItems1, numItems2, numItems3);

                    xCommit(data);
                });
                ospSetObject(group, "volume", volume);

                xCommit(group);
            });
            ospSetObject(instance, "group", group);

            xCommit(instance);
        });
        ospSetObjectAsData(world, "instance", OSP_INSTANCE, instance);

        OSPLight light;
        light = ({
            OSPLight light;
            const char *type = "ambient";
            light = ospNewLight(type);

            float color[3] = { 1.0f, 1.0f, 1.0f };
            ospSetParam(light, "color", OSP_VEC3F, color);

            float intensity = 100.0f;
            ospSetParam(light, "intensity", OSP_FLOAT, &intensity);

            xCommit(light);
        });
        ospSetObjectAsData(world, "light", OSP_LIGHT, light);

        xCommit(world);
    });

    ospRenderFrameBlocking(frameBuffer, renderer, camera, world);

    size_t jpegsize;
    void *jpegdata;
    std::tie(jpegsize, jpegdata) = ({
        const void *rgba;
        OSPFrameBufferChannel channel = OSP_FB_COLOR;
        rgba = ospMapFrameBuffer(frameBuffer, channel);

        int width = 256;
        int height = 256;
        size_t size = 0;
        void *data = nullptr;
        xjpeg(rgba, width, height, &size, &data);

        ospUnmapFrameBuffer(rgba, frameBuffer);
        
        std::make_tuple(size, data);
    });

    ({
        auto bounds = ospGetBounds(world);
        std::fprintf(stderr, "ospGetBounds(world) = { lower = { %f, %f, %f }, upper = { %f, %f, %f } }\n",
            bounds.lower[0],
            bounds.lower[1],
            bounds.lower[2],
            bounds.upper[0],
            bounds.upper[1],
            bounds.upper[2]);
    });

    xwrite("out.jpg", jpegsize, jpegdata);
    std::fprintf(stdout, "Wrote %zu bytes to %s\n", jpegsize, "out.jpg");

    ospShutdown();

    std::fprintf(stdout, "done shutting down\n");

    return 0;
}
