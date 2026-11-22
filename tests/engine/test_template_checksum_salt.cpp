// engine.template_checksum_salt (SHADER-SYSTEM-1 stage 5, the lead's ruling 2; a NULL Root, no card, no display) - THE
// PREPROCESSOR GUARD IS OGRE's OWN RE-PARSE. With the fork commit and the staged media trees gone from the hlms layer's key,
// a cached recipe's processed source is guarded by the Hlms' TEMPLATE CHECKSUM alone: unchanged, HlmsDiskCache compiles
// the saved processed source; changed, it re-parses the templates from the saved properties and pieces (fork
// OgreHlmsDiskCache.cpp, "the templates have changed"). The laws, on a real HlmsUnlit over scratch template folders:
//   (1) THE SALT IS IN THE CHECKSUM: a salt (the host's hash of the preprocessor's own code, Hlms::setJahTemplateSalt)
//       moves it, and a zero salt is upstream's checksum exactly;
//   (2) THE CHECKSUM COVERS A LIBRARY's PIECES (Photon/Hlms and Hlms/Jahshaka are PBS libraries, OgreEngine.cpp): an
//       edited library piece moves it at the next launch (reloadFrom);
//   (3) ...and the template itself.
#include <OgreAbiUtils.h>
#include <OgreArchiveManager.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreRoot.h>

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {
void writeFile(const std::string &path, const std::string &bytes) { std::ofstream(path, std::ios::binary) << bytes; }
struct Sum {
    Ogre::uint64 h[2] = { 0u, 0u };
    bool operator==(const Sum &o) const { return h[0] == o.h[0] && h[1] == o.h[1]; }
    bool operator!=(const Sum &o) const { return !(*this == o); }
};
Sum checksum(const Ogre::Hlms *h) { Sum s; h->getTemplateChecksum(s.h); return s; }
}  // namespace

static int run() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return 1;
    std::string tmplPath = std::string(cwd) + "/template-salt-XXXXXX";
    const char *made = mkdtemp(&tmplPath[0]);
    if (!made) { std::printf("FAIL: no scratch directory\n"); return 1; }
    const std::string dir = made;
    if (std::system(("mkdir -p '" + dir + "/main' '" + dir + "/lib'").c_str()) != 0) return 1;
    writeFile(dir + "/main/VertexShader_vs.glsl", "#version 450\n@insertpiece( Body )\nvoid main() {}\n");
    writeFile(dir + "/lib/Body_piece_vs.any", "@piece( Body )// body v1@end\n");

    {
        const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
        auto root = std::make_unique<Ogre::Root>(&cookie, "", "", dir + "/ogre.log", "salt");
        root->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
        if (root->getAvailableRenderers().empty()) { std::printf("FAIL: no NULL render system\n"); return 1; }
        root->setRenderSystem(root->getAvailableRenderers().front());
        root->initialise(false);
        root->createRenderWindow("salt", 1u, 1u, false);
        Ogre::ArchiveManager &am = Ogre::ArchiveManager::getSingleton();
        Ogre::ArchiveVec libs{ am.load(dir + "/lib", "FileSystem", true) };
        Ogre::Archive *main = am.load(dir + "/main", "FileSystem", true);
        auto *unlit = OGRE_NEW Ogre::HlmsUnlit(main, &libs);
        root->getHlmsManager()->registerHlms(unlit);

        const Sum base = checksum(unlit);
        const Ogre::uint64 salt[2] = { 0x0123456789abcdefull, 0xfedcba9876543210ull };
        unlit->setJahTemplateSalt(salt);
        const Sum salted = checksum(unlit);
        const Ogre::uint64 zero[2] = { 0u, 0u };
        unlit->setJahTemplateSalt(zero);
        const Sum unsalted = checksum(unlit);
        CHECK(salted != base, "(1) a salt moves the template checksum");
        CHECK(unsalted == base, "(1) a zero salt is upstream's checksum exactly");

        writeFile(dir + "/lib/Body_piece_vs.any", "@piece( Body )// body v2@end\n");
        unlit->reloadFrom(main, &libs);
        const Sum pieceEdited = checksum(unlit);
        CHECK(pieceEdited != base, "(2) an edited LIBRARY piece moves the checksum (the next launch: reloadFrom)");

        writeFile(dir + "/main/VertexShader_vs.glsl", "#version 450\n@insertpiece( Body )\nvoid main() { }\n");
        unlit->reloadFrom(main, &libs);
        CHECK(checksum(unlit) != pieceEdited, "(3) an edited template moves it too");
        root.reset();
    }
    std::system(("rm -rf '" + dir + "'").c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}

// A LAW THAT FAILS FAILS - it never crashes or aborts the row: an exception (Ogre's included) is a FAIL line and exit 1.
int main() {
    try {
        return run();
    } catch (const std::exception &e) {
        std::printf("FAIL: an exception ended the row: %s\nRESULT: 1 FAILURE(S)\n", e.what());
        return 1;
    }
}
