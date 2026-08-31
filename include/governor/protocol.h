#pragma once
#include <string>
#include <cstdint>
#include <stdexcept>
#include <sstream>
#include <arpa/inet.h>
#include <cstring>
#include <vector>

namespace governor {

constexpr const char* SOCKET_PATH = "/tmp/mem_governor.sock";

enum class MessageType {
    REGISTER,
    REPORT_SIZE,
    SHRINK,
    GROW,
    ACK,
    HEARTBEAT,
    UNKNOWN
};

struct Message {
    MessageType type{MessageType::UNKNOWN};
    int32_t pid{0};
    uint64_t target_bytes{0};
    uint64_t current_size_bytes{0};
    int32_t tier{0};

    std::string serialize() const {
        std::stringstream ss;
        ss << "{";
        ss << "\"type\":\"" << type_to_string(type) << "\",";
        ss << "\"pid\":" << pid << ",";
        ss << "\"target_bytes\":" << target_bytes << ",";
        ss << "\"current_size_bytes\":" << current_size_bytes << ",";
        ss << "\"tier\":" << tier;
        ss << "}";
        return ss.str();
    }

    static Message deserialize(const std::string& json) {
        Message msg;
        auto extract_string = [&](const std::string& key) -> std::string {
            size_t pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = json.find(":", pos);
            if (pos == std::string::npos) return "";
            size_t start = json.find_first_not_of(" \t\"", pos + 1);
            if (start == std::string::npos) return "";
            size_t end;
            if (json[start - 1] == '"') {
                end = json.find("\"", start);
            } else {
                end = json.find_first_of(",}", start);
            }
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
        };

        std::string type_str = extract_string("type");
        msg.type = string_to_type(type_str);
        
        std::string pid_str = extract_string("pid");
        if (!pid_str.empty()) msg.pid = std::stoi(pid_str);
        
        std::string tb_str = extract_string("target_bytes");
        if (!tb_str.empty()) msg.target_bytes = std::stoull(tb_str);
        
        std::string cb_str = extract_string("current_size_bytes");
        if (!cb_str.empty()) msg.current_size_bytes = std::stoull(cb_str);
        
        std::string tier_str = extract_string("tier");
        if (!tier_str.empty()) msg.tier = std::stoi(tier_str);

        return msg;
    }

    static std::string type_to_string(MessageType t) {
        switch(t) {
            case MessageType::REGISTER: return "REGISTER";
            case MessageType::REPORT_SIZE: return "REPORT_SIZE";
            case MessageType::SHRINK: return "SHRINK";
            case MessageType::GROW: return "GROW";
            case MessageType::ACK: return "ACK";
            case MessageType::HEARTBEAT: return "HEARTBEAT";
            default: return "UNKNOWN";
        }
    }

    static MessageType string_to_type(const std::string& s) {
        if (s == "REGISTER") return MessageType::REGISTER;
        if (s == "REPORT_SIZE") return MessageType::REPORT_SIZE;
        if (s == "SHRINK") return MessageType::SHRINK;
        if (s == "GROW") return MessageType::GROW;
        if (s == "ACK") return MessageType::ACK;
        if (s == "HEARTBEAT") return MessageType::HEARTBEAT;
        return MessageType::UNKNOWN;
    }
    
    // Frame the message: 4-byte length prefix (network byte order) + JSON payload
    static std::vector<uint8_t> frame(const Message& msg) {
        std::string payload = msg.serialize() + "\n";
        uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
        std::vector<uint8_t> buffer(sizeof(len) + payload.size());
        std::memcpy(buffer.data(), &len, sizeof(len));
        std::memcpy(buffer.data() + sizeof(len), payload.data(), payload.size());
        return buffer;
    }
};

} // namespace governor
