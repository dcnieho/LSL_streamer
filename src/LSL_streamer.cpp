#include "LSL_streamer/LSL_streamer.h"
#include <vector>
#include <algorithm>
#include <string_view>
#include <numeric>
#include <map>
#include <ranges>

#include "Titta/utils.h"

namespace
{
    // default argument values
    namespace defaults
    {
        constexpr bool                  createStartsListening   = false;

        constexpr size_t                gazeBufSize             = 2<<19;        // about half an hour at 600Hz

        constexpr size_t                eyeImageBufSize         = 2<<11;        // about seven minutes at 2*5Hz
        constexpr bool                  eyeImageAsGIF           = false;        // NB: this is for outlet, not inlet

        constexpr size_t                extSignalBufSize        = 2<<9;

        constexpr size_t                timeSyncBufSize         = 2<<9;

        constexpr size_t                positioningBufSize      = 2<<11;

        constexpr int64_t               clearTimeRangeStart     = 0;
        constexpr int64_t               clearTimeRangeEnd       = std::numeric_limits<int64_t>::max();

        constexpr bool                  stopBufferEmpties       = false;
        constexpr Titta::BufferSide     consumeSide             = Titta::BufferSide::Start;
        constexpr size_t                consumeNSamp            = -1;           // this overflows on purpose, consume all samples is default
        constexpr int64_t               consumeTimeRangeStart   = 0;
        constexpr int64_t               consumeTimeRangeEnd     = std::numeric_limits<int64_t>::max();
        constexpr Titta::BufferSide     peekSide                = Titta::BufferSide::End;
        constexpr size_t                peekNSamp               = 1;
        constexpr int64_t               peekTimeRangeStart      = 0;
        constexpr int64_t               peekTimeRangeEnd        = std::numeric_limits<int64_t>::max();
        constexpr bool                  timeIsLocalTime         = true;
    }

    template <class...> constexpr std::false_type always_false{};

    template <Titta::Stream T> struct TittaStreamToLSLInletType { static_assert(always_false<T>, "TittaStreamToLSLInletType not implemented for this enum value: this stream type is not supported as an LSL_streamer inlet"); };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::Gaze> { using type = LSL_streamer::gaze; };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::EyeOpenness> { using type = LSL_streamer::gaze; };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::EyeImage> { using type = LSL_streamer::eyeImage; };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::ExtSignal> { using type = LSL_streamer::extSignal; };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::TimeSync> { using type = LSL_streamer::timeSync; };
    template <>                struct TittaStreamToLSLInletType<Titta::Stream::Positioning> { using type = LSL_streamer::positioning; };
    template <Titta::Stream T>
    using TittaStreamToLSLInletType_t = typename TittaStreamToLSLInletType<T>::type;

    template <typename T> struct LSLInletTypeToTittaStream { static_assert(always_false<T>, "LSLInletTypeToTittaStream not implemented for this type"); static constexpr Titta::Stream value = Titta::Stream::Unknown; };
    template <>           struct LSLInletTypeToTittaStream<LSL_streamer::gaze> { static constexpr Titta::Stream value = Titta::Stream::Gaze; };
    template <>           struct LSLInletTypeToTittaStream<LSL_streamer::eyeImage> { static constexpr Titta::Stream value = Titta::Stream::EyeImage; };
    template <>           struct LSLInletTypeToTittaStream<LSL_streamer::extSignal> { static constexpr Titta::Stream value = Titta::Stream::ExtSignal; };
    template <>           struct LSLInletTypeToTittaStream<LSL_streamer::timeSync> { static constexpr Titta::Stream value = Titta::Stream::TimeSync; };
    template <>           struct LSLInletTypeToTittaStream<LSL_streamer::positioning> { static constexpr Titta::Stream value = Titta::Stream::Positioning; };
    template <typename T>
    constexpr Titta::Stream LSLInletTypeToTittaStream_v = LSLInletTypeToTittaStream<T>::value;

    template <typename T> struct LSLInletTypeNumSamples { static_assert(always_false<T>, "LSLInletTypeNumSamples not implemented for this type"); static constexpr size_t value = 0; };
    template <>           struct LSLInletTypeNumSamples<LSL_streamer::gaze> { static constexpr size_t value = 43; };
    template <>           struct LSLInletTypeNumSamples<LSL_streamer::eyeImage> { static constexpr size_t value = 0; };
    template <>           struct LSLInletTypeNumSamples<LSL_streamer::extSignal> { static constexpr size_t value = 4; };
    template <>           struct LSLInletTypeNumSamples<LSL_streamer::timeSync> { static constexpr size_t value = 3; };
    template <>           struct LSLInletTypeNumSamples<LSL_streamer::positioning> { static constexpr size_t value = 8; };
    template <typename T>
    constexpr size_t LSLInletTypeNumSamples_v = LSLInletTypeNumSamples<T>::value;

    template <typename T> struct LSLInletTypeToChannelFormat { static_assert(always_false<T>, "LSLInletTypeToChannelFormat not implemented for this type"); static constexpr enum lsl::channel_format_t value = lsl::cf_undefined; };
    template <>           struct LSLInletTypeToChannelFormat<LSL_streamer::gaze> { static constexpr enum lsl::channel_format_t value = lsl::cf_double64; };
    template <>           struct LSLInletTypeToChannelFormat<LSL_streamer::eyeImage> { static constexpr enum lsl::channel_format_t value = lsl::cf_undefined; };
    template <>           struct LSLInletTypeToChannelFormat<LSL_streamer::extSignal> { static constexpr enum lsl::channel_format_t value = lsl::cf_int64; };
    template <>           struct LSLInletTypeToChannelFormat<LSL_streamer::timeSync> { static constexpr enum lsl::channel_format_t value = lsl::cf_int64; };
    template <>           struct LSLInletTypeToChannelFormat<LSL_streamer::positioning> { static constexpr enum lsl::channel_format_t value = lsl::cf_float32; };
    template <typename T>
    constexpr enum lsl::channel_format_t LSLInletTypeToChannelFormat_v = LSLInletTypeToChannelFormat<T>::value;

    template <enum lsl::channel_format_t T> struct LSLChannelFormatToCppType { static_assert(always_false<T>, "LSLChannelFormatToCppType not implemented for this enum value: this channel format is not supported by LSL_streamer"); };
    template <>                struct LSLChannelFormatToCppType<lsl::cf_float32> { using type = float; };
    template <>                struct LSLChannelFormatToCppType<lsl::cf_double64> { using type = double; };
    template <>                struct LSLChannelFormatToCppType<lsl::cf_int64> { using type = int64_t; };
    template <enum lsl::channel_format_t T>
    using LSLChannelFormatToCppType_t = typename LSLChannelFormatToCppType<T>::type;
}

// callbacks
void LSLGazeCallback(TobiiResearchGazeData* gaze_data_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        instance->receiveSample(gaze_data_, nullptr);
    }
}
void LSLEyeOpennessCallback(TobiiResearchEyeOpennessData* openness_data_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        instance->receiveSample(nullptr, openness_data_);
    }
}
void LSLEyeImageCallback(TobiiResearchEyeImage* eye_image_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        if (instance->isStreaming(Titta::Stream::EyeImage))
            instance->pushSample(eye_image_);
    }
}
void LSLEyeImageGifCallback(TobiiResearchEyeImageGif* eye_image_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        if (instance->isStreaming(Titta::Stream::EyeImage))
            instance->pushSample(eye_image_);
    }
}
void LSLExtSignalCallback(TobiiResearchExternalSignalData* ext_signal_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        if (instance->isStreaming(Titta::Stream::ExtSignal))
            instance->pushSample(*ext_signal_);
    }
}
void LSLTimeSyncCallback(TobiiResearchTimeSynchronizationData* time_sync_data_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        if (instance->isStreaming(Titta::Stream::TimeSync))
            instance->pushSample(*time_sync_data_);
    }
}
void LSLPositioningCallback(TobiiResearchUserPositionGuide* position_data_, void* user_data)
{
    if (user_data)
    {
        const auto instance = static_cast<LSL_streamer*>(user_data);
        if (instance->isStreaming(Titta::Stream::Positioning))
            instance->pushSample(*position_data_);
    }
}

