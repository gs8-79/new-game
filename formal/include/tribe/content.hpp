#pragma once

#include "tribe/formal_types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

struct LocationDefinition {
    LocationId id;
    std::string_view key;
    std::string_view chineseName;
    std::string_view description;
    std::vector<LocationId> neighbors;
};

struct BuildingDefinition {
    BuildingId id;
    std::string_view key;
    std::string_view chineseName;
    int woodCost;
    int stoneCost;
    int herbCost;
    std::string_view effect;
};

struct TechnologyDefinition {
    TechnologyId id;
    std::string_view key;
    std::string_view chineseName;
    int tier;
    std::optional<TechnologyId> prerequisite;
    std::string_view effect;
};

struct EventDefinition {
    EventId id;
    std::string_view key;
    std::string_view title;
    std::string_view description;
};

const std::array<LocationDefinition, kLocationCount>& locations();
const std::array<BuildingDefinition, kBuildingCount>& buildings();
const std::array<TechnologyDefinition, kTechnologyCount>& technologies();
const std::array<EventDefinition, kEventCount>& events();

const LocationDefinition& location(LocationId id);
const BuildingDefinition& building(BuildingId id);
const TechnologyDefinition& technology(TechnologyId id);
const EventDefinition& event(EventId id);

bool areAdjacent(LocationId left, LocationId right);
std::optional<LocationId> findLocation(std::string_view name);
std::optional<BuildingId> findBuilding(std::string_view name);
std::optional<TechnologyId> findTechnology(std::string_view name);
std::optional<FactionId> findFaction(std::string_view name);
std::optional<Tactic> findTactic(std::string_view name);
std::optional<Ending> findEnding(std::string_view name);

std::string factionName(FactionId faction);
std::string tacticName(Tactic tactic);

} // namespace tribe
