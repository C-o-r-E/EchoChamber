//
//  main.c
//  EchoChamber
//
//  Created by Corey Clayton on 2013-01-15.
//  Copyright (c) 2013 Awake Coding. All rights reserved.
//

#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/CoreAudio.h>

#include <AudioToolbox/AudioToolbox.h>

static const int numBuffers = 3;
struct AQRecorderState
{
    AudioStreamBasicDescription  dataFormat;
    AudioQueueRef                queue;
    AudioQueueBufferRef          buffers[numBuffers];
    AudioFileID                  audioFile;
    UInt32                       bufferByteSize;
    SInt64                       currentPacket;
    bool                         isRunning;
};


void InputCallback (
                                void                                *inUserData,
                                AudioQueueRef                       inAQ,
                                AudioQueueBufferRef                 inBuffer,
                                const AudioTimeStamp                *inStartTime,
                                UInt32                              inNumberPacketDescriptions,
                                const AudioStreamPacketDescription  *inPacketDescs
                                );


void InputCallback (
                    void                                *inUserData,
                    AudioQueueRef                       inAQ,
                    AudioQueueBufferRef                 inBuffer,
                    const AudioTimeStamp                *inStartTime,
                    UInt32                              inNumberPacketDescriptions,
                    const AudioStreamPacketDescription  *inPacketDescs
                    )
{
    OSStatus status;
    struct AQRecorderState * rState;
    rState = inUserData;
    
    
    if (inNumberPacketDescriptions == 0 && rState->dataFormat.mBytesPerPacket != 0)
    {
            inNumberPacketDescriptions = inBuffer->mAudioDataByteSize / rState->dataFormat.mBytesPerPacket;
    }
    
    //write to disc
    
    status = AudioFileWritePackets(
                          rState->audioFile,
                          false,
                          rState->bufferByteSize,
                          NULL,
                          rState->currentPacket,
                          &inNumberPacketDescriptions,
                          inBuffer->mAudioData);
    
    if (status == noErr) {
        rState->currentPacket += inNumberPacketDescriptions;
    }
    
    if (rState->isRunning == 0)
    {
        return ;
    }
    
    status = AudioQueueEnqueueBuffer(
                                     rState->queue,
                                     inBuffer,
                                     0,
                                     NULL);
    
    if (status != noErr)
    {
        printf("AudioQueueEnqueueBuffer() returned status = %d\n", status);
    }
    
}

void DeriveBufferSize (AudioQueueRef                audioQueue,
                       AudioStreamBasicDescription  *ASBDescription,
                       Float64                      seconds, 
                       UInt32                       *outBufferSize)
{
    static const int maxBufferSize = 0x50000;
    
    int maxPacketSize = ASBDescription->mBytesPerPacket;
    if (maxPacketSize == 0)
    {
        UInt32 maxVBRPacketSize = sizeof(maxPacketSize);
        AudioQueueGetProperty (audioQueue,
                               kAudioQueueProperty_MaximumOutputPacketSize,
                               // in Mac OS X v10.5, instead use
                               //   kAudioConverterPropertyMaximumOutputPacketSize
                               &maxPacketSize,
                               &maxVBRPacketSize
                               );
    }
    
    Float64 numBytesForTime =
    ASBDescription->mSampleRate * maxPacketSize * seconds;
    *outBufferSize = (UInt32) (numBytesForTime < maxBufferSize ? numBytesForTime : maxBufferSize);
}


int main(int argc, const char * argv[])
{
    struct AQRecorderState recorderState;
    
    printf("Let's create an audio queue for input!\n");
    
    CFURLRef urlpath;
    char path[] = "/Users/corey/audiotest.wav";
    urlpath = CFURLCreateFromFileSystemRepresentation(NULL, (const unsigned char *) path, strlen(path), FALSE);
    
    OSStatus status;
    
    recorderState.dataFormat.mSampleRate = 44100.0;
    recorderState.dataFormat.mFormatID = kAudioFormatLinearPCM;
    recorderState.dataFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    recorderState.dataFormat.mBytesPerPacket = 4;
    recorderState.dataFormat.mFramesPerPacket = 1;
    recorderState.dataFormat.mBytesPerFrame = 4;
    recorderState.dataFormat.mChannelsPerFrame = 2;
    recorderState.dataFormat.mBitsPerChannel = 16;
    
    status = AudioFileCreateWithURL(urlpath,
                                    kAudioFileWAVEType,
                                    &recorderState.dataFormat,
                                    kAudioFileFlags_EraseFile,
                                    &recorderState.audioFile);
    
    if (status != noErr)
    {
        printf("Failed to create Audio File. Status code: %d\n", status);
        exit(1);
    }
    
    printf("Created File [%s]\n", path);
    
    
    status = AudioQueueNewInput(&recorderState.dataFormat,
                       InputCallback,
                       &recorderState,
                       NULL,
                       kCFRunLoopCommonModes,
                       0,
                       &recorderState.queue);
    
    if (status != noErr)
    {
        printf("Failed to create a new Audio Queue. Status code: %d\n", status);
    }
    
    
    UInt32 dataFormatSize = sizeof (recorderState.dataFormat);
    
    AudioQueueGetProperty(recorderState.queue,
                          kAudioConverterCurrentInputStreamDescription,
                          &recorderState.dataFormat,
                          &dataFormatSize);
    
    
    DeriveBufferSize(recorderState.queue, &recorderState.dataFormat, 0.5, &recorderState.bufferByteSize);
    
    
    printf("Preparing a set of buffers...");
    
    for (int i = 0; i < numBuffers; ++i)
    {
        AudioQueueAllocateBuffer(recorderState.queue,
                                  recorderState.bufferByteSize,
                                  &recorderState.buffers[i]);
        
        AudioQueueEnqueueBuffer(recorderState.queue,
                                 recorderState.buffers[i],
                                 0,
                                 NULL);
    }
    
    printf("done\n");
    
    printf("recording...\n");
    
    
    
    recorderState.currentPacket = 0;
    recorderState.isRunning = true;
    
    AudioQueueStart (recorderState.queue, NULL);
    
    sleep(1);
    
    AudioQueueStop (recorderState.queue, true);
    
    recorderState.isRunning = false;
    
    printf("finished!\n");
    
    //CreateInputDeviceArray();
    
    return 0;
}