// info getter static functions
TobiiResearchSDKVersion LSL_streamer::getTobiiSDKVersion()
{
    TobiiResearchSDKVersion sdk_version;
    const TobiiResearchStatus status = tobii_research_get_sdk_version(&sdk_version);
    if (status != TOBII_RESEARCH_STATUS_OK)
        ErrorExit("Titta::cpp: Cannot get Tobii SDK version", status);
    return sdk_version;
}
int32_t LSL_streamer::getLSLVersion()
{
    return lsl::library_version();
}

namespace
{
    // eye image helpers
    TobiiResearchStatus doSubscribeEyeImage(TobiiResearchEyeTracker* eyeTracker_, LSL_streamer* instance_, const bool asGif_)
    {
        if (asGif_)
            return tobii_research_subscribe_to_eye_image_as_gif(eyeTracker_, LSLEyeImageGifCallback, instance_);
        else
            return tobii_research_subscribe_to_eye_image       (eyeTracker_,    LSLEyeImageCallback, instance_);
    }
    TobiiResearchStatus doUnsubscribeEyeImage(TobiiResearchEyeTracker* eyeTracker_, const bool isGif_)
    {
        if (isGif_)
            return tobii_research_unsubscribe_from_eye_image_as_gif(eyeTracker_, LSLEyeImageGifCallback);
        else
            return tobii_research_unsubscribe_from_eye_image       (eyeTracker_,    LSLEyeImageCallback);
    }
}




LSL_streamer::LSL_streamer(std::string address_)
{
    connect(std::move(address_));
}
LSL_streamer::LSL_streamer(TobiiResearchEyeTracker* et_)
{
    connect(et_);
}
LSL_streamer::LSL_streamer(const TobiiTypes::eyeTracker& et_)
{
    connect(et_.et);
}
LSL_streamer::~LSL_streamer()
{
    stopOutlet(Titta::Stream::Gaze);
    stopOutlet(Titta::Stream::EyeOpenness);
    stopOutlet(Titta::Stream::EyeImage);
    stopOutlet(Titta::Stream::ExtSignal);
    stopOutlet(Titta::Stream::TimeSync);
    stopOutlet(Titta::Stream::Positioning);

    // stop all inlets
    for (const auto& id : _inStreams | std::views::keys)
        deleteListener(id);
}
uint32_t LSL_streamer::getID()
{
    static std::atomic<uint32_t> lastID = 0;
    return lastID++;
}
void LSL_streamer::CheckClocks()
{
    // check tobii/titta clock and lsl clock are the same
    // 1. warm up clocks by calling them once
    Titta::getSystemTimestamp();
    lsl::local_clock();

    // acquire a bunch of samples, in both orders of calling
    constexpr size_t nSample = 20;
    std::array<double, nSample> tobiiTime;
    std::array<double, nSample> lslTime;

    for (size_t i = 0; i < nSample / 2; i++)
    {
        tobiiTime[i] = Titta::getSystemTimestamp() / 1'000'000.;
        lslTime[i] = lsl::local_clock();
    }
    for (size_t i = nSample / 2; i < nSample; i++)
    {
        lslTime[i] = lsl::local_clock();
        tobiiTime[i] = Titta::getSystemTimestamp() / 1'000'000.;
    }
    // get differences
    std::array<double, nSample> diff;
    std::ranges::transform(tobiiTime, lslTime, diff.begin(), std::minus{});

    // get average value
    const auto average = std::reduce(diff.begin(), diff.end(), 0.) / nSample;

    // should be well within a millisecond (actually, if different clocks are used
    // it would be super wrong), so check
    if (std::abs(average) > 0.001)
        DoExitWithMsg(std::format("LSL and Tobii/Titta clocks are not the same (average offset over {} samples was {:.3f} s), or you are having some serious clock trouble. Cannot continue", nSample, average));
}


void LSL_streamer::connect(std::string address_)
{
    if (_localEyeTracker)
        DoExitWithMsg("Already connected to an eye tracker, cannot connect again");

    TobiiResearchEyeTracker* et;
    const TobiiResearchStatus status = tobii_research_get_eyetracker(address_.c_str(), &et);
    if (status != TOBII_RESEARCH_STATUS_OK)
        ErrorExit("Titta::cpp: Cannot get eye tracker \"" + address_ + "\"", status);
    connect(et);
}

void LSL_streamer::connect(TobiiResearchEyeTracker* et_)
{
    if (_localEyeTracker)
        DoExitWithMsg("Already connected to an eye tracker, cannot connect again");

    _localEyeTracker = std::make_unique<TobiiTypes::eyeTracker>(et_);
    CheckClocks();
}


