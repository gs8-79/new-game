#include "scenarios.hpp"

#include <memory>

namespace mud {
namespace {
class IslandPlaceholder final : public Scenario {
public:
    std::string id() const override { return "island"; }
    std::string title() const override { return "荒岛求生7日（待实现）"; }
    std::string intro() const override { return "场景正在实现。"; }
    std::string help() const override { return "暂无场景命令。"; }
    void reset() override { outcome_ = Outcome::Running; }
    CommandResult execute(const ParsedCommand&) override { return {}; }
    Outcome outcome() const override { return outcome_; }
    SaveFields saveFields() const override { return {}; }
    bool loadFields(const SaveFields& fields, std::string& error) override {
        return hasExactKeys(fields, {}, error);
    }
private:
    Outcome outcome_ = Outcome::Running;
};
} // namespace
std::unique_ptr<Scenario> makeIslandScenario() { return std::make_unique<IslandPlaceholder>(); }
} // namespace mud

