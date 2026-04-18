#include "core/config.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>

using json = nlohmann::json;

// ── RegionConfig JSON ──

void to_json(json& j, const RegionConfig& r) {
    j = json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
}

void from_json(const json& j, RegionConfig& r) {
    if (j.contains("x")) j.at("x").get_to(r.x);
    if (j.contains("y")) j.at("y").get_to(r.y);
    if (j.contains("w")) j.at("w").get_to(r.w);
    if (j.contains("h")) j.at("h").get_to(r.h);
}

// ── AppConfig ──

json AppConfig::to_json() const {
    json j;
    j["target_envs"]         = target_envs;
    j["unwanted_debuffs"]    = unwanted_debuffs;
    j["wanted_buffs"]        = wanted_buffs;
    j["min_confirm_rounds"]  = min_confirm_rounds;
    j["max_confirm_attempts"]= max_confirm_attempts;
    j["env_region"]          = env_region;
    j["debuff_region"]       = debuff_region;
    j["show_debug_overlay"]  = show_debug_overlay;
    j["stop_hotkey"]         = stop_hotkey;
    return j;
}

AppConfig AppConfig::from_json(const json& j) {
    AppConfig cfg;
    if (j.contains("target_envs"))
        cfg.target_envs = j["target_envs"].get<std::vector<std::string>>();
    if (j.contains("unwanted_debuffs"))
        cfg.unwanted_debuffs = j["unwanted_debuffs"].get<std::vector<std::string>>();
    if (j.contains("wanted_buffs"))
        cfg.wanted_buffs = j["wanted_buffs"].get<std::vector<std::string>>();
    if (j.contains("min_confirm_rounds"))
        cfg.min_confirm_rounds = j["min_confirm_rounds"].get<int>();
    if (j.contains("max_confirm_attempts"))
        cfg.max_confirm_attempts = j["max_confirm_attempts"].get<int>();
    if (j.contains("env_region"))
        cfg.env_region = j["env_region"].get<RegionConfig>();
    if (j.contains("debuff_region"))
        cfg.debuff_region = j["debuff_region"].get<RegionConfig>();
    if (j.contains("show_debug_overlay"))
        cfg.show_debug_overlay = j["show_debug_overlay"].get<bool>();
    if (j.contains("stop_hotkey"))
        cfg.stop_hotkey = j["stop_hotkey"].get<std::string>();
    return cfg;
}

void AppConfig::save(const std::string& path) const {
    std::ofstream ofs(path);
    if (!ofs) {
        std::cerr << "[config] 无法写入配置文件: " << path << std::endl;
        return;
    }
    ofs << to_json().dump(2);
    std::cout << "[config] 配置已保存到 " << path << std::endl;
}

AppConfig AppConfig::load(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        std::cout << "[config] 配置文件不存在: " << path << "，使用默认配置" << std::endl;
        return AppConfig{};
    }
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[config] 无法读取配置文件: " << path << std::endl;
        return AppConfig{};
    }
    try {
        json j = json::parse(ifs);
        std::cout << "[config] 已从 " << path << " 加载配置" << std::endl;
        return AppConfig::from_json(j);
    } catch (const std::exception& e) {
        std::cerr << "[config] 解析配置文件失败: " << e.what() << std::endl;
        return AppConfig{};
    }
}

std::vector<std::string> load_name_list(const std::string& path) {
    std::vector<std::string> names;
    if (!std::filesystem::exists(path)) {
        std::cerr << "[config] 名称列表文件不存在: " << path << std::endl;
        return names;
    }
    std::ifstream ifs(path);
    if (!ifs) return names;
    std::string line;
    while (std::getline(ifs, line)) {
        // trim
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);
        if (!trimmed.empty()) {
            names.push_back(trimmed);
        }
    }
    return names;
}