bool LSL_streamer::startOutlet(std::string stream_, std::optional<bool> asGif_, const bool snake_case_on_stream_not_found /*= false*/)
{
    return startOutlet(Titta::stringToStream(std::move(stream_), snake_case_on_stream_not_found, true), asGif_);
}
bool LSL_streamer::startOutlet(const Titta::Stream stream_, std::optional<bool> asGif_)
{
    if (!_localEyeTracker)
        DoExitWithMsg("Not connected to an eye tracker, cannot start an outlet");

    // if already streaming, don't start again
    if (isStreaming(stream_))
        return false;

    // for gaze signal, get info about the eye tracker's gaze stream
    const auto hasFreq = stream_ == Titta::Stream::Gaze || stream_ == Titta::Stream::EyeOpenness;
    if (hasFreq)
        _localEyeTracker->refreshInfo();

    std::string type;
    int nChannel = 0;
    enum lsl::channel_format_t format = lsl::cf_undefined;
    switch (stream_)
    {
    case Titta::Stream::Gaze:
    case Titta::Stream::EyeOpenness:
        type = "Gaze";
        nChannel = LSLInletTypeNumSamples_v<TittaStreamToLSLInletType_t<Titta::Stream::Gaze>>;
        format = LSLInletTypeToChannelFormat_v<TittaStreamToLSLInletType_t<Titta::Stream::Gaze>>;
        break;
    case Titta::Stream::EyeImage:
        if (asGif_)
            type = "VideoCompressed";
        else
            type = "VideoRaw";
        nChannel = LSLInletTypeNumSamples_v<TittaStreamToLSLInletType_t<Titta::Stream::EyeImage>>;
        format = LSLInletTypeToChannelFormat_v<TittaStreamToLSLInletType_t<Titta::Stream::EyeImage>>;
        break;
    case Titta::Stream::ExtSignal:
        type = "TTL";
        nChannel = LSLInletTypeNumSamples_v<TittaStreamToLSLInletType_t<Titta::Stream::ExtSignal>>;
        format = LSLInletTypeToChannelFormat_v<TittaStreamToLSLInletType_t<Titta::Stream::ExtSignal>>;
        break;
    case Titta::Stream::TimeSync:
        type = "TimeSync";
        nChannel = LSLInletTypeNumSamples_v<TittaStreamToLSLInletType_t<Titta::Stream::TimeSync>>;
        format = LSLInletTypeToChannelFormat_v<TittaStreamToLSLInletType_t<Titta::Stream::TimeSync>>;
        break;
    case Titta::Stream::Positioning:
        type = "Positioning";
        nChannel = LSLInletTypeNumSamples_v<TittaStreamToLSLInletType_t<Titta::Stream::Positioning>>;
        format = LSLInletTypeToChannelFormat_v<TittaStreamToLSLInletType_t<Titta::Stream::Positioning>>;
        break;
    default:
        DoExitWithMsg(std::format("LSL_streamer::cpp::startOutlet: opening an outlet for {} stream is not supported.", Titta::streamToString(stream_)));
        break;
    }

    // set up the outlet
    auto streamName = Titta::streamToString(stream_);
    auto lslStreamName = std::format("Tobii_{}", streamName);
    lsl::stream_info info(lslStreamName,
        type,
        nChannel,
        hasFreq ? _localEyeTracker->frequency : lsl::IRREGULAR_RATE,
        format,
        std::format("LSL_streamer:{}@{}", lslStreamName, _localEyeTracker->serialNumber));

    // create meta-data
    info.desc()
        .append_child("acquisition")
        .append_child_value("manufacturer", "Tobii")
        .append_child_value("model", _localEyeTracker->model)
        .append_child_value("serial_number", _localEyeTracker->serialNumber)
        .append_child_value("firmware_version", _localEyeTracker->firmwareVersion)
        .append_child_value("tracking_mode", _localEyeTracker->trackingMode);
    auto channels = info.desc().append_child("channels");

    // describe the streams
    switch (stream_)
    {
    case Titta::Stream::Gaze:
        [[fallthrough]];
    case Titta::Stream::EyeOpenness:
        channels.append_child("channel")
            .append_child_value("label", "x.position_on_display_area.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ScreenX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.position_on_display_area.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ScreenY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "x.position_in_user_coordinates.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "IntersectionX")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_user_coordinates.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "IntersectionY")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_user_coordinates.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "IntersectionZ")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.gaze_point.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "diameter.pupil.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "Diameter")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.pupil.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.pupil.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "x.position_in_user_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilX")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_user_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilY")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_user_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilZ")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "x.position_in_track_box_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_track_box_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_track_box_coordinates.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PupilZ")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "valid.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.gaze_origin.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "diameter.eye_openness.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "EyeLidDistance")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.eye_openness.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.eye_openness.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");


        channels.append_child("channel")
            .append_child_value("label", "x.position_on_display_area.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ScreenX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.position_on_display_area.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ScreenY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "x.position_in_user_coordinates.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "IntersectionX")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_user_coordinates.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "IntersectionY")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_user_coordinates.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "IntersectionZ")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.gaze_point.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "diameter.pupil.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "Diameter")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.pupil.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.pupil.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "x.position_in_user_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilX")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_user_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilY")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_user_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilZ")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "x.position_in_track_box_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.position_in_track_box_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "z.position_in_track_box_coordinates.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PupilZ")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "valid.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.gaze_origin.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "diameter.eye_openness.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "EyeLidDistance")
            .append_child_value("unit", "mm");
        channels.append_child("channel")
            .append_child_value("label", "valid.eye_openness.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        channels.append_child("channel")
            .append_child_value("label", "available.eye_openness.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "AvailableFlag")
            .append_child_value("unit", "bool");

    case Titta::Stream::EyeImage:
        if (asGif_)
            type = "VideoCompressed";
        else
            type = "VideoRaw";
        break;
    case Titta::Stream::ExtSignal:
        channels.append_child("channel")
            .append_child_value("label", "device_time_stamp")
            .append_child_value("type", "TimeStamp")
            .append_child_value("unit", "us");
        channels.append_child("channel")
            .append_child_value("label", "system_time_stamp")
            .append_child_value("type", "TimeStamp")
            .append_child_value("unit", "us");
        channels.append_child("channel")
            .append_child_value("label", "value")
            .append_child_value("type", "TTLIn");
        channels.append_child("channel")
            .append_child_value("label", "change_type")
            .append_child_value("type", "flag");
        break;
    case Titta::Stream::TimeSync:
        channels.append_child("channel")
            .append_child_value("label", "system_request_time_stamp")
            .append_child_value("type", "TimeStamp")
            .append_child_value("unit", "us");
        channels.append_child("channel")
            .append_child_value("label", "device_time_stamp")
            .append_child_value("type", "TimeStamp")
            .append_child_value("unit", "us");
        channels.append_child("channel")
            .append_child_value("label", "system_response_time_stamp")
            .append_child_value("type", "TimeStamp")
            .append_child_value("unit", "us");
        break;
    case Titta::Stream::Positioning:
        channels.append_child("channel")
            .append_child_value("label", "x.user_position.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PositionX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.user_position.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PositionY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "z.user_position.left_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "PositionZ")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "valid.user_position.right_eye")
            .append_child_value("eye", "left")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");

        channels.append_child("channel")
            .append_child_value("label", "x.user_position.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PositionX")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "y.user_position.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PositionY")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "z.user_position.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "PositionZ")
            .append_child_value("unit", "normalized");
        channels.append_child("channel")
            .append_child_value("label", "valid.user_position.right_eye")
            .append_child_value("eye", "right")
            .append_child_value("type", "ValidFlag")
            .append_child_value("unit", "bool");
        break;
    }

    // make the outlet
    _outStreams.insert(std::make_pair(stream_,lsl::stream_outlet(info, 1)));

    // start the eye tracker stream
    return start(stream_, asGif_);
}


void LSL_streamer::setIncludeEyeOpennessInGaze(const bool include_)
{
    if (!_localEyeTracker)
        DoExitWithMsg("Not connected to an eye tracker, no possible to set outlet options");

    if (include_ && !(_localEyeTracker->capabilities & TOBII_RESEARCH_CAPABILITIES_HAS_EYE_OPENNESS_DATA))
        DoExitWithMsg(
            "LSL_streamer::cpp::setIncludeEyeOpennessInGaze: Cannot request to record the " + Titta::streamToString(Titta::Stream::EyeOpenness) + " stream, this eye tracker does not provide it"
        );

    _includeEyeOpennessInGaze = include_;

    // start/stop eye openness stream if needed
    if (_streamingGaze && !_includeEyeOpennessInGaze)
        stop(Titta::Stream::EyeOpenness);
    else if (_streamingGaze && _includeEyeOpennessInGaze)
        start(Titta::Stream::EyeOpenness);
}

bool LSL_streamer::start(const Titta::Stream stream_, std::optional<bool> asGif_)
{
    TobiiResearchStatus result=TOBII_RESEARCH_STATUS_OK;
    bool* stateVar = nullptr;
    switch (stream_)
    {
        case Titta::Stream::Gaze:
        {
            if (_streamingGaze)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // start sending
                result = tobii_research_subscribe_to_gaze_data(_localEyeTracker->et, LSLGazeCallback, this);
                stateVar = &_streamingGaze;
            }
            break;
        }
        case Titta::Stream::EyeOpenness:
        {
            if (_streamingEyeOpenness)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // start sending
                result = tobii_research_subscribe_to_eye_openness(_localEyeTracker->et, LSLEyeOpennessCallback, this);
                stateVar = &_streamingEyeOpenness;
            }
            break;
        }
        case Titta::Stream::EyeImage:
        {
            if (_streamingEyeImages)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // deal with default arguments
                const auto asGif = asGif_.value_or(defaults::eyeImageAsGIF);

                // if already recording and switching from gif to normal or other way, first stop old stream
                if (_streamingEyeImages)
                    if (asGif != _eyeImIsGif)
                        doUnsubscribeEyeImage(_localEyeTracker->et, _eyeImIsGif);
                    else
                        // nothing to do
                        return true;

                // subscribe to new stream
                result = doSubscribeEyeImage(_localEyeTracker->et, this, asGif);
                stateVar = &_streamingEyeImages;
                if (result==TOBII_RESEARCH_STATUS_OK)
                    // update type being recorded if subscription to stream was successful
                    _eyeImIsGif = asGif;
            }
            break;
        }
        case Titta::Stream::ExtSignal:
        {
            if (_streamingExtSignal)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // start sending
                result = tobii_research_subscribe_to_external_signal_data(_localEyeTracker->et, LSLExtSignalCallback, this);
                stateVar = &_streamingExtSignal;
            }
            break;
        }
        case Titta::Stream::TimeSync:
        {
            if (_streamingTimeSync)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // start sending
                result = tobii_research_subscribe_to_time_synchronization_data(_localEyeTracker->et, LSLTimeSyncCallback, this);
                stateVar = &_streamingTimeSync;
            }
            break;
        }
        case Titta::Stream::Positioning:
        {
            if (_streamingPositioning)
                result = TOBII_RESEARCH_STATUS_OK;
            else
            {
                // start sending
                result = tobii_research_subscribe_to_user_position_guide(_localEyeTracker->et, LSLPositioningCallback, this);
                stateVar = &_streamingPositioning;
            }
            break;
        }
        default:
        {
            DoExitWithMsg("LSL_streamer::cpp::start: Cannot start sending " + Titta::streamToString(stream_) + " stream, not supported to send via outlet");
            break;
        }
    }

    if (stateVar)
        *stateVar = result==TOBII_RESEARCH_STATUS_OK;

    if (result != TOBII_RESEARCH_STATUS_OK)
        ErrorExit("LSL_streamer::cpp::start: Cannot start recording " + Titta::streamToString(stream_) + " stream", result);
    else
    {
        // if requested to merge gaze and eye openness, a call to start eye openness also starts gaze
        if (     stream_== Titta::Stream::EyeOpenness && _includeEyeOpennessInGaze && !_streamingGaze)
            return start(Titta::Stream::Gaze       , asGif_);
        // if requested to merge gaze and eye openness, a call to start gaze also starts eye openness
        else if (stream_== Titta::Stream::Gaze        && _includeEyeOpennessInGaze && !_streamingEyeOpenness)
            return start(Titta::Stream::EyeOpenness, asGif_);
        return true;
    }

    // will never get here, but to make compiler happy
    return true;
}

