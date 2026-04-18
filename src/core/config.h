#pragma once
// config.h — JSON 配置管理模块

#include <string>
#include <vector>
#include <tuple>
#include <nlohmann/json.hpp>

struct RegionConfig {
    int x = 0, y = 0, w = 0, h = 0;
    std::tuple<int,int,int,int> as_tuple() const { return {x, y, w, h}; }
};

void to_json(nlohmann::json& j, const RegionConfig& r);
void from_json(const nlohmann::json& j, RegionConfig& r);

struct AppConfig {
    // 需要的投资策略
    std::vector<std::string> target_envs;

    // 不想要的 Debuff
    std::vector<std::string> unwanted_debuffs;

    // 需要的 Buff
    std::vector<std::string> wanted_buffs;

    // OCR 稳定扫描参数
    int min_confirm_rounds  = 5;
    int max_confirm_attempts = 25;

    // 投资策略识别区域
    RegionConfig env_region{0, 490, 0, 55};

    // Debuff 识别区域
    RegionConfig debuff_region{325, 1275, 1200, 65};

    // 是否显示识别 debug 框体
    bool show_debug_overlay = true;

    // 停止热键
    std::string stop_hotkey = "delete";

    nlohmann::json to_json() const;
    static AppConfig from_json(const nlohmann::json& j);

    void save(const std::string& path = "config.json") const;
    static AppConfig load(const std::string& path = "config.json");
};

// 从 txt 文件加载名称列表，每行一个
std::vector<std::string> load_name_list(const std::string& path);
