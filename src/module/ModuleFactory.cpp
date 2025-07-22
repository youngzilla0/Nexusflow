#include "nexusflow/Config.hpp"
#include "utils/logging.hpp" // Assuming you have a logger
#include <nexusflow/ModuleFactory.hpp>
#include <unordered_map>

namespace nexusflow {

// This is where the singleton instance is actually defined and created.
// It's guaranteed to be created only once in a thread-safe manner (since C++11).
ModuleFactory& ModuleFactory::GetInstance() {
    static ModuleFactory instance;
    return instance;
}

// Implementation of the non-template member function.
std::shared_ptr<Module> ModuleFactory::CreateModule(const std::string& className, const std::string& moduleName,
                                                    const Config& config) {
    // Find the creator function associated with the class name.
    auto it = m_creators.find(className);
    if (it == m_creators.end()) {
        // The class name was not found in our map of registered creators.
        LOG_ERROR("ModuleFactory Error: Attempted to create a module of unregistered class '{}'.", className);
        return nullptr;
    }

    // Call the stored creator lambda function.
    // The lambda will call the constructor: new T(moduleName)
    std::shared_ptr<Module> moduleInst = it->second(moduleName);

    if (moduleInst) {
        try {
            moduleInst->Configure(config);
        } catch (const std::exception& e) {
            // Log an error if configuration fails
            LOG_ERROR("Failed to configure module '{}' of class '{}': {}", moduleName, className, e.what());
            return nullptr;
        }
    }
    return moduleInst;
}

} // namespace nexusflow