// tobii to own type helpers
namespace {
    void convert(TobiiTypes::gazePoint& out_, const TobiiResearchGazePoint& in_)
    {
        out_.position_in_user_coordinates   = in_.position_in_user_coordinates;
        out_.position_on_display_area       = in_.position_on_display_area;
        out_.validity                       = in_.validity;
        out_.available                      = true;
    }
    void convert(TobiiTypes::pupilData& out_, const TobiiResearchPupilData& in_)
    {
        out_.diameter                       = in_.diameter;
        out_.validity                       = in_.validity;
        out_.available                      = true;
    }
    void convert(TobiiTypes::gazeOrigin& out_, const TobiiResearchGazeOrigin& in_)
    {
        out_.position_in_track_box_coordinates = in_.position_in_track_box_coordinates;
        out_.position_in_user_coordinates   = in_.position_in_user_coordinates;
        out_.validity                       = in_.validity;
        out_.available                      = true;
    }
    void convert(TobiiTypes::eyeOpenness& out_, const TobiiResearchEyeOpennessData* in_, const bool leftEye_)
    {
        if (leftEye_)
        {
            out_.diameter = in_->left_eye_openness_value;
            out_.validity = in_->left_eye_validity;
        }
        else
        {
            out_.diameter = in_->right_eye_openness_value;
            out_.validity = in_->right_eye_validity;
        }
        out_.available = true;
    }
    void convert(TobiiTypes::eyeData& out_, const TobiiResearchEyeData& in_)
    {
        convert(out_.gaze_point, in_.gaze_point);
        convert(out_.pupil, in_.pupil_data);
        convert(out_.gaze_origin, in_.gaze_origin);
    }
}

void LSL_streamer::receiveSample(const TobiiResearchGazeData* gaze_data_, const TobiiResearchEyeOpennessData* openness_data_)
{
    const auto needStage = _streamingGaze && _streamingEyeOpenness;
    if (!needStage && !_gazeStagingEmpty)
    {
        // if any data in staging area but no longer expecting to merge, flush to output
        if (isStreaming(Titta::Stream::Gaze))
        {
            for (const auto& sample : _gazeStaging)
                pushSample(sample);
        }
        _gazeStaging.clear();
        _gazeStagingEmpty = true;
    }

    std::unique_lock l(_gazeStageMutex, std::defer_lock);
    if (needStage)
        l.lock();

    Titta::gaze* sample = nullptr;
    std::deque<Titta::gaze> emitBuffer;
    if (needStage)
    {
        // find if there is already a corresponding sample in the staging area
        for (auto it = _gazeStaging.begin(); it != _gazeStaging.end(); )
        {
            if ((!!gaze_data_     && it->device_time_stamp <     gaze_data_->device_time_stamp && it->left_eye.eye_openness.available) ||
                (!!openness_data_ && it->device_time_stamp < openness_data_->device_time_stamp && it->left_eye.gaze_origin.available))
            {
                // We assume samples come in order. Here we have:
                // 1. a sample older than this     gaze     sample for which eye openness is already available, or
                // 2. a sample older than this eye openness sample for which     gaze     is already available;
                // emit it, continue searching
                emitBuffer.push_back(*it);
                it = _gazeStaging.erase(it);
            }
            else if ((!!gaze_data_     && it->device_time_stamp ==     gaze_data_->device_time_stamp) ||
                     (!!openness_data_ && it->device_time_stamp == openness_data_->device_time_stamp))
            {
                // found, this is the one we want. Move to output, take pointer to it as we'll be adding to it
                emitBuffer.push_back(*it);
                it = _gazeStaging.erase(it);
                sample = &emitBuffer.back();
                break;
            }
            else
                ++it;
        }
    }
    if (!sample)
    {
        if (needStage)
        {
            _gazeStaging.push_back({});
            sample = &_gazeStaging.back();
        }
        else
        {
            emitBuffer.push_back({});
            sample = &emitBuffer.back();
        }

        if (gaze_data_)
        {
            sample->device_time_stamp = gaze_data_->device_time_stamp;
            sample->system_time_stamp = gaze_data_->system_time_stamp;
        }
        else if (openness_data_)
        {
            sample->device_time_stamp = openness_data_->device_time_stamp;
            sample->system_time_stamp = openness_data_->system_time_stamp;
        }
    }

    if (gaze_data_)
    {
        // convert to own gaze data type
        convert(sample->left_eye,  gaze_data_->left_eye);
        convert(sample->right_eye, gaze_data_->right_eye);
    }
    else if (openness_data_)
    {
        // convert to own gaze data type
        convert(sample->left_eye.eye_openness , openness_data_, true);
        convert(sample->right_eye.eye_openness, openness_data_, false);
    }
    if (needStage)
        l.unlock();

    // output if anything
    if (!emitBuffer.empty())
    {
        if (isStreaming(Titta::Stream::Gaze))
            for (const auto& samp : emitBuffer)
                pushSample(samp);
    }
}

