/*
    ASIO - Audio Stream Input/Output
    SDK Version 2.3
    (c) Steinberg Media Technologies GmbH
*/

#ifndef __asio_h__
#define __asio_h__

#include <stdint.h>

#ifdef _WIN32
#define ASIO_CALLING_CONVENTION __stdcall
#else
#define ASIO_CALLING_CONVENTION
#endif

// Platform-specific types
#ifdef _WIN32
typedef intptr_t ASIOHandle;
#else
typedef void* ASIOHandle;
#endif

// ASIO Types
typedef int32_t ASIOError;
typedef int32_t ASIOBool;
typedef double ASIOSampleRate;
typedef int64_t ASIOSamples;
typedef int64_t ASIOTimeStamp;

// ASIO Error Codes
#define ASE_OK                    0
#define ASE_SUCCESS               0x3F482AA0
#define ASE_NotPresent           -1000
#define ASE_HWMalfunction        -1001
#define ASE_InvalidParameter     -1002
#define ASE_InvalidMode          -1003
#define ASE_SPNotAdvancing       -1004
#define ASE_NoClock              -1005
#define ASE_NoMemory             -1006

// ASIO Bool values
#define ASIOTrue                 1
#define ASIOFalse                0

// ASIO Sample Types
typedef enum {
    ASIOSTInt16MSB = 0,
    ASIOSTInt24MSB = 1,
    ASIOSTInt32MSB = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,
    ASIOSTInt16LSB = 16,
    ASIOSTInt24LSB = 17,
    ASIOSTInt32LSB = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,
    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40
} ASIOSampleType;

// ASIO Driver Info - filled by driver via ASIOEntry
typedef struct ASIODriverInfo {
    char name[32];
    char version[32];
    char author[32];
    char description[128];
    ASIOError (ASIO_CALLING_CONVENTION *ASIOInit)(void* sysRef);
    void (ASIO_CALLING_CONVENTION *ASIOExit)();
    ASIOError (ASIO_CALLING_CONVENTION *ASIOStart)();
    void (ASIO_CALLING_CONVENTION *ASIOStop)();
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetChannels)(int32_t* numInputChannels, int32_t* numOutputChannels);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetLatencies)(int32_t* inputLatency, int32_t* outputLatency);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetBufferSize)(int32_t* minSize, int32_t* maxSize, int32_t* preferredSize, int32_t* granularity);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOCanSampleRate)(ASIOSampleRate rate);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetSampleRate)(ASIOSampleRate* rate);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOSetSampleRate)(ASIOSampleRate rate);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetClockSources)(void* clocks, int32_t* numClockSources);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOSetClockSource)(int32_t clockSourceIndex);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetSamplePosition)(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOGetChannelInfo)(void* info);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOCreateBuffers)(void* bufferInfos, int32_t numChannels, int32_t bufferSize, void* callbacks);
    ASIOError (ASIO_CALLING_CONVENTION *ASIODisposeBuffers)();
    ASIOError (ASIO_CALLING_CONVENTION *ASIOControlPanel)();
    ASIOError (ASIO_CALLING_CONVENTION *ASIOFuture)(int32_t selector, void* opt);
    ASIOError (ASIO_CALLING_CONVENTION *ASIOOutputReady)();
} ASIODriverInfo;

// ASIO Buffer Info
typedef struct ASIOBufferInfo {
    int32_t isInput;
    int32_t channelNum;
    void* buffers[2];  // double buffering
} ASIOBufferInfo;

// ASIO Channel Info
typedef struct ASIOChannelInfo {
    int32_t channel;
    int32_t isInput;
    int32_t isActive;
    char name[32];
    ASIOSampleType type;
} ASIOChannelInfo;

// ASIO Time
typedef struct ASIOTime {
    ASIOTimeStamp time;
    ASIOSamples samples;
    uint32_t flags;
} ASIOTime;

// ASIO Time Flags
#define kSystemTimeValid        1
#define kSamplePositionValid    2
#define kSampleRateValid        4
#define kSpeedValid             8
#define kSampleRateChanged      16
#define kClockSourceChanged     32

