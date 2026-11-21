// engine.compute_record (COLD-BOOT-1 stage 3; CPU, no card, no engine) - THE COMPUTE LAYER'S RECORD AND ITS TWO KEYS
// (irisgl engine/src/ComputeRecord.h says why). The laws:
//   (1) A RECORD SURVIVES THE LAYER: records -> compute.bin's bytes -> records, equal field for field, in order.
//   (2) THE INPUT KEY IS THE CONTENT THE PARSE READS (the lead's ruling P3): a changed template, piece file, C++ piece,
//       property or root layout (SHADER-SYSTEM-1 F2a: a slot's type reaches no property) moves the key of the job that
//       reads it; a change to ANOTHER job's piece does not move this job's key;
//       the C++ pieces' container order is not an input. NEGATIVE CONTROL: two jobs differing ONLY in a C++ piece get two
//       keys - the key HlmsCompute::permutationKey gave (source name + piece file names + properties, fork
//       OgreHlmsCompute.cpp) folds no C++ piece and could not tell them apart; its fields are restated here as
//       `todaysKeyFields` and shown to merge them.
//   (3) A RECORD PAIRS WITH A PROGRAM ONLY THROUGH ITS PROGRAM KEY (the lead's condition 1): a record whose stored source
//       does not hash to its stored program key is REJECTED at read and counted; the others are kept. Damaged bytes
//       (bad magic, a truncated record, trailing bytes) are no layer at all: ok = false, nothing filed.
//   (5) A CHANGED SOURCE DROPS EXACTLY ITS ENTRIES (P3): at load, the records one of whose files (the template, a piece
//       file) now hashes to other content - or is gone - are pruned; the records that read only unchanged files stay.
//   (4) THE PROGRAM KEY IS THE FORK'S CODE-CACHE KEY: the source's hash alone without an explicit API, the source and the
//       root layout together with one - a different root layout is a different program.
#include "ComputeRecord.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine::detail;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

ComputeCodeRecord makeRecord(const std::string &name, const std::string &source, const std::string &layout) {
    ComputeCodeRecord r;
    r.name = name;
    r.sourceFilename = name + "_cs.glsl";
    r.source = source;
    r.rootLayout = layout;
    r.explicitApi = true;
    r.arrayBindings = name.size() % 2 == 0;
    r.threadsPerGroup[0] = 8; r.threadsPerGroup[1] = 8; r.threadsPerGroup[2] = 1;
    r.numThreadGroups[0] = 4; r.numThreadGroups[1] = 2; r.numThreadGroups[2] = 1;
    r.indirect = false;
    r.jobSized = true;
    r.files = { { name + "_cs.glsl", computeFileHash("template of " + name) },
                { "CrossPlatformSettings_piece_all.glsl", computeFileHash("shared piece v1") } };
    ComputeKeyInputs in;
    in.templateText = "template of " + name;
    r.inputKey = computeRecordInputKey(in);
    r.programKey = computeProgramKey(r.source, r.rootLayout, r.explicitApi);
    return r;
}

ComputeKeyInputs jobInputs(const std::string &piece, const std::string &cppPiece) {
    ComputeKeyInputs in;
    in.templateText = "#version 450\n@insertpiece( Body )\n";
    in.pieceFiles = { { "CrossPlatformSettings_piece_all.glsl", "#define ogre_float float\n" },
                      { "Body_piece_cs.any", piece } };
    in.cppPieces = { { 0x3333u, "// header\n" }, { 0x4444u, cppPiece } };
    in.properties = { { 0x1111u, 8 }, { 0x2222u, 1 } };
    in.rootLayout = std::string(48, '\x05');
    return in;
}