void LSL_streamer::pushSample(const Titta::gaze& sample_)
{
    using lsl_inlet_type = TittaStreamToLSLInletType_t<Titta::Stream::Gaze>;
    using data_t = LSLChannelFormatToCppType_t<LSLInletTypeToChannelFormat_v<lsl_inlet_type>>;

    const data_t sample[LSLInletTypeNumSamples_v<lsl_inlet_type>] = {
        sample_.left_eye.gaze_point.position_on_display_area.x, sample_.left_eye.gaze_point.position_on_display_area.y,
        sample_.left_eye.gaze_point.position_in_user_coordinates.x, sample_.left_eye.gaze_point.position_in_user_coordinates.y, sample_.left_eye.gaze_point.position_in_user_coordinates.z,
        static_cast<data_t>(sample_.left_eye.gaze_point.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.left_eye.gaze_point.available),
        sample_.left_eye.pupil.diameter,
        static_cast<data_t>(sample_.left_eye.pupil.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.left_eye.pupil.available),
        sample_.left_eye.gaze_origin.position_in_user_coordinates.x, sample_.left_eye.gaze_origin.position_in_user_coordinates.y, sample_.left_eye.gaze_origin.position_in_user_coordinates.z,
        sample_.left_eye.gaze_origin.position_in_track_box_coordinates.x, sample_.left_eye.gaze_origin.position_in_track_box_coordinates.y, sample_.left_eye.gaze_origin.position_in_track_box_coordinates.z,
        static_cast<data_t>(sample_.left_eye.gaze_origin.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.left_eye.gaze_origin.available),
        sample_.left_eye.eye_openness.diameter,
        static_cast<data_t>(sample_.left_eye.eye_openness.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.left_eye.eye_openness.available),

        sample_.right_eye.gaze_point.position_on_display_area.x, sample_.right_eye.gaze_point.position_on_display_area.y,
        sample_.right_eye.gaze_point.position_in_user_coordinates.x, sample_.right_eye.gaze_point.position_in_user_coordinates.y, sample_.right_eye.gaze_point.position_in_user_coordinates.z,
        static_cast<data_t>(sample_.right_eye.gaze_point.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.right_eye.gaze_point.available),
        sample_.right_eye.pupil.diameter,
        static_cast<data_t>(sample_.right_eye.pupil.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.right_eye.pupil.available),
        sample_.right_eye.gaze_origin.position_in_user_coordinates.x, sample_.right_eye.gaze_origin.position_in_user_coordinates.y, sample_.right_eye.gaze_origin.position_in_user_coordinates.z,
        sample_.right_eye.gaze_origin.position_in_track_box_coordinates.x, sample_.right_eye.gaze_origin.position_in_track_box_coordinates.y, sample_.right_eye.gaze_origin.position_in_track_box_coordinates.z,
        static_cast<data_t>(sample_.right_eye.gaze_origin.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.right_eye.gaze_origin.available),
        sample_.right_eye.eye_openness.diameter,
        static_cast<data_t>(sample_.right_eye.eye_openness.validity == TOBII_RESEARCH_VALIDITY_VALID),static_cast<data_t>(sample_.right_eye.eye_openness.available),

        static_cast<data_t>(sample_.device_time_stamp) / 1'000'000.
    };
    _outStreams.at(Titta::Stream::Gaze).push_sample(sample, static_cast<double>(sample_.system_time_stamp)/1'000'000.);
}
void LSL_streamer::pushSample(Titta::eyeImage&& sample_)
{

}
void LSL_streamer::pushSample(const Titta::extSignal& sample_)
{
    using lsl_inlet_type = TittaStreamToLSLInletType_t<Titta::Stream::ExtSignal>;
    using data_t = LSLChannelFormatToCppType_t<LSLInletTypeToChannelFormat_v<lsl_inlet_type>>;

    const data_t sample[LSLInletTypeNumSamples_v<lsl_inlet_type>] = {
        sample_.device_time_stamp, sample_.system_time_stamp, sample_.value, sample_.change_type
    };
    _outStreams.at(Titta::Stream::ExtSignal).push_sample(sample, static_cast<double>(sample_.system_time_stamp) / 1'000'000.);
}
void LSL_streamer::pushSample(const Titta::timeSync& sample_)
{
    using lsl_inlet_type = TittaStreamToLSLInletType_t<Titta::Stream::TimeSync>;
    using data_t = LSLChannelFormatToCppType_t<LSLInletTypeToChannelFormat_v<lsl_inlet_type>>;

    const data_t sample[LSLInletTypeNumSamples_v<lsl_inlet_type>] = {
        sample_.system_request_time_stamp, sample_.device_time_stamp, sample_.system_response_time_stamp
    };
    _outStreams.at(Titta::Stream::TimeSync).push_sample(sample, static_cast<double>(sample_.system_request_time_stamp) / 1'000'000.);
}
void LSL_streamer::pushSample(const Titta::positioning& sample_)
{
    using lsl_inlet_type = TittaStreamToLSLInletType_t<Titta::Stream::Positioning>;
    using data_t = LSLChannelFormatToCppType_t<LSLInletTypeToChannelFormat_v<lsl_inlet_type>>;

    const data_t sample[LSLInletTypeNumSamples_v<lsl_inlet_type>] = {
        sample_.left_eye.user_position.x, sample_.left_eye.user_position.y, sample_.left_eye.user_position.z,
        static_cast<float>(sample_.left_eye.validity == TOBII_RESEARCH_VALIDITY_VALID),
        sample_.right_eye.user_position.x, sample_.right_eye.user_position.y, sample_.right_eye.user_position.z,
        static_cast<float>(sample_.right_eye.validity == TOBII_RESEARCH_VALIDITY_VALID)
    };
    _outStreams.at(Titta::Stream::Positioning).push_sample(sample); // this stream doesn't have a timestamp
}

bool LSL_streamer::stop(const Titta::Stream stream_)
{
    TobiiResearchStatus result = TOBII_RESEARCH_STATUS_OK;
    bool* stateVar = nullptr;
    switch (stream_)
    {
    case Titta::Stream::Gaze:
        result = !_streamingGaze ? TOBII_RESEARCH_STATUS_OK : tobii_research_unsubscribe_from_gaze_data(_localEyeTracker->et, LSLGazeCallback);
        stateVar = &_streamingGaze;
        break;
    case Titta::Stream::EyeOpenness:
        result = !_streamingEyeOpenness ? TOBII_RESEARCH_STATUS_OK : tobii_research_unsubscribe_from_eye_openness(_localEyeTracker->et, LSLEyeOpennessCallback);
        stateVar = &_streamingEyeOpenness;
        break;
    case Titta::Stream::EyeImage:
        result = !_streamingEyeImages ? TOBII_RESEARCH_STATUS_OK : doUnsubscribeEyeImage(_localEyeTracker->et, _eyeImIsGif);
        stateVar = &_streamingEyeImages;
        break;
    case Titta::Stream::ExtSignal:
        result = !_streamingExtSignal ? TOBII_RESEARCH_STATUS_OK : tobii_research_unsubscribe_from_external_signal_data(_localEyeTracker->et, LSLExtSignalCallback);
        stateVar = &_streamingExtSignal;
        break;
    case Titta::Stream::TimeSync:
        result = !_streamingTimeSync ? TOBII_RESEARCH_STATUS_OK : tobii_research_unsubscribe_from_time_synchronization_data(_localEyeTracker->et, LSLTimeSyncCallback);
        stateVar = &_streamingTimeSync;
        break;
    case Titta::Stream::Positioning:
        result = !_streamingPositioning ? TOBII_RESEARCH_STATUS_OK : tobii_research_unsubscribe_from_user_position_guide(_localEyeTracker->et, LSLPositioningCallback);
        stateVar = &_streamingPositioning;
        break;
    }

    const bool success = result==TOBII_RESEARCH_STATUS_OK;
    if (stateVar && success)
        *stateVar = false;

    // if requested to merge gaze and eye openness, a call to stop eye openness also stops gaze
    if (stream_ == Titta::Stream::EyeOpenness && _includeEyeOpennessInGaze && _streamingGaze)
        return stop(Titta::Stream::Gaze) && success;
    // if requested to merge gaze and eye openness, a call to stop gaze also stops eye openness
    else if (stream_ == Titta::Stream::Gaze && _includeEyeOpennessInGaze && _streamingEyeOpenness)
        return stop(Titta::Stream::EyeOpenness) && success;
    return success;
}

bool LSL_streamer::isStreaming(std::string stream_, const bool snake_case_on_stream_not_found /*= false*/) const
{
    return isStreaming(Titta::stringToStream(std::move(stream_), snake_case_on_stream_not_found, true));
}
bool LSL_streamer::isStreaming(const Titta::Stream stream_) const
{
    bool isStreaming = false;
    switch (stream_)
    {
        case Titta::Stream::Gaze:
            isStreaming = _streamingGaze;
            break;
        case Titta::Stream::EyeOpenness:
            isStreaming = _streamingEyeOpenness;
            break;
        case Titta::Stream::EyeImage:
            isStreaming = _streamingEyeImages;
            break;
        case Titta::Stream::ExtSignal:
            isStreaming = _streamingExtSignal;
            break;
        case Titta::Stream::TimeSync:
            isStreaming = _streamingTimeSync;
            break;
        case Titta::Stream::Positioning:
            isStreaming = _streamingPositioning;
            break;
    }

    // EyeOpenness is always packed in a gaze stream, so check for that instead
    return isStreaming && ((stream_ == Titta::Stream::EyeOpenness && _outStreams.contains(Titta::Stream::Gaze)) || _outStreams.contains(stream_));
}

void LSL_streamer::stopOutlet(std::string stream_, const bool snake_case_on_stream_not_found /*= false*/)
{
    stopOutlet(Titta::stringToStream(std::move(stream_), snake_case_on_stream_not_found, true));
}

void LSL_streamer::stopOutlet(const Titta::Stream stream_)
{
    if (!_localEyeTracker)
        return;

    // stop the callback
    stop(stream_);

    // stop the outlet, if any
    if (_outStreams.contains(stream_))
        _outStreams.erase(stream_);
}