// ASIO Callbacks
typedef struct ASIOCallbacks {
    void (ASIO_CALLING_CONVENTION *bufferSwitch)(int32_t doubleBufferIndex, ASIOBool directProcess);
    ASIOTime* (ASIO_CALLING_CONVENTION *bufferSwitchTimeInfo)(ASIOTime* params, int32_t doubleBufferIndex, ASIOBool directProcess);
    void (ASIO_CALLING_CONVENTION *sampleRateDidChange)(ASIOSampleRate sRate);
    int32_t (ASIO_CALLING_CONVENTION *asioMessage)(int32_t selector, int32_t value, void* message, double* opt);
} ASIOCallbacks;

// IASIO Interface - wrapper for the driver's function table
struct IASIO {
    ASIODriverInfo* driverInfo = nullptr;
    
    ASIOError ASIOInit(void* sysRef) {
        return driverInfo ? driverInfo->ASIOInit(sysRef) : ASE_InvalidParameter;
    }
    void ASIOExit() {
        if (driverInfo && driverInfo->ASIOExit) driverInfo->ASIOExit();
    }
    ASIOError ASIOStart() {
        return driverInfo ? driverInfo->ASIOStart() : ASE_InvalidParameter;
    }
    void ASIOStop() {
        if (driverInfo && driverInfo->ASIOStop) driverInfo->ASIOStop();
    }
    ASIOError ASIOGetChannels(int32_t* numInputChannels, int32_t* numOutputChannels) {
        return driverInfo ? driverInfo->ASIOGetChannels(numInputChannels, numOutputChannels) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetLatencies(int32_t* inputLatency, int32_t* outputLatency) {
        return driverInfo ? driverInfo->ASIOGetLatencies(inputLatency, outputLatency) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetBufferSize(int32_t* minSize, int32_t* maxSize, int32_t* preferredSize, int32_t* granularity) {
        return driverInfo ? driverInfo->ASIOGetBufferSize(minSize, maxSize, preferredSize, granularity) : ASE_InvalidParameter;
    }
    ASIOError ASIOCanSampleRate(ASIOSampleRate rate) {
        return driverInfo ? driverInfo->ASIOCanSampleRate(rate) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetSampleRate(ASIOSampleRate* rate) {
        return driverInfo ? driverInfo->ASIOGetSampleRate(rate) : ASE_InvalidParameter;
    }
    ASIOError ASIOSetSampleRate(ASIOSampleRate rate) {
        return driverInfo ? driverInfo->ASIOSetSampleRate(rate) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetClockSources(void* clocks, int32_t* numClockSources) {
        return driverInfo ? driverInfo->ASIOGetClockSources(clocks, numClockSources) : ASE_InvalidParameter;
    }
    ASIOError ASIOSetClockSource(int32_t clockSourceIndex) {
        return driverInfo ? driverInfo->ASIOSetClockSource(clockSourceIndex) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
        return driverInfo ? driverInfo->ASIOGetSamplePosition(sPos, tStamp) : ASE_InvalidParameter;
    }
    ASIOError ASIOGetChannelInfo(void* info) {
        return driverInfo ? driverInfo->ASIOGetChannelInfo(info) : ASE_InvalidParameter;
    }
    ASIOError ASIOCreateBuffers(void* bufferInfos, int32_t numChannels, int32_t bufferSize, void* callbacks) {
        return driverInfo ? driverInfo->ASIOCreateBuffers(bufferInfos, numChannels, bufferSize, callbacks) : ASE_InvalidParameter;
    }
    ASIOError ASIODisposeBuffers() {
        return driverInfo ? driverInfo->ASIODisposeBuffers() : ASE_InvalidParameter;
    }
    ASIOError ASIOControlPanel() {
        return driverInfo ? driverInfo->ASIOControlPanel() : ASE_InvalidParameter;
    }
    ASIOError ASIOFuture(int32_t selector, void* opt) {
        return driverInfo ? driverInfo->ASIOFuture(selector, opt) : ASE_InvalidParameter;
    }
    ASIOError ASIOOutputReady() {
        return driverInfo ? driverInfo->ASIOOutputReady() : ASE_InvalidParameter;
    }
    void release() {
        if (driverInfo && driverInfo->ASIOExit) driverInfo->ASIOExit();
    }
    virtual ~IASIO() {}
};

// ASIO Functions (exported by driver)
#ifdef __cplusplus
extern "C" {
#endif

// Entry point for ASIO drivers
typedef ASIOError (ASIO_CALLING_CONVENTION *ASIOEntryProc)(ASIODriverInfo* info);

#ifdef __cplusplus
}
#endif

#endif // __asio_h__