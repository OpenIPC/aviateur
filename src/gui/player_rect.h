#pragma once

#include "../player/video_player.h"
#include "app.h"
#include "tip_label.h"

class SignalBar;
class GstDecoder;

class PlayerRect final : public revector::TextureRect {
public:
    std::shared_ptr<VideoPlayer> player_;

    // Preserved for reuse when restarting decoder.
    std::string play_url_;
    std::mutex play_mutex_;

    bool playing_ = false;

    bool force_software_decoding = false;

    std::shared_ptr<revector::VectorImage> logo_;
    std::shared_ptr<revector::RenderImage> render_image_;

    std::shared_ptr<TipLabel> tip_label_;

    bool is_recording = false;

    std::chrono::time_point<std::chrono::steady_clock> record_start_time;

    std::shared_ptr<revector::Timer> rx_status_update_timer;

    std::shared_ptr<revector::CollapseContainer> collapse_panel_;

    std::shared_ptr<revector::HBoxContainer> hud_container_;

    std::shared_ptr<revector::Label> record_status_label_;

    std::shared_ptr<revector::Label> bitrate_label_;

    std::shared_ptr<revector::Label> decoder_label_;

    std::shared_ptr<revector::Label> pl_label_;

    std::shared_ptr<revector::Label> fec_label_;

    std::vector<std::shared_ptr<SignalBar>> lq_bars_;

    std::shared_ptr<revector::Label> video_info_label_;

    std::shared_ptr<revector::Label> render_fps_label_;

    std::shared_ptr<revector::Button> video_stabilization_button_;
    std::shared_ptr<revector::Button> low_light_enhancement_button_;

    std::shared_ptr<revector::HBoxContainer> top_control_container;
    std::shared_ptr<revector::Button> fullscreen_button_;

    std::shared_ptr<revector::Button> record_button_;

    // Record when the signal had been lost.
    std::chrono::time_point<std::chrono::steady_clock> signal_lost_time_;

    void show_red_tip(std::string tip);

    void show_green_tip(std::string tip);

    void custom_input(revector::InputEvent &event) override;

    void custom_ready() override;

    void custom_update(double dt) override;

    void custom_draw() override;

    void start_playing(const std::string &url);

    void stop_playing();
};
