#include "test_util.hpp"

#include <fstream>
#include <vector>

#include "capacity_fabric/adapters/reference.hpp"
#include "capacity_fabric/model/model.hpp"
#include "capacity_fabric/persistence/store.hpp"

using namespace capacity_fabric;

static std::vector<uint8_t> readFile(const std::string& p) {
    std::vector<uint8_t> b;
    (void)readFileBytes(p, b);
    return b;
}
static void writeFile(const std::string& p, const std::vector<uint8_t>& b) {
    (void)writeFileBytes(p, b);
}

static void testRoundTrip() {
    CapacityModel model;
    reference::seedReferenceModel(model);
    const std::string p = "persist_rt.cf";
    CHECK(model.save(p));
    CapacityModel m2;
    CHECK(m2.load(p));
    CHECK(m2.recovered());
    CHECK(m2.currentResources().size() == model.currentResources().size());
    CHECK(m2.digest() == model.digest());
    // Dynamic current capacity is NOT asserted after recovery: freshness is
    // RevalidationRequired until fresh workers republish.
    auto fe = m2.queryFeasibilityNow(reference::makeDemand(WorkloadDemandId(1), WorkloadDemandGeneration(1), 1.0, DeviceCount(1)));
    CHECK(fe.outcome == Outcome::RevalidationRequired || fe.outcome == Outcome::InsufficientEvidence);
}

static void testBadMagic() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_badmagic.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    b[0] ^= 0xff; b[1] ^= 0xff; b[2] ^= 0xff; b[3] ^= 0xff;
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

static void testBadVersion() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_badver.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    b[4] = 99;   // version field
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

static void testTruncation() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_trunc.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    b.resize(b.size() - 10);
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

static void testTrailingGarbage() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_trail.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    b.push_back(0xAB); b.push_back(0xCD);
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

static void testChecksumFlip() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_cksum.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    // Flip a byte in the middle of the serialized body (not the header).
    b[b.size() / 2] ^= 0x40;
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

static void testHostileLength() {
    CapacityModel model; reference::seedReferenceModel(model);
    const std::string p = "persist_hostile.cf";
    CHECK(model.save(p));
    auto b = readFile(p);
    // Set the length field (bytes 8..11) to a huge value.
    b[8] = 0xFF; b[9] = 0xFF; b[10] = 0xFF; b[11] = 0x7F;
    writeFile(p, b);
    CapacityModel m2;
    CHECK(!m2.load(p));
}

int main() {
    testRoundTrip();
    testBadMagic();
    testBadVersion();
    testTruncation();
    testTrailingGarbage();
    testChecksumFlip();
    testHostileLength();
    CF_TEST_SUMMARY();
    return CF_TEST_RETURN();
}
