#include <chrono>
#include <random>

#include "signal_quality.h"

namespace {

std::string generate_random_string(size_t length) {
    const std::string characters = "abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, characters.size() - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += characters[distrib(gen)];
    }
    return result;
}

} // namespace

// Remove RSSI samples older than 1 second
void SignalQualityCalculator::cleanup_old_rssi_data() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - kAveragingWindow;

    // Erase-remove idiom for data older than cutoff
    m_rssis.erase(std::remove_if(m_rssis.begin(),
                                 m_rssis.end(),
                                 [&](const RssiEntry &entry) { return entry.timestamp < cutoff; }),
                  m_rssis.end());
}

void SignalQualityCalculator::cleanup_old_snr_data() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - kAveragingWindow;

    // Erase-remove idiom for data older than cutoff
    m_snrs.erase(
        std::remove_if(m_snrs.begin(), m_snrs.end(), [&](const SnrEntry &entry) { return entry.timestamp < cutoff; }),
        m_snrs.end());
}

void SignalQualityCalculator::cleanup_old_fec_data() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - kAveragingWindow;

    m_fec_data.erase(std::remove_if(m_fec_data.begin(),
                                    m_fec_data.end(),
                                    [&](const FecEntry &entry) { return entry.timestamp < cutoff; }),
                     m_fec_data.end());
}

void SignalQualityCalculator::add_rssi(uint8_t ant1, uint8_t ant2) {
    std::lock_guard lock(m_mutex);

    RssiEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.ant1 = ant1;
    entry.ant2 = ant2;
    m_rssis.push_back(entry);
}

void SignalQualityCalculator::add_snr(int8_t ant1, int8_t ant2) {
    std::lock_guard lock(m_mutex);

    SnrEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.ant1 = ant1;
    entry.ant2 = ant2;
    m_snrs.push_back(entry);
}

SignalQualityCalculator::SignalQuality SignalQualityCalculator::calculate_signal_quality() {
    SignalQuality ret;
    std::lock_guard lock(m_mutex);

    // Make sure we clean up old data first
    cleanup_old_rssi_data();
    cleanup_old_snr_data();
    cleanup_old_fec_data();

    float avg_rssi = get_avg(m_rssis);
    float avg_snr = get_avg(m_snrs);

    // Map the RSSI from range 10..80 to -1024..1024
    avg_rssi = map_range(avg_rssi, 0.f, 80.f, -1024.f, 1024.f);
    avg_rssi = std::max(-1024.f, std::min(1024.f, avg_rssi));

    // Return final clamped quality
    // formula: quality = avg_rssi - p_recovered * 5 - p_lost * 100
    // clamp between -1024 and 1024
    auto [p_recovered, p_lost, p_total] = get_accumulated_fec_data();

    float quality = avg_rssi; // - static_cast<float>(p_recovered) * 12.f - static_cast<float>(p_lost) * 40.f;
    quality = std::max(-1024.f, std::min(1024.f, quality));

    ret.lost_last_second = p_lost;
    ret.recovered_last_second = p_recovered;
    ret.total_last_second = p_total;

    ret.quality = quality;
    ret.snr = avg_snr;
    ret.idr_code = m_idr_code;

    return ret;
}

std::tuple<uint32_t, uint32_t, uint32_t> SignalQualityCalculator::get_accumulated_fec_data() const {
    uint32_t p_recovered = 0;
    uint32_t p_all = 0;
    uint32_t p_lost = 0;
    for (const auto &data : m_fec_data) {
        p_all += data.all;
        p_recovered += data.recovered;
        p_lost += data.lost;
    }

    return {p_recovered, p_lost, p_all};
}

void SignalQualityCalculator::add_fec_data(uint32_t p_all, uint32_t p_recovered, uint32_t p_lost) {
    std::lock_guard lock(m_mutex);

    FecEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.all = p_all;
    entry.recovered = p_recovered;
    entry.lost = p_lost;

    if (p_lost > 0) {
        m_idr_code = generate_random_string(4);
    }

    m_fec_data.push_back(entry);
}
