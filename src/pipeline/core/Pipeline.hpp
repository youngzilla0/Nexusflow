#ifndef PIPELINE_HPP_
#define PIPELINE_HPP_

#include "ErrorCode.hpp"
#include "ModuleBase.hpp"
#include "base/Graph.hpp"
#include <memory>
#include <string>
#include <unordered_map>
namespace pipeline_core {

class Pipeline {
public:
    static std::unique_ptr<Pipeline> makeByGraph(const Graph& graph);

    ErrorCode Init();

    ErrorCode Start();

    ErrorCode Stop();

    ErrorCode DeInit();

    // TODO: export FeedData api.

protected:
    std::shared_ptr<ModuleBase> getOrCreateModule(const std::string& name, const ModuleName& moduleName);

private:
    Pipeline() = default;

    std::unordered_map<std::string, std::shared_ptr<ModuleBase>> m_moduleMap;
};

}; // namespace pipeline_core

#endif