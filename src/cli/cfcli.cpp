#include <iostream>
#include <string>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"

using namespace capacity_fabric;

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    CapacityModel model;
    reference::seedReferenceModel(model);

    std::cout << "Capacity Fabric cfcli (inspection tool)\n";
    std::cout << "Adapter: " << reference::description() << "\n\n";
    std::cout << model.describe() << "\n\n";

    std::cout << "--- Current resources ---\n";
    for (const auto& r : model.currentResources()) {
        std::cout << "  resource " << r.id << " gen " << r.generation
                  << " dev " << r.deviceId << " vram " << (r.nominal.vram().value() >> 20)
                  << " MiB free " << (r.free.vram().value() >> 20) << " MiB"
                  << " health " << to_string(r.health) << "\n";
    }
    std::cout << "\n--- Generations ---\n";
    for (const auto& [id, g] : model.resourceGenerations()) {
        std::cout << "  resource " << id << " -> gen " << g << "\n";
    }

    const WorkloadDemand d = reference::makeDemand(WorkloadDemandId(100), WorkloadDemandGeneration(1),
                                                   3.0, DeviceCount(3));
    std::cout << "\n--- Feasibility (demand: 3 accelerators) ---\n";
    auto r = model.queryFeasibilityNow(d);
    std::cout << "  outcome: " << to_string(r.outcome) << "\n";
    if (!r.bottleneck.message.empty()) std::cout << "  reason: " << r.bottleneck.message << "\n";

    std::cout << "\n--- Earliest fit ---\n";
    auto ef = model.queryEarliestFit(d, QueryMode::Expected, 0, timeFromMillis(10000),
                                     timeFromMillis(60000));
    std::cout << "  found: " << (ef.found ? "yes" : "no");
    if (ef.found) std::cout << " at t=" << toMillis(ef.start) << "ms";
    std::cout << "\n";

    std::cout << "\n--- Headroom (SLO 0.6) ---\n";
    WorkloadDemand d2 = d;
    d2.sloHeadroomRequirement = CapacityFraction(0.6);
    auto h = model.queryHeadroom(d2, QueryMode::Conservative);
    std::cout << "  capacityConstrained: " << (h.capacityConstrained ? "yes" : "no")
              << " conservative accel: " << h.conservative.amount(Dimension::AcceleratorCount) << "\n";

    std::cout << "\n--- Scenario: remove device 3 ---\n";
    ScenarioAction act;
    act.kind = ScenarioAction::Kind::RemoveResource;
    act.resource = ResourceId(3);
    auto scen = model.createScenario({act});
    auto sr = model.evaluateScenario(scen, d, QueryMode::Expected, 0);
    std::cout << "  WHAT_IF outcome: " << to_string(sr.outcome) << "\n";
    if (!sr.bottleneck.message.empty()) std::cout << "  reason: " << sr.bottleneck.message << "\n";

    const std::string path = "cfcli_state.cf";
    const bool ok = model.save(path);
    std::cout << "\n--- Persistence ---\n  save: " << (ok ? "ok" : "failed") << "\n";
    CapacityModel model2;
    const bool ok2 = model2.load(path);
    std::cout << "  load: " << (ok2 ? "ok" : "failed") << " recovered=" << (model2.recovered() ? "yes" : "no")
              << " digest=" << model.digest() << "\n";

    return 0;
}
