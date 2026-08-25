#include "scenarios.hpp"

#include <memory>

namespace mud {
namespace {
class TribePlaceholder final : public Scenario {
public:
    std::string id() const override { return "tribe"; }
    std::string title() const override { return "燧火纪：部落黎明（待实现）"; }
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
std::unique_ptr<Scenario> makeTribeScenario() { return std::make_unique<TribePlaceholder>(); }
} // namespace mud