CFArrayRef CreateInputDeviceArray()
{
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };
    
    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize);
    if(kAudioHardwareNoError != status) {
        fprintf(stderr, "AudioObjectGetPropertyDataSize (kAudioHardwarePropertyDevices) failed: %i\n", status);
        return NULL;
    }
    
    UInt32 deviceCount = (UInt32)(dataSize / sizeof(AudioDeviceID));
    
    AudioDeviceID *audioDevices = (AudioDeviceID *)(malloc(dataSize));
    if(NULL == audioDevices) {
        fputs("Unable to allocate memory", stderr);
        return NULL;
    }
    
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize, audioDevices);
    if(kAudioHardwareNoError != status) {
        fprintf(stderr, "AudioObjectGetPropertyData (kAudioHardwarePropertyDevices) failed: %i\n", status);
        free(audioDevices), audioDevices = NULL;
        return NULL;
    }
    
    CFMutableArrayRef inputDeviceArray = CFArrayCreateMutable(kCFAllocatorDefault, deviceCount, &kCFTypeArrayCallBacks);
    if(NULL == inputDeviceArray) {
        fputs("CFArrayCreateMutable failed", stderr);
        free(audioDevices), audioDevices = NULL;
        return NULL;
    }
    
    // Iterate through all the devices and determine which are input-capable
    propertyAddress.mScope = kAudioDevicePropertyScopeInput;
    for(UInt32 i = 0; i < deviceCount; ++i) {
        // Query device UID
        CFStringRef deviceUID = NULL;
        dataSize = sizeof(deviceUID);
        propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceUID);
        if(kAudioHardwareNoError != status) {
            fprintf(stderr, "AudioObjectGetPropertyData (kAudioDevicePropertyDeviceUID) failed: %i\n", status);
            continue;
        }
        
        // Query device name
        CFStringRef deviceName = NULL;
        dataSize = sizeof(deviceName);
        propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceName);
        if(kAudioHardwareNoError != status) {
            fprintf(stderr, "AudioObjectGetPropertyData (kAudioDevicePropertyDeviceNameCFString) failed: %i\n", status);
            continue;
        }
        
        // Query device manufacturer
        CFStringRef deviceManufacturer = NULL;
        dataSize = sizeof(deviceManufacturer);
        propertyAddress.mSelector = kAudioDevicePropertyDeviceManufacturerCFString;
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceManufacturer);
        if(kAudioHardwareNoError != status) {
            fprintf(stderr, "AudioObjectGetPropertyData (kAudioDevicePropertyDeviceManufacturerCFString) failed: %i\n", status);
            continue;
        }
        
        // Determine if the device is an input device (it is an input device if it has input channels)
        dataSize = 0;
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        status = AudioObjectGetPropertyDataSize(audioDevices[i], &propertyAddress, 0, NULL, &dataSize);
        if(kAudioHardwareNoError != status) {
            fprintf(stderr, "AudioObjectGetPropertyDataSize (kAudioDevicePropertyStreamConfiguration) failed: %i\n", status);
            continue;
        }
        
        AudioBufferList *bufferList = (AudioBufferList *)(malloc(dataSize));
        if(NULL == bufferList) {
            fputs("Unable to allocate memory", stderr);
            break;
        }
        
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, bufferList);
        if(kAudioHardwareNoError != status || 0 == bufferList->mNumberBuffers) {
            if(kAudioHardwareNoError != status)
                fprintf(stderr, "AudioObjectGetPropertyData (kAudioDevicePropertyStreamConfiguration) failed: %i\n", status);
            free(bufferList), bufferList = NULL;
            continue;
        }
        
        free(bufferList), bufferList = NULL;
        
        // Add a dictionary for this device to the array of input devices
        CFStringRef keys    []  = { CFSTR("deviceUID"),     CFSTR("deviceName"),    CFSTR("deviceManufacturer") };
        CFStringRef values  []  = { deviceUID,              deviceName,             deviceManufacturer };
        
        CFDictionaryRef deviceDictionary = CFDictionaryCreate(kCFAllocatorDefault,
                                                              (const void **) (keys),
                                                              (const void **) (values),
                                                              3,
                                                              &kCFTypeDictionaryKeyCallBacks,
                                                              &kCFTypeDictionaryValueCallBacks);
        
        
        CFArrayAppendValue(inputDeviceArray, deviceDictionary);
        
        CFRelease(deviceDictionary), deviceDictionary = NULL;
    }
    
    free(audioDevices), audioDevices = NULL;
    
    // Return a non-mutable copy of the array
    CFArrayRef copy = CFArrayCreateCopy(kCFAllocatorDefault, inputDeviceArray);
    CFRelease(inputDeviceArray), inputDeviceArray = NULL;
    
    return copy;
}
