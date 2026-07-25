#pragma once

#include <string>
#include <map>
#include <memory>

namespace sky::utils {

    /**
     * @brief Supported configuration types
     */
    enum class ConfigType {
        STRING,
        INT,
        DOUBLE,
        BOOL
    };

    /**
     * @brief Configuration value using union for compatibility
     */
    struct ConfigValue {
        ConfigType type;
        union {
            std::string* string_value;
            int int_value;
            double double_value;
            bool bool_value;
        };

        ConfigValue() : type(ConfigType::STRING), string_value(nullptr) {}

        ConfigValue(const std::string& value) : type(ConfigType::STRING) {
            string_value = new std::string(value);
        }

        ConfigValue(int value) : type(ConfigType::INT), int_value(value) {}

        ConfigValue(double value) : type(ConfigType::DOUBLE), double_value(value) {}

        ConfigValue(bool value) : type(ConfigType::BOOL), bool_value(value) {}

        ~ConfigValue() {
            if (type == ConfigType::STRING && string_value) {
                delete string_value;
            }
        }

        // Copy constructor
        ConfigValue(const ConfigValue& other) : type(other.type) {
            if (type == ConfigType::STRING && other.string_value) {
                string_value = new std::string(*other.string_value);
            } else {
                int_value = other.int_value;
            }
        }

        // Assignment operator
        ConfigValue& operator=(const ConfigValue& other) {
            if (this != &other) {
                if (type == ConfigType::STRING && string_value) {
                    delete string_value;
                }

                type = other.type;
                if (type == ConfigType::STRING && other.string_value) {
                    string_value = new std::string(*other.string_value);
                } else {
                    int_value = other.int_value;
                }
            }
            return *this;
        }
    };

    /**
     * @brief Abstract interface for the configuration system
     */
    class IConfigManager {
    public:
        virtual ~IConfigManager() = default;

        // Value management
        virtual void set_value(const std::string& key, const ConfigValue& value) = 0;
        virtual ConfigValue get_value(const std::string& key, const ConfigValue& default_value = ConfigValue()) const = 0;
        virtual bool has_value(const std::string& key) const = 0;
        virtual void remove_value(const std::string& key) = 0;

        // Typed convenience methods
        virtual std::string get_string(const std::string& key, const std::string& default_value = "") const = 0;
        virtual int get_int(const std::string& key, int default_value = 0) const = 0;
        virtual double get_double(const std::string& key, double default_value = 0.0) const = 0;
        virtual bool get_bool(const std::string& key, bool default_value = false) const = 0;

        // Persistence
        virtual bool load_from_file(const std::string& filename) = 0;
        virtual bool save_to_file(const std::string& filename) const = 0;
        virtual bool load_from_json(const std::string& json_content) = 0;
        virtual std::string save_to_json() const = 0;

        // Section management
        virtual void set_section(const std::string& section) = 0;
        virtual std::string get_section() const = 0;
        virtual void clear_section() = 0;
    };

    /**
     * @brief Factory for creating configuration managers
     */
    class ConfigManagerFactory {
    public:
        static std::shared_ptr<IConfigManager> create_memory_config();
        static std::shared_ptr<IConfigManager> create_file_config(const std::string& filename);
        static std::shared_ptr<IConfigManager> create_json_config(const std::string& filename);
    };

    // Global configuration
    extern std::shared_ptr<IConfigManager> g_config;

} // namespace sky::utils
