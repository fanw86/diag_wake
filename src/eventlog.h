#pragma once
#include <string>
#include <vector>

struct PowerEvent {
    std::string time;
    std::string provider;
    uint16_t eventId = 0;
    std::string description;
    uint8_t level = 0;
};

std::vector<PowerEvent> ReadRecentPowerEvents(int maxCount = 50);
