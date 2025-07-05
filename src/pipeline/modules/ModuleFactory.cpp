#include "ModuleFactory.hpp"
#include "helper/logging.hpp"

void ModuleFactory::registerModule(const ModuleName& moduleName, CreateModuleFunction createFunction) {
    LOG_DEBUG("Registering module: {}", moduleName);
    m_moduleCreatorsMap[moduleName] = createFunction;
}

void ModuleFactory::unregisterModule(const ModuleName& moduleName) {
    LOG_DEBUG("Unregistering module: {}", moduleName);
    m_moduleCreatorsMap.erase(moduleName);
}

bool ModuleFactory::isModuleRegistered(const ModuleName& moduleName) const {
    return m_moduleCreatorsMap.find(moduleName) != m_moduleCreatorsMap.end();
}

std::unique_ptr<ModuleBase> ModuleFactory::CreateModule(const ModuleName& moduleName) {
    auto it = m_moduleCreatorsMap.find(moduleName);
    if (it != m_moduleCreatorsMap.end()) {
        return it->second();
    }
    return nullptr;
}
