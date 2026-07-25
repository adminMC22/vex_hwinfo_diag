#pragma once

#include <memory>
#include <string>
#include "../driver/idriver.hpp"
#include "../game/vgk_system.hpp"

namespace vex::core {

    /**
     * @brief Abstract interface for the main application
     *
     * This interface defines the contract for the main application,
     * allowing different implementations and facilitating testing.
     */
    class IApplication {
    public:
        virtual ~IApplication() = default;

        // Application lifecycle
        virtual bool initialize() = 0;
        virtual void run() = 0;
        virtual void shutdown() = 0;
        virtual bool is_running() const = 0;

        // Configuration
        virtual void set_target_process(const std::wstring& process_name) = 0;
        virtual void set_configuration(const std::string& config_name, const std::string& value) = 0;

        // Execution control
        virtual void pause() = 0;
        virtual void resume() = 0;
        virtual void stop() = 0;
    };

} // namespace vex::core
