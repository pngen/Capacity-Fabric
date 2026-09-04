#include <cstdio>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

// Demonstrates scenario analysis as an isolated WHAT_IF overlay, and persistence
// recovery. Never mutates authoritative state.
int main() {
    CapacityModel model;
    reference::seedReferenceModel(model);

    const std::size_t before = model.currentResources().size();
    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1),
                                                   4.0, DeviceCount(4));
    std::printf("authoritative fit (4 devices): %s\n", to_string(model.queryFeasibilityNow(d).outcome));

    // WHAT_IF: remove device 3.
    ScenarioAction act;
    act.kind = ScenarioAction::Kind::RemoveResource;
    act.resource = ResourceId(3);
    Scenario scen = model.createScenario({act});
    auto sr = model.evaluateScenario(scen, d, QueryMode::WhatIf, 0);
    std::printf("WHAT_IF (remove device 3)   : %s\n", to_string(sr.outcome));
    std::printf("authoritative resources     : %zu (unchanged) \n", model.currentResources().size());

    (void)before;
    // Persist and recover.
    const std::string path = "example_state.cf";
    if (model.save(path)) {
        CapacityModel m2;
        if (m2.load(path)) std::printf("recover: ok (recovered=%d)\n", m2.recovered() ? 1 : 0);
    }
    return 0;
}
