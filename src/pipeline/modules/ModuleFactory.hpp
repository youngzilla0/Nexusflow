#pragma once

#include "core/ModuleBase.hpp"
#include <functional>
#include <unordered_map>

class ModuleFactory {
public:
    using CreateModuleFunction = std::function<std::unique_ptr<ModuleBase>()>;

    static ModuleFactory& getInstance() {
        static ModuleFactory instance;
        return instance;
    }

    std::unique_ptr<ModuleBase> CreateModule(const ModuleName& moduleName);

public:
    void registerModule(const ModuleName& moduleName, CreateModuleFunction createFunction);

    void unregisterModule(const ModuleName& moduleName);

    bool isModuleRegistered(const ModuleName& moduleName) const;

private:
    ModuleFactory() = default;

    std::unordered_map<ModuleName, CreateModuleFunction> m_moduleCreatorsMap;
};

/**
 * @brief Registers a module with the ModuleFactory.
 *
 * This macro defines a static boolean variable. The initialization of this variable
 * triggers a lambda function that calls the registration method on the factory singleton.
 * This ensures the registration happens automatically at program startup.
 *
 * IMPORTANT: This macro must be called in the .cpp file of the module, NOT the .hpp file, to avoid multiple definition errors.
 *
 * @param moduleClassName The class name of the module to register (e.g., MockInputModule).
 */
#define REGISTER_MODULE(moduleClassName)                                                                                     \
    static bool registrar_##moduleClassName = []() {                                                                         \
        ModuleFactory::getInstance().registerModule(#moduleClassName, []() { return std::make_unique<moduleClassName>(); }); \
        return true;                                                                                                         \
    }();

/**
 * @brief Unregisters a module with the ModuleFactory.
 * This macro calls the unregisterModule method on the factory singleton.
 * @param moduleClassName The class name of the module to register (e.g., MockInputModule).
 */
#define UNREGISTER_MODULE(moduleClassName)                               \
    static bool unregistrar_##moduleClassName = []() {                   \
        ModuleFactory::getInstance().unregisterModule(#moduleClassName); \
        return true;                                                     \
    }();