/// What the fork's HlmsCompute::permutationKey folds (OgreHlmsCompute.cpp): the source NAME, the piece file NAMES and
/// the properties. The C++ pieces and every file's BYTES are not among them.
std::string todaysKeyFields(const ComputeKeyInputs &in, const std::string &sourceName) {
    std::string k = sourceName;
    for (const auto &f : in.pieceFiles) k += "|" + f.first;
    for (const auto &p : in.properties) k += "|" + std::to_string(p.first) + "=" + std::to_string(p.second);
    return k;
}

}  // namespace

int main() {
    // (1) THE ROUND TRIP
    std::vector<ComputeRecordPtr> records;
    records.push_back(std::make_shared<ComputeCodeRecord>(makeRecord("Jahshaka/ReceiverDepth", "void main() { a(); }", std::string(64, '\x01'))));
    records.push_back(std::make_shared<ComputeCodeRecord>(makeRecord("VCT/LightInjection", std::string(20000, 'x') + "\n", std::string(64, '\x02'))));
    records.push_back(std::make_shared<ComputeCodeRecord>(makeRecord("Jahshaka/VideoNv12", "", std::string())));
    const std::vector<char> bytes = writeComputeLayer(records);
    const ComputeLayerRead back = readComputeLayer(bytes.data(), bytes.size());
    bool equal = back.ok && back.records.size() == records.size() && back.rejected == 0;
    for (size_t i = 0; equal && i < records.size(); ++i) equal = back.records[i] == *records[i];
    CHECK(equal, "(1) %zu records -> %zu bytes -> %zu records, equal field for field and in order (rejected %u)",
          records.size(), bytes.size(), back.records.size(), back.rejected);
    const std::vector<char> none = writeComputeLayer({});
    const ComputeLayerRead empty = readComputeLayer(none.data(), none.size());
    CHECK(empty.ok && empty.records.empty(), "(1) an empty layer reads back empty and valid");

    // (2) THE INPUT KEY
    const ComputeKeyInputs base = jobInputs("body A\n", "extra A\n");
    const ComputeKey128 k0 = computeRecordInputKey(base);
    CHECK(computeRecordInputKey(jobInputs("body A\n", "extra A\n")) == k0, "(2) the same inputs give the same key");
    ComputeKeyInputs t = base; t.templateText += " ";
    CHECK(computeRecordInputKey(t) != k0, "(2) a changed TEMPLATE byte moves the key");
    ComputeKeyInputs p = base; p.pieceFiles[1].second = "body B\n";
    CHECK(computeRecordInputKey(p) != k0, "(2) a changed PIECE FILE's bytes move the key");
    ComputeKeyInputs c = base; c.cppPieces[1].second = "extra B\n";
    CHECK(computeRecordInputKey(c) != k0, "(2) a changed C++ PIECE moves the key");
    ComputeKeyInputs v = base; v.properties[0].second = 16;
    CHECK(computeRecordInputKey(v) != k0, "(2) a changed PROPERTY value moves the key");
    ComputeKeyInputs l = base; l.rootLayout[7] = '\x06';
    CHECK(computeRecordInputKey(l) != k0,
          "(2) a changed ROOT LAYOUT moves the key (equal text, other slot types: SHADER-SYSTEM-1 F2a)");
    ComputeKeyInputs o = base; std::swap(o.cppPieces[0], o.cppPieces[1]);
    CHECK(computeRecordInputKey(o) == k0, "(2) the C++ pieces' container order is not an input");
    // Another job reads another piece: editing THAT piece moves the other job's key and leaves this one's.
    ComputeKeyInputs other = base; other.pieceFiles[1] = { "Other_piece_cs.any", "other v1\n" };
    const ComputeKey128 otherBefore = computeRecordInputKey(other);
    other.pieceFiles[1].second = "other v2\n";
    CHECK(computeRecordInputKey(other) != otherBefore && computeRecordInputKey(base) == k0,
          "(2) an edit to ANOTHER job's piece moves that job's key and not this one's");
    // NEGATIVE CONTROL: two jobs differing only in a C++ piece.
    CHECK(todaysKeyFields(base, "JahX_cs") == todaysKeyFields(c, "JahX_cs") && computeRecordInputKey(c) != k0,
          "(2) NEGATIVE CONTROL: two jobs differing only in a C++ piece - one key by the old permutation key's fields, "
          "two by the input key");

    // (3) THE PAIRING LAW
    std::vector<ComputeRecordPtr> mixed = records;
    ComputeCodeRecord foreign = *records[0];
    foreign.source += "// another compiler's source\n";   // the stored program key is the original source's
    mixed.insert(mixed.begin() + 1, std::make_shared<ComputeCodeRecord>(foreign));
    const std::vector<char> mb = writeComputeLayer(mixed);
    const ComputeLayerRead mr = readComputeLayer(mb.data(), mb.size());
    CHECK(mr.ok && mr.rejected == 1 && mr.records.size() == records.size() && mr.records[0] == *records[0] &&
              mr.records[1] == *records[1],
          "(3) a record whose source does not hash to its program key is rejected (%u) and the other %zu filed",
          mr.rejected, mr.records.size());
    std::vector<char> bad = bytes; bad[0] = 'X';
    const ComputeLayerRead br = readComputeLayer(bad.data(), bad.size());
    std::vector<char> cut(bytes.begin(), bytes.end() - 5);
    const ComputeLayerRead cr = readComputeLayer(cut.data(), cut.size());
    std::vector<char> extra = bytes; extra.push_back('\0');
    const ComputeLayerRead er = readComputeLayer(extra.data(), extra.size());
    CHECK(!br.ok && br.records.empty() && !cr.ok && cr.records.empty() && !er.ok && er.records.empty(),
          "(3) damaged bytes (bad magic, a truncated record, a trailing byte) are no layer: nothing filed");

    // (4) THE PROGRAM KEY
    const std::string src = "void main() {}\n";
    CHECK(computeProgramKey(src, "L1", true) != computeProgramKey(src, "L2", true) &&
              computeProgramKey(src, "L1", false) == computeProgramKey(src, "L2", false) &&
              computeProgramKey(src, "L1", true) != computeProgramKey(src, "L1", false),
          "(4) on an explicit API the root layout is part of the program key; without one only the source is");

    // (5) THE PRUNE: three records; the second's own template is edited, every one reads the shared piece unchanged.
    std::vector<ComputeCodeRecord> loaded;
    for (const ComputeRecordPtr &r : records) loaded.push_back(*r);
    std::map<std::string, std::string> staged = { { "CrossPlatformSettings_piece_all.glsl", "shared piece v1" } };
    for (const ComputeCodeRecord &r : loaded) staged[r.sourceFilename] = "template of " + r.name;
    staged["VCT/LightInjection_cs.glsl"] += " (edited)";
    auto current = [&](const std::string &name, ComputeKey128 &out) {
        auto it = staged.find(name);
        if (it == staged.end()) return false;
        out = computeFileHash(it->second);
        return true;
    };
    std::vector<ComputeCodeRecord> kept = loaded;
    const unsigned dropped = pruneStaleRecords(kept, current);
    CHECK(dropped == 1 && kept.size() == 2 && kept[0] == loaded[0] && kept[1] == loaded[2],
          "(5) one record's template edited: exactly that record dropped (%u), the two others kept (%zu)", dropped,
          kept.size());
    staged["CrossPlatformSettings_piece_all.glsl"] = "shared piece v2";
    kept = loaded;
    CHECK(pruneStaleRecords(kept, current) == 3 && kept.empty(), "(5) a piece every record reads edited: all dropped");
    staged.erase("Jahshaka/VideoNv12_cs.glsl");
    staged["CrossPlatformSettings_piece_all.glsl"] = "shared piece v1";
    kept = loaded;
    CHECK(pruneStaleRecords(kept, current) == 2 && kept.size() == 1 && kept[0] == loaded[0],
          "(5) a record whose file is gone is dropped like an edited one");

    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