/* inlet stuff starts here */
namespace
{
inline int64_t timeStampSecondsToUs(double ts_)
{
    return static_cast<int64_t>(ts_ * 1'000'000);
}
Titta::Stream getInletTypeImpl(LSL_streamer::AllInlets& inlet_)
{
    return std::visit(
        [] <typename T>(LSL_streamer::Inlet<T>&) {
        return LSLInletTypeToTittaStream_v<T>;
    }
    , inlet_);
}

lsl::stream_inlet& getLSLInlet(LSL_streamer::AllInlets& inlet_)
{
    return std::visit(
        [](auto& in_) -> lsl::stream_inlet& {
            return in_._lsl_inlet;
        }, inlet_);
}

// helpers to make the below generic
template <typename DataType>
read_lock  lockForReading(LSL_streamer::Inlet<DataType>& inlet_) { return  read_lock(inlet_._mutex); }
template <typename DataType>
write_lock lockForWriting(LSL_streamer::Inlet<DataType>& inlet_) { return write_lock(inlet_._mutex); }
template <typename DataType>
std::vector<DataType>& getBuffer(LSL_streamer::Inlet<DataType>& inlet_)
{
    return inlet_._buffer;
}
template <typename DataType>
std::tuple<typename std::vector<DataType>::iterator, typename std::vector<DataType>::iterator>
getIteratorsFromSampleAndSide(std::vector<DataType>& buf_, const size_t NSamp_, const Titta::BufferSide side_)
{
    auto startIt    = std::begin(buf_);
    auto   endIt    = std::end(buf_);
    const auto nSamp= std::min(NSamp_, std::size(buf_));

    switch (side_)
    {
    case Titta::BufferSide::Start:
        endIt   = std::next(startIt, nSamp);
        break;
    case Titta::BufferSide::End:
        startIt = std::prev(endIt  , nSamp);
        break;
    default:
        DoExitWithMsg("LSL_streamer::::cpp::getIteratorsFromSampleAndSide: unknown Titta::BufferSide provided.");
        break;
    }
    return { startIt, endIt };
}

template <typename DataType>
std::tuple<typename std::vector<DataType>::iterator, typename std::vector<DataType>::iterator, bool>
getIteratorsFromTimeRange(std::vector<DataType>& buf_, const int64_t timeStart_, const int64_t timeEnd_, const bool timeIsLocalTime_)
{
    // !NB: appropriate locking is responsibility of caller!
    // find elements within given range of time stamps, both sides inclusive.
    // Since returns are iterators, what is returned is first matching element until one past last matching element
    // 1. get buffer to traverse, if empty, return
    auto startIt = std::begin(buf_);
    auto   endIt = std::end(buf_);
    if (std::empty(buf_))
        return {startIt,endIt, true};

    // 2. see which member variable to access
    int64_t DataType::* field;
    if (timeIsLocalTime_)
        field = &DataType::local_system_time_stamp;
    else
        field = &DataType::remote_system_time_stamp;

    // 3. check if requested times are before or after vector start and end
    const bool inclFirst = timeStart_ <= buf_.front().*field;
    const bool inclLast  = timeEnd_   >= buf_.back().*field;

    // 4. if start time later than beginning of samples, or end time earlier, find correct iterators
    if (!inclFirst)
        startIt = std::lower_bound(startIt, endIt, timeStart_, [&field](const DataType& a_, const int64_t& b_) {return a_.*field < b_;});
    if (!inclLast)
        endIt   = std::upper_bound(startIt, endIt, timeEnd_  , [&field](const int64_t& a_, const DataType& b_) {return a_ < b_.*field;});

    // 5. done, return
    return {startIt, endIt, inclFirst&&inclLast};
}

template <typename T>
std::vector<T> consumeFromVec(std::vector<T>& buf_, typename std::vector<T>::iterator startIt_, typename std::vector<T>::iterator endIt_)
{
    if (std::empty(buf_))
        return std::vector<T>{};

    // move out the indicated elements
    if (startIt_==std::begin(buf_) && endIt_==std::end(buf_))
        // whole buffer
        return std::vector<T>(std::move(buf_));
    else
    {
        std::vector<T> out;
        out.reserve(std::distance(startIt_, endIt_));
        out.insert(std::end(out), std::make_move_iterator(startIt_), std::make_move_iterator(endIt_));
        buf_.erase(startIt_, endIt_);
        return out;
    }
}

template <typename T>
std::vector<T> peekFromVec(const std::vector<T>& buf_, const typename std::vector<T>::const_iterator startIt_, const typename std::vector<T>::const_iterator endIt_)
{
    if (std::empty(buf_))
        return std::vector<T>{};

    // copy the indicated elements
    return std::vector<T>(startIt_, endIt_);
}

template <typename DataType>
void clearVec(LSL_streamer::Inlet<DataType>& inlet_, const int64_t timeStart_, const int64_t timeEnd_, const bool timeIsLocalTime_)
{
    auto l = lockForWriting(inlet_);  // NB: if C++ std gains upgrade_lock, replace this with upgrade lock that is converted to unique lock only after range is determined
    auto& buf = getBuffer(inlet_);
    if (std::empty(buf))
        return;

    // find applicable range
    auto [startIt, endIt, whole] = getIteratorsFromTimeRange(buf, timeStart_, timeEnd_, timeIsLocalTime_);
    // clear the flagged bit
    if (whole)
        buf.clear();
    else
        buf.erase(startIt, endIt);
}
}
template <typename DataType>
void checkInletType(LSL_streamer::AllInlets& inlet_, const uint32_t id_)
{
    if (!std::holds_alternative<LSL_streamer::Inlet<DataType>>(inlet_))
    {
        const auto wanted = LSLInletTypeToTittaStream_v<DataType>;
        const auto actual = getInletTypeImpl(inlet_);
        DoExitWithMsg(std::format("Inlet with id {} should be of type {}, but the inlet associated with that ID instead was of type {}. Fatal error", id_, Titta::streamToString(wanted), Titta::streamToString(actual)));
    }
}

LSL_streamer::AllInlets& LSL_streamer::getAllInletsVariant(const uint32_t id_) const
{
    if (!_inStreams.contains(id_))
        DoExitWithMsg(std::format("No inlet with id {} is known", id_));

    return *_inStreams.at(id_);
}
template <typename DataType>
LSL_streamer::Inlet<DataType>& LSL_streamer::getInlet(const uint32_t id_) const
{
    auto& allInlets = getAllInletsVariant(id_);
    checkInletType<DataType>(allInlets, id_);

    return std::get<Inlet<DataType>>(allInlets);
}
std::unique_ptr<std::thread>& LSL_streamer::getWorkerThread(LSL_streamer::AllInlets& inlet_)
{
    return std::visit(
        [](auto& in_) -> std::unique_ptr<std::thread>&{
            return in_._recorder;
        }, inlet_);
}
bool LSL_streamer::getWorkerThreadStopFlag(LSL_streamer::AllInlets& inlet_)
{
    return std::visit(
        [](auto& in_) -> bool {
            return in_._recorder_should_stop;
        }, inlet_);
}
void LSL_streamer::setWorkerThreadStopFlag(LSL_streamer::AllInlets& inlet_)
{
    std::visit(
        [](auto& in_){
            in_._recorder_should_stop = true;
        }, inlet_);
}

std::vector<lsl::stream_info> LSL_streamer::getRemoteStreams(std::string stream_, const bool snake_case_on_stream_not_found)
{
    if (!stream_.empty())
        return getRemoteStreams(Titta::stringToStream(std::move(stream_), snake_case_on_stream_not_found, true));
    else
        return getRemoteStreams(std::nullopt);
}
std::vector<lsl::stream_info> LSL_streamer::getRemoteStreams(std::optional<Titta::Stream> stream_)
{
    // filter if wanted
    if (stream_.has_value())
    {
        if (*stream_!=Titta::Stream::Gaze && *stream_!=Titta::Stream::EyeImage && *stream_!=Titta::Stream::ExtSignal && *stream_!=Titta::Stream::TimeSync && *stream_!=Titta::Stream::Positioning)
            DoExitWithMsg(std::format("LSL_streamer::cpp::getRemoteStreams: {} streams are not supported.", Titta::streamToString(*stream_)));
        const auto streamName = std::format("Tobii_{}", Titta::streamToString(*stream_));
        return lsl::resolve_stream("name", streamName, 0, 2.);
    }
    else
        return lsl::resolve_streams(2.);
}

