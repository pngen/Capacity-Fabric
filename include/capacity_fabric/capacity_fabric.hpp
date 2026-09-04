#pragma once

// Capacity Fabric — public umbrella header.
//
// Modelling, forecasting, and explaining usable AI-infrastructure capacity.

#include "capacity_fabric/capacity/capacity.hpp"
#include "capacity_fabric/capacity/dimension.hpp"
#include "capacity_fabric/core/hash.hpp"
#include "capacity_fabric/core/identities.hpp"
#include "capacity_fabric/core/provenance.hpp"
#include "capacity_fabric/core/strong_id.hpp"
#include "capacity_fabric/core/units.hpp"
#include "capacity_fabric/demand/demand.hpp"
#include "capacity_fabric/forecast/forecast.hpp"
#include "capacity_fabric/fragmentation/fragmentation.hpp"
#include "capacity_fabric/headroom/headroom.hpp"
#include "capacity_fabric/model/freshness.hpp"
#include "capacity_fabric/model/model.hpp"
#include "capacity_fabric/model/view.hpp"
#include "capacity_fabric/persistence/binary.hpp"
#include "capacity_fabric/persistence/store.hpp"
#include "capacity_fabric/query/earliest_fit.hpp"
#include "capacity_fabric/query/feasibility.hpp"
#include "capacity_fabric/query/outcome.hpp"
#include "capacity_fabric/resource/resource.hpp"
#include "capacity_fabric/scenario/scenario.hpp"
#include "capacity_fabric/timeline/timeline.hpp"
