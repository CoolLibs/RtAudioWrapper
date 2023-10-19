#include "Player.hpp"
#include <rtaudio/RtAudio.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace RtAudioW {

static constexpr size_t output_channels_count = 2;

static auto is_API_available() -> bool
{
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    return apis[0] != RtAudio::Api::RTAUDIO_DUMMY;
}

Player::Player()
{
    update_device_if_necessary();
#if !defined(DISABLE_ASSERTING_AVAILABLE_API_AND_DEVICE)
    assert(is_API_available());
    // assert(is_device_available()); // TODO(Audio) Device should not be an assert, but a warning
#endif
}

void Player::update_device_if_necessary()
{
    auto const id = _backend.getDefaultOutputDevice();
    if (id == _current_output_device_id)
        return;

    _current_output_device_id = id;
    recreate_stream_adapted_to_current_audio_data();
}

auto Player::has_audio_data() const -> bool
{
    return !_data.samples.empty();
}

auto Player::has_device() const -> bool
{
    return _current_output_device_id != 0;
}

void Player::recreate_stream_adapted_to_current_audio_data()
{
    if (_backend.isStreamOpen())
        _backend.closeStream();

    if (!has_audio_data()
        || !has_device())
        return;

    RtAudio::StreamParameters _parameters;
    _parameters.deviceId     = _current_output_device_id;
    _parameters.firstChannel = 0;
    _parameters.nChannels    = output_channels_count;
    // _backend.getDeviceInfo(_parameters.deviceId).nativeFormats; // TODO(Audio) error if doesn't support FLOAT32

    unsigned int nb_frames_per_callback{128 /*256*/}; // TODO(Audio) Try setting to 0?
    // TODO(Audio) Try settings the RTAUDIO_MINIMIZE_LATENCY flag?
    // RtAudioStreamFlags
    // TODO(Audio) Fix grésillement when changing volume with headphones
    // TODO(Audio) TODO(Philippe) Resampler l'audio pour qu'il match le preferredSampleRate du device
    // auto const sr = _backend.getDeviceInfo(_parameters.deviceId).sampleRates;
    // std::cout << _sample_rate << '\n';
    _backend.openStream(
        &_parameters,
        nullptr,
        RTAUDIO_FLOAT32,
        // _backend.getDeviceInfo(_parameters.deviceId).preferredSampleRate,
        _data.sample_rate, // TODO(Audio) Error when the device does not support this sample_rate
        &nb_frames_per_callback,
        &audio_callback, // TODO(Audio) Can't move a Player because of the Callback
        this
    );

    if (_play_has_been_requested)
        _backend.startStream();
}

void Player::set_audio_data(AudioData data)
{
    _data = std::move(data);
    recreate_stream_adapted_to_current_audio_data();
}

void Player::reset_audio_data()
{
    set_audio_data({});
}

auto Player::play() -> RtAudioErrorType
{
    _play_has_been_requested = true;
    if (!_backend.isStreamOpen()      // We will start playing when we open the stream, i.e. when we receive audio data
        || _backend.isStreamRunning() // The stream is already started, no need to do anything
    )
    {
        return RtAudioErrorType::RTAUDIO_NO_ERROR;
    }
    return _backend.startStream();
}

auto Player::pause() -> RtAudioErrorType
{
    _play_has_been_requested = false;
    if (!_backend.isStreamRunning() /* The stream is already inactive, no need to do anything */)
        return RtAudioErrorType::RTAUDIO_NO_ERROR;
    return _backend.stopStream();
}

void Player::set_time(float time_in_seconds)
{
    // TODO(Audio) Store the desired time in seconds too, so that if we switch to an audio data with a different sample rate, we can adjust the _next_frame_to_play to make it match the actual time in seconds.
    _next_frame_to_play = static_cast<size_t>(
        static_cast<float>(_data.sample_rate)
            * time_in_seconds
    );
}

auto Player::get_time() const -> float
{
    // TODO(Audio) return the stored desired time and handle when no data (sample_rate == 0)
    return static_cast<float>(_next_frame_to_play)
           / static_cast<float>(_data.sample_rate);
}

auto audio_callback(void* output_buffer, void* /* input_buffer */, unsigned int frames_count, double /* stream_time */, RtAudioStreamStatus /* status */, void* user_data) -> int
{
    auto* out_buffer = static_cast<float*>(output_buffer);
    auto& player     = *static_cast<Player*>(user_data);

    for (size_t frame_idx = 0; frame_idx < frames_count; frame_idx++)
    {
        for (size_t channel_idx = 0; channel_idx < output_channels_count; ++channel_idx)
        {
            out_buffer[frame_idx * output_channels_count + channel_idx] = // NOLINT(*pointer-arithmetic)
                player.sample(player._next_frame_to_play, channel_idx);
        }
        player._next_frame_to_play++;
    }

    return 0;
}

auto Player::sample(size_t frame_index, size_t channel_index) -> float
{
    if (_properties.is_muted)
        return 0.f;

    auto const sample_index = frame_index * _data.channels_count
                              + channel_index % _data.channels_count;
    if (sample_index >= _data.samples.size() && !_properties.does_loop)
        return 0.f;

    return _data.samples[sample_index % _data.samples.size()]
           * _properties.volume;
}

auto player() -> Player&
{
    static auto instance = Player{};
    return instance;
}

} // namespace RtAudioW