uint32_t LSL_streamer::createListener(std::string streamSourceID_, std::optional<size_t> initialBufferSize_, std::optional<bool> startListening_)
{
    if (streamSourceID_.empty())
        DoExitWithMsg("LSL_streamer::createListener: must specify stream source ID, cannot be empty");

    // find stream with specified source ID
    const auto streams = lsl::resolve_stream("source_id", streamSourceID_, 0, 2.);
    if (streams.empty())
        DoExitWithMsg(std::format("LSL_streamer::createListener: stream with source ID {} could not be found", streamSourceID_));
    else if (streams.size()>1)
        DoExitWithMsg(std::format("LSL_streamer::createListener: more than one stream with source ID {} found", streamSourceID_));

    // start listening
    return createListener(streams[0], initialBufferSize_, startListening_);
}
uint32_t LSL_streamer::createListener(lsl::stream_info streamInfo_, std::optional<size_t> initialBufferSize_, std::optional<bool> doStartListening_)
{
    // deal with default arguments
    const auto doStartListening = doStartListening_.value_or(defaults::createStartsListening);

    if (!streamInfo_.source_id().starts_with("LSL_streamer:Tobii_"))
        DoExitWithMsg(std::format("LSL_streamer::createListener: stream {} (source_id: {}) is not an LSL_streamer stream, cannot be used.", streamInfo_.name(), streamInfo_.source_id()));

# define MAKE_INLET(type, defaultName) \
    _inStreams.emplace(id, \
        std::make_unique<AllInlets>(std::in_place_type<Inlet<type>>, streamInfo_) \
    ); \
    auto& inlet = getInlet<type>(id); \
    createdInlet = &inlet._lsl_inlet; \
    getBuffer<type>(inlet).reserve(initialBufferSize_.value_or(defaults::defaultName));

    // subscribe to the stream
    const auto id = getID();
    const auto sType = streamInfo_.type();
    lsl::stream_inlet* createdInlet = nullptr;
    if (sType =="Gaze")
    {
        MAKE_INLET(LSL_streamer::gaze, gazeBufSize)
    }
    else if (sType == "VideoCompressed" || sType == "VideoRaw")
    {
        MAKE_INLET(LSL_streamer::eyeImage, eyeImageBufSize)
    }
    else if (sType == "TTL")
    {
        MAKE_INLET(LSL_streamer::extSignal, extSignalBufSize)
    }
    else if (sType == "TimeSync")
    {
        MAKE_INLET(LSL_streamer::timeSync, timeSyncBufSize)
    }
    else if (sType == "Positioning")
    {
        MAKE_INLET(LSL_streamer::positioning, positioningBufSize)
    }
    else
        DoExitWithMsg(std::format("LSL_streamer::createListener: stream {} (source_id: {}) has type {}, which is not understood.", streamInfo_.name(), streamInfo_.source_id(), sType));

    if (createdInlet)
    {
        // immediately start time offset collection, we'll need that
        createdInlet->time_correction(5.);

        // start the stream
        if (doStartListening)
            startListening(id);
    }

    return id;
#undef MAKE_INLET
}

Titta::Stream LSL_streamer::getInletType(const uint32_t id_) const
{
    return getInletTypeImpl(getAllInletsVariant(id_));
}

lsl::stream_info LSL_streamer::getInletInfo(const uint32_t id_) const
{
    // get inlet
    auto& inlet = getAllInletsVariant(id_);
    lsl::stream_inlet& lsl_inlet = std::visit(
        [](auto& in_) -> lsl::stream_inlet& {
            return in_._lsl_inlet;
        }, inlet);

    // return it's stream info
    return lsl_inlet.info(2.);
}

void LSL_streamer::startListening(const uint32_t id_)
{
    auto& inlet = getAllInletsVariant(id_);
    // ignore if listener already started
    if (getWorkerThread(inlet))
        return;

    // start receiving samples
    auto& lslInlet = getLSLInlet(inlet);
    lslInlet.open_stream(5.);

    // start recorder thread
    switch (getInletType(id_))
    {
    case Titta::Stream::Gaze:
    case Titta::Stream::EyeOpenness:
        getInlet<gaze>(id_)._recorder = std::make_unique<std::thread>(&LSL_streamer::recorderThreadFunc<gaze>, this, id_);
        break;
    case Titta::Stream::EyeImage:
        break;
    case Titta::Stream::ExtSignal:
        getInlet<gaze>(id_)._recorder = std::make_unique<std::thread>(&LSL_streamer::recorderThreadFunc<extSignal>, this, id_);
        break;
    case Titta::Stream::TimeSync:
        getInlet<gaze>(id_)._recorder = std::make_unique<std::thread>(&LSL_streamer::recorderThreadFunc<timeSync>, this, id_);
        break;
    case Titta::Stream::Positioning:
        getInlet<gaze>(id_)._recorder = std::make_unique<std::thread>(&LSL_streamer::recorderThreadFunc<positioning>, this, id_);
        break;
    }
}

bool LSL_streamer::isListening(const uint32_t id_) const
{
    auto& inlet = getAllInletsVariant(id_);
    return getWorkerThread(inlet) && getWorkerThreadStopFlag(inlet);
}

