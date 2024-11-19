/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ProcessIdentity.h"
#include "SampleMap.h"
#include <wtf/Function.h>
#include <wtf/Lock.h>
#include <wtf/Ref.h>
#include <wtf/RetainPtr.h>
#include <wtf/ThreadSafeWeakPtr.h>

OBJC_CLASS AVSampleBufferDisplayLayer;
OBJC_CLASS AVSampleBufferVideoRenderer;
OBJC_PROTOCOL(WebSampleBufferVideoRendering);
typedef struct opaqueCMBufferQueue *CMBufferQueueRef;
typedef struct opaqueCMSampleBuffer *CMSampleBufferRef;
typedef struct OpaqueCMTimebase* CMTimebaseRef;
typedef struct __CVBuffer* CVPixelBufferRef;

namespace WTF {
class WorkQueue;
}

namespace WebCore {

class MediaSample;
class WebCoreDecompressionSession;

class VideoMediaSampleRenderer : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<VideoMediaSampleRenderer, WTF::DestructionThread::Main> {
public:
    static Ref<VideoMediaSampleRenderer> create(WebSampleBufferVideoRendering *renderer) { return adoptRef(*new VideoMediaSampleRenderer(renderer)); }
    ~VideoMediaSampleRenderer();

    bool prefersDecompressionSession() { return m_prefersDecompressionSession; }
    void setPrefersDecompressionSession(bool);

    void setTimebase(RetainPtr<CMTimebaseRef>&&);
    RetainPtr<CMTimebaseRef> timebase() const;

    bool isReadyForMoreMediaData() const;
    void requestMediaDataWhenReady(Function<void()>&&);
    void enqueueSample(const MediaSample&);
    void stopRequestingMediaData();

    void flush();

    void expectMinimumUpcomingSampleBufferPresentationTime(const MediaTime&);
    void resetUpcomingSampleBufferPresentationTimeExpectations();

    WebSampleBufferVideoRendering *renderer() const;
    AVSampleBufferDisplayLayer *displayLayer() const;

    struct DisplayedPixelBufferEntry {
        RetainPtr<CVPixelBufferRef> pixelBuffer;
        MediaTime presentationTimeStamp;
    };
    DisplayedPixelBufferEntry copyDisplayedPixelBuffer();
    CGRect bounds() const;

    unsigned totalVideoFrames() const;
    unsigned droppedVideoFrames() const;
    unsigned corruptedVideoFrames() const;
    MediaTime totalFrameDelay() const;

    void setResourceOwner(const ProcessIdentity&);

private:
    VideoMediaSampleRenderer(WebSampleBufferVideoRendering *);

    void clearTimebase();

    void resetReadyForMoreSample();
    void initializeDecompressionSession();
    void decodeNextSample();
    void decodedFrameAvailable(RetainPtr<CMSampleBufferRef>&&);
    void maybeQueueFrameForDisplay(CMSampleBufferRef);
    void flushCompressedSampleQueue();
    void flushDecodedSampleQueue();
    void purgeDecodedSampleQueue();
    CMBufferQueueRef ensureDecodedSampleQueue();
    void assignResourceOwner(CMSampleBufferRef);
    void maybeBecomeReadyForMoreMediaData();

    void cancelTimer();

    const Ref<WTF::WorkQueue> m_workQueue;
    RetainPtr<AVSampleBufferDisplayLayer> m_displayLayer;
    RetainPtr<AVSampleBufferVideoRenderer> m_renderer;
    mutable Lock m_lock;
    RetainPtr<CMTimebaseRef> m_timebase WTF_GUARDED_BY_LOCK(m_lock);
    OSObjectPtr<dispatch_source_t> m_timerSource WTF_GUARDED_BY_LOCK(m_lock);
    std::atomic<ssize_t> m_framesBeingDecoded { 0 };
    std::atomic<int> m_flushId { 0 };
    Deque<std::pair<RetainPtr<CMSampleBufferRef>, int>> m_compressedSampleQueue WTF_GUARDED_BY_CAPABILITY(m_workQueue.get());
    RetainPtr<CMBufferQueueRef> m_decodedSampleQueue WTF_GUARDED_BY_CAPABILITY(m_workQueue.get());
    RefPtr<WebCoreDecompressionSession> m_decompressionSession;
    bool m_isDecodingSample WTF_GUARDED_BY_CAPABILITY(m_workQueue.get()) { false };
    bool m_isDisplayingSample WTF_GUARDED_BY_CAPABILITY(m_workQueue.get()) { false };
    Function<void()> m_readyForMoreSampleFunction;
    bool m_prefersDecompressionSession { false };
    std::optional<uint32_t> m_currentCodec;
    std::atomic<bool> m_gotDecodingError { false };

    ProcessIdentity m_resourceOwner;
};

} // namespace WebCore
