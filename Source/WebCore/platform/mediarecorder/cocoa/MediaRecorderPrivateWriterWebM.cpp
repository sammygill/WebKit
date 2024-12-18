/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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

#import "config.h"
#import "MediaRecorderPrivateWriterWebM.h"

#if ENABLE(MEDIA_RECORDER_WEBM)

#import "CMUtilities.h"
#import "Logging.h"
#import "MediaSampleAVFObjC.h"
#import "MediaUtilities.h"
#import "WebMAudioUtilitiesCocoa.h"
#import <webm/mkvmuxer/mkvmuxer.h>
#import <wtf/NativePromise.h>
#import <wtf/TZoneMallocInlines.h>
#import <wtf/UniqueRef.h>

#import <pal/cf/CoreMediaSoftLink.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(MediaRecorderPrivateWriterWebM);

static const char* mkvCodeIcForMediaVideoCodecId(FourCC codec)
{
    switch (codec.value) {
    case 'vp08': return mkvmuxer::Tracks::kVp8CodecId;
    case 'vp92':
    case kCMVideoCodecType_VP9: return mkvmuxer::Tracks::kVp9CodecId;
    case kCMVideoCodecType_AV1: return mkvmuxer::Tracks::kAv1CodecId;
    case kAudioFormatOpus: return mkvmuxer::Tracks::kOpusCodecId;
    default:
        ASSERT_NOT_REACHED("Unsupported codec");
        return "";
    }
}

class MediaRecorderPrivateWriterWebMDelegate : public mkvmuxer::IMkvWriter {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(MediaRecorderPrivateWriterWebMDelegate);

public:
    explicit MediaRecorderPrivateWriterWebMDelegate(MediaRecorderPrivateWriterListener& listener)
        : m_listener(listener)
    {
        m_segment.Init(this);
        m_segment.set_mode(mkvmuxer::Segment::kFile);
        m_segment.OutputCues(true);
        auto* info = m_segment.GetSegmentInfo();
        info->set_writing_app("WebKit");
        info->set_muxing_app("WebKit");
    }

    // mkvmuxer::IMkvWriter:
    mkvmuxer::int32 Write(const void* buf, mkvmuxer::uint32 len) final
    {
        if (RefPtr protectedListener = m_listener.get())
            protectedListener->appendData(unsafeMakeSpan(static_cast<const uint8_t*>(buf), len));
        m_position += len;
        return 0;
    }

    int64_t Position() const final { return m_position; }
    bool Seekable() const final { return false; }
    int32_t Position(int64_t) final { return -1; }
    void ElementStartNotify(uint64_t, int64_t) final { }

    std::optional<uint8_t> addAudioTrack(const AudioInfo& info)
    {
        auto trackIndex = m_segment.AddAudioTrack(info.rate, info.channels, 0);
        if (!trackIndex)
            return { };
        auto* audioTrack = reinterpret_cast<mkvmuxer::AudioTrack*>(m_segment.GetTrackByNumber(trackIndex));
        ASSERT(audioTrack);
        audioTrack->set_bit_depth(32u);
        audioTrack->set_codec_id(mkvCodeIcForMediaVideoCodecId(info.codecName));
        auto description = audioStreamDescriptionFromAudioInfo(info);
        auto opusHeader = createOpusPrivateData(description.streamDescription());
        audioTrack->SetCodecPrivate(opusHeader.data(), opusHeader.size());
        return trackIndex;
    }

    std::optional<uint8_t> addVideoTrack(const VideoInfo& info)
    {
        auto trackIndex = m_segment.AddVideoTrack(info.size.width(), info.size.height(), 0);
        if (!trackIndex)
            return { };
        auto* videoTrack = reinterpret_cast<mkvmuxer::VideoTrack*>(m_segment.GetTrackByNumber(trackIndex));
        ASSERT(videoTrack);
        videoTrack->set_codec_id(mkvCodeIcForMediaVideoCodecId(info.codecName));
        return trackIndex;
    }

    bool addFrame(const std::span<const uint8_t>& data, uint8_t trackIndex, uint64_t timeNs, bool keyframe)
    {
        return m_segment.AddFrame(data.data(), data.size(), trackIndex, timeNs, keyframe);
    }

    void forceNewClusterOnNextFrame()
    {
        m_segment.ForceNewClusterOnNextFrame();
    }

    void finalize()
    {
        m_segment.Finalize();
    }

private:
    ThreadSafeWeakPtr<MediaRecorderPrivateWriterListener> m_listener;
    mkvmuxer::Segment m_segment;
    int64_t m_position { 0 };
};

std::unique_ptr<MediaRecorderPrivateWriter> MediaRecorderPrivateWriterWebM::create(MediaRecorderPrivateWriterListener& listener)
{
    return std::unique_ptr<MediaRecorderPrivateWriter> { new MediaRecorderPrivateWriterWebM(listener) };
}

MediaRecorderPrivateWriterWebM::MediaRecorderPrivateWriterWebM(MediaRecorderPrivateWriterListener& listener)
    : m_delegate(makeUniqueRef<MediaRecorderPrivateWriterWebMDelegate>(listener))
{
}

MediaRecorderPrivateWriterWebM::~MediaRecorderPrivateWriterWebM() = default;

std::optional<uint8_t> MediaRecorderPrivateWriterWebM::addAudioTrack(const AudioInfo& description)
{
    return m_delegate->addAudioTrack(description);
}

std::optional<uint8_t> MediaRecorderPrivateWriterWebM::addVideoTrack(const VideoInfo& description, const std::optional<CGAffineTransform>&)
{
    return m_delegate->addVideoTrack(description);
}

MediaRecorderPrivateWriterWebM::Result MediaRecorderPrivateWriterWebM::writeFrame(const MediaSamplesBlock& sample)
{
    bool success = true;
    for (auto& block : sample) {
        ASSERT(block.data);
        Ref buffer = Ref { *block.data }->makeContiguous();
        success &= m_delegate->addFrame(buffer->span(), sample.trackID(), Seconds { block.presentationTime.toDouble() }.nanosecondsAs<uint64_t>(), block.isSync());
    }
    return success ? Result::Success : Result::Failure;
}

void MediaRecorderPrivateWriterWebM::forceNewSegment(const MediaTime&)
{
    m_delegate->forceNewClusterOnNextFrame();
}

Ref<GenericPromise> MediaRecorderPrivateWriterWebM::close(const MediaTime&)
{
    m_delegate->finalize();
    return GenericPromise::createAndResolve();
}

} // namespace WebCore

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(MEDIA_RECORDER)