template <typename DataType>
void LSL_streamer::recorderThreadFunc(const uint32_t id_)
{
    using data_t = LSLChannelFormatToCppType_t<LSLInletTypeToChannelFormat_v<DataType>>;
    constexpr size_t numElem = LSLInletTypeNumSamples_v<DataType>;
    using array_t = data_t[numElem];
    auto& inlet = getInlet<DataType>(id_);
    while (!inlet._recorder_should_stop)
    {
        array_t sample = { 0 };
        auto remoteT = inlet._lsl_inlet.pull_sample<data_t,numElem>(sample, 0.1);
        if (remoteT <= 0.)
            continue;
        auto tCorr = inlet._lsl_inlet.time_correction(0);
        // now parse into type
        if constexpr (std::is_same_v<DataType, gaze>)
        {
            data_t* ptr = sample;
            inlet._buffer.emplace_back(LSL_streamer::gaze{
                {
                    {   // left eye
                        {   // gazePoint
                            {   // position_on_display_area
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            {   // position_in_user_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // pupilData
                            static_cast<float>(*ptr++),
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // gazeOrigin
                            {   // position_in_user_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            {   // position_in_track_box_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // eyeOpenness
                            static_cast<float>(*ptr++),
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                    },
                    // right eye
                    {
                        {   // gazePoint
                            {   // position_on_display_area
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            {   // position_in_user_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // pupilData
                            static_cast<float>(*ptr++),
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // gazeOrigin
                            {   // position_in_user_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            {   // position_in_track_box_coordinates
                                static_cast<float>(*ptr++), static_cast<float>(*ptr++), static_cast<float>(*ptr++)
                            },
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                        {   // eyeOpenness
                            static_cast<float>(*ptr++),
                            *ptr == 1. ? TOBII_RESEARCH_VALIDITY_VALID : TOBII_RESEARCH_VALIDITY_INVALID,
                            *ptr == 1.
                        },
                    },
                    // device time
                    timeStampSecondsToUs(*ptr),
                    // system timestamp, transmitted as remote time
                    timeStampSecondsToUs(remoteT),
                },
            timeStampSecondsToUs(remoteT),
            timeStampSecondsToUs(remoteT + tCorr)
            });
        }
        else if constexpr (std::is_same_v<DataType, LSL_streamer::eyeImage>)
        {

        }
        else if constexpr (std::is_same_v<DataType, LSL_streamer::extSignal>)
        {
            data_t* ptr = sample;
            inlet._buffer.emplace_back(LSL_streamer::extSignal{
                {
                    *ptr++, *ptr++, static_cast<uint32_t>(*ptr++), *ptr==TOBII_RESEARCH_EXTERNAL_SIGNAL_VALUE_CHANGED? TOBII_RESEARCH_EXTERNAL_SIGNAL_VALUE_CHANGED: *ptr == TOBII_RESEARCH_EXTERNAL_SIGNAL_INITIAL_VALUE? TOBII_RESEARCH_EXTERNAL_SIGNAL_INITIAL_VALUE: TOBII_RESEARCH_EXTERNAL_SIGNAL_CONNECTION_RESTORED
                },
                timeStampSecondsToUs(remoteT),
                timeStampSecondsToUs(remoteT + tCorr)
            });
        }
        else if constexpr (std::is_same_v<DataType, LSL_streamer::timeSync>)
        {
            data_t* ptr = sample;
            inlet._buffer.emplace_back(LSL_streamer::timeSync{
                {
                    *ptr++, *ptr++, *ptr
                },
                timeStampSecondsToUs(remoteT),
                timeStampSecondsToUs(remoteT + tCorr)
            });
        }
        else if constexpr (std::is_same_v<DataType, LSL_streamer::positioning>)
        {
            data_t* ptr = sample;
            inlet._buffer.emplace_back(LSL_streamer::positioning{
                {
                    // left eye
                    {
                        {*ptr++, *ptr++, *ptr++},
                        *ptr++==1.f ? TOBII_RESEARCH_VALIDITY_VALID: TOBII_RESEARCH_VALIDITY_INVALID
                    },
                    // right eye
                    {
                        {*ptr++, *ptr++, *ptr++},
                        *ptr==1.f ? TOBII_RESEARCH_VALIDITY_VALID: TOBII_RESEARCH_VALIDITY_INVALID
                    }
                },
                timeStampSecondsToUs(remoteT),
                timeStampSecondsToUs(remoteT + tCorr)
            });
        }
    }
}


template <typename DataType>
std::vector<DataType> LSL_streamer::consumeN(const uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_)
{
    // deal with default arguments
    const auto N    = NSamp_.value_or(defaults::consumeNSamp);
    const auto side = side_ .value_or(defaults::consumeSide);

    auto& inlet = getInlet<DataType>(id_);
    auto l      = lockForWriting(inlet);  // NB: if C++ std gains upgrade_lock, replace this with upgrade lock that is converted to unique lock only after range is determined
    auto& buf   = getBuffer(inlet);

    auto [startIt, endIt] = getIteratorsFromSampleAndSide(buf, N, side);
    return consumeFromVec(buf, startIt, endIt);
}
template <typename DataType>
std::vector<DataType> LSL_streamer::consumeTimeRange(const uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_)
{
    // deal with default arguments
    const auto timeStart        = timeStart_      .value_or(defaults::consumeTimeRangeStart);
    const auto timeEnd          = timeEnd_        .value_or(defaults::consumeTimeRangeEnd);
    const auto timeIsLocalTime  = timeIsLocalTime_.value_or(defaults::timeIsLocalTime);

    auto& inlet = getInlet<DataType>(id_);
    auto l      = lockForWriting(inlet);  // NB: if C++ std gains upgrade_lock, replace this with upgrade lock that is converted to unique lock only after range is determined
    auto& buf   = getBuffer(inlet);

    auto [startIt, endIt, whole] = getIteratorsFromTimeRange(buf, timeStart, timeEnd, timeIsLocalTime);
    return consumeFromVec(buf, startIt, endIt);
}

template <typename DataType>
std::vector<DataType> LSL_streamer::peekN(const uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_)
{
    // deal with default arguments
    const auto N    = NSamp_.value_or(defaults::peekNSamp);
    const auto side = side_ .value_or(defaults::peekSide);

    auto& inlet = getInlet<DataType>(id_);
    auto l      = lockForReading(inlet);
    auto& buf   = getBuffer(inlet);

    auto [startIt, endIt] = getIteratorsFromSampleAndSide(buf, N, side);
    return peekFromVec(buf, startIt, endIt);
}
template <typename DataType>
std::vector<DataType> LSL_streamer::peekTimeRange(const uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_)
{
    // deal with default arguments
    auto timeStart       = timeStart_      .value_or(defaults::peekTimeRangeStart);
    auto timeEnd         = timeEnd_        .value_or(defaults::peekTimeRangeEnd);
    auto timeIsLocalTime = timeIsLocalTime_.value_or(defaults::timeIsLocalTime);

    auto& inlet     = getInlet<DataType>(id_);
    auto l          = lockForReading(inlet);
    auto& buf       = getBuffer(inlet);

    auto [startIt, endIt, whole] = getIteratorsFromTimeRange(buf, timeStart, timeEnd, timeIsLocalTime);
    return peekFromVec(buf, startIt, endIt);
}

void LSL_streamer::clear(const uint32_t id_)
{
    // visit with generic lambda so we get the inlet, lock and cal clear() on its buffer
    const auto stream = getInletType(id_);
    if (stream == Titta::Stream::Positioning)
    {
        auto& inlet = getInlet<positioning>(id_);
        auto l      = lockForWriting(inlet);    // NB: if C++ std gains upgrade_lock, replace this with upgrade lock that is converted to unique lock only after range is determined
        auto& buf   = getBuffer(inlet);
        if (std::empty(buf))
            return;
        buf.clear();
    }
    else
        clearTimeRange(id_);
}
void LSL_streamer::clearTimeRange(const uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_)
{
    // deal with default arguments
    const auto timeStart        = timeStart_      .value_or(defaults::clearTimeRangeStart);
    const auto timeEnd          = timeEnd_        .value_or(defaults::clearTimeRangeEnd);
    const auto timeIsLocalTime  = timeIsLocalTime_.value_or(defaults::timeIsLocalTime);

    // visit with templated lambda that allows us to get the data type, then
    // check if type is positioning, error, else forward. May need to split in two
    // overloaded lambdas actually, first for positioning, then templated generic
    switch (getInletType(id_))
    {
        case Titta::Stream::Gaze:
        case Titta::Stream::EyeOpenness:
            clearVec(getInlet<LSL_streamer::gaze>(id_), timeStart, timeEnd, timeIsLocalTime);
            break;
        case Titta::Stream::EyeImage:
            clearVec(getInlet<LSL_streamer::eyeImage>(id_), timeStart, timeEnd, timeIsLocalTime);
            break;
        case Titta::Stream::ExtSignal:
            clearVec(getInlet<LSL_streamer::extSignal>(id_), timeStart, timeEnd, timeIsLocalTime);
            break;
        case Titta::Stream::TimeSync:
            clearVec(getInlet<LSL_streamer::timeSync>(id_), timeStart, timeEnd, timeIsLocalTime);
            break;
        case Titta::Stream::Positioning:
            DoExitWithMsg("Titta::cpp::clearTimeRange: not supported for the positioning stream.");
            break;
    }
}

void LSL_streamer::stopListening(const uint32_t id_, std::optional<bool> clearBuffer_)
{
    // deal with default arguments
    const auto clearBuffer = clearBuffer_.value_or(defaults::stopBufferEmpties);

    auto& inlet = getAllInletsVariant(id_);
    auto& lsl_inlet = getLSLInlet(inlet);

    // stop thread
    setWorkerThreadStopFlag(inlet);
    getWorkerThread(inlet)->join();

    // close stream
    lsl_inlet.close_stream();

    // flush to be sure there's nothing stale left in LSL's buffers that would appear when we restart
    lsl_inlet.flush();

    // clean up if wanted
    if (clearBuffer)
        clear(id_);
}

void LSL_streamer::deleteListener(const uint32_t id_)
{
    stopListening(id_);
    // stop time syncer

    // delete entry to clean it all up
    _inStreams.erase(id_);
}

// gaze data (including eye openness), instantiate templated functions
template std::vector<LSL_streamer::gaze> LSL_streamer::consumeN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::gaze> LSL_streamer::consumeTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);
template std::vector<LSL_streamer::gaze> LSL_streamer::peekN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::gaze> LSL_streamer::peekTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);

// eye images, instantiate templated functions
template std::vector<LSL_streamer::eyeImage> LSL_streamer::consumeN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::eyeImage> LSL_streamer::consumeTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);
template std::vector<LSL_streamer::eyeImage> LSL_streamer::peekN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::eyeImage> LSL_streamer::peekTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);

// external signals, instantiate templated functions
template std::vector<LSL_streamer::extSignal> LSL_streamer::consumeN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::extSignal> LSL_streamer::consumeTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);
template std::vector<LSL_streamer::extSignal> LSL_streamer::peekN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::extSignal> LSL_streamer::peekTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);

// time sync data, instantiate templated functions
template std::vector<LSL_streamer::timeSync> LSL_streamer::consumeN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::timeSync> LSL_streamer::consumeTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);
template std::vector<LSL_streamer::timeSync> LSL_streamer::peekN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
template std::vector<LSL_streamer::timeSync> LSL_streamer::peekTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_, std::optional<bool> timeIsLocalTime_);

// positioning data, instantiate templated functions
// NB: positioning data does not have timestamps, so the Time Range version of the below functions are not defined for the positioning stream
template std::vector<LSL_streamer::positioning> LSL_streamer::consumeN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
//template std::vector<LSL_streamer::positioning> LSL_streamer::consumeTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_);
template std::vector<LSL_streamer::positioning> LSL_streamer::peekN(uint32_t id_, std::optional<size_t> NSamp_, std::optional<Titta::BufferSide> side_);
//template std::vector<LSL_streamer::positioning> LSL_streamer::peekTimeRange(uint32_t id_, std::optional<int64_t> timeStart_, std::optional<int64_t> timeEnd_);
