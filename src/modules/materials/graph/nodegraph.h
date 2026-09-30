#ifndef NODEGRAPH2_H
#define NODEGRAPH2_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QMap>
#include <QUuid>
#include <QJsonValue>
#include <QJsonObject>
#include <functional>
#include "../models/properties.h"

class NodeModel;
class ConnectionModel;
class NodeLibrary;

// Unreal-order blend modes for the master material (serialized as STRINGS in
// serializeMaterialSettings, so the numeric order can match the settings-view
// combo). Opaque keeps the baker's auto rules (a connected cutoff -> Masked, a
// baked alpha chain -> Translucent); the others force the material's alphaMode.
enum class BlendMode {
	Opaque,
	Masked,       // alpha cutout (material alphaMode 1)
	Translucent,  // plain alpha blend — the mode formerly named "Blend" (alphaMode 2)
	Additive,     // Final = Src + Dest (alphaMode 4)
	Modulate,     // Final = Src × Dest (alphaMode 5)
	// THE GRAPH CAN SAY EVERY ALPHA MODE THE MATERIAL HAS (PRESET-UNIFY-1
	// fix round). Two of the material's seven were missing here — Glass and
	// Refractive — and the consequence was not cosmetic: a material carrying
	// one of them could not be described by a graph at all, so saving its
	// graph silently turned it opaque, and the only way round that was to
	// carry the old value forward past whatever the user had chosen, which
	// made "set Blend Mode to Opaque" impossible to obey. The graph says it
	// now, and nothing is carried.
	Glass,        // the engine's glass (alphaMode 3): alpha blend that keeps specular
	Refractive,   // (alphaMode 6): samples what is behind it, refractionStrength
};

/// A graph material's settings. EVERY FIELD HERE LANDS SOMEWHERE — that is a
/// rule now, not an observation.
///
/// It used to carry eight more (HLMS_ADOPTION P2 deleted them): zwrite,
/// depthTest, fog, castShadow, receiveShadow, acceptLighting, cullMode and
/// renderLayer. All eight were live checkboxes and combos in TWO panels, they
/// were serialized, and they were UNDOABLE — a completely finished UI wired to
/// nothing. Not one had a reader outside the widgets that set it.
///
/// Two of them have honest homes now: "receive shadows" is a real PbrMaterial
/// row (P1) that reaches the renderer, and per-material fog is a candidate for
/// the custom-piece phase. The rest describe render state the engine derives
/// from the blend mode and the alpha mode.
struct MaterialSettings {
	QString name = "";
	BlendMode blendMode = BlendMode::Opaque;
	// Final bake resolution for this material's UV-varying chains
	// (MATERIALS_EVALUATOR_SPEC section 2); previews always bake 256.
	int bakeResolution = 1024;
};

class NodeGraph
{
public:
	/// A CLOSED GRAPH IS FREED (MATERIALS_TABS_SPEC §2.8). Nothing ever
	/// deleted one: the page dropped the pointer on every open, so every
	/// material a session looked at leaked its whole graph.
	~NodeGraph();

	QMap<QString, NodeModel*> nodes;
	QMap<QString, ConnectionModel*> connections;
	NodeModel* masterNode = nullptr;
	MaterialSettings settings;
	QString materialGuid = "";

	/// PbrMasterNode's socket layout (see pbrmasternode.h): nine sockets, no
	/// Occlusion. deserialize REFUSES a graph stamped with any other value (or
	/// none) — connections are stored by index (FORWARD-ONLY-1).
	static constexpr int kSocketLayoutVersion = 2;

	QVector<NodeModel *> getNodesByTypeName(QString name);

	/// THE NODE LIBRARY IS NOT OWNED. It is a stateless factory registry
	/// handed in from outside, and half the callers own a stack-allocated
	/// one (tests/shadergraph, tests/materialpreview) — so the destructor
	/// below frees the graph's own nodes and connections and never this.
	/// The app has exactly one (MaterialHelper::sharedNodeLibrary).
	NodeLibrary* library = nullptr;
	void setNodeLibrary(NodeLibrary* lib);

	void addNode(NodeModel* model);
	NodeModel* getNode(const QString& nodeId);
	QVector<ConnectionModel*> getNodeConnections(const QString& nodeId);
	void removeNode(const QString& nodeId);

	// master node must already be added as a node
	void setMasterNode(NodeModel* masterNode);
	NodeModel* getMasterNode();

	ConnectionModel* addConnection(NodeModel* leftNode, int leftSockIndex, NodeModel* rightNode, int rightSockIndex);
	ConnectionModel* addConnection(QString leftNodeId, int leftSockIndex, QString rightNodeId, int rightSockIndex);

	void removeConnection(QString connectionId);

	QJsonObject serialize();
	/// Loads a saved graph, or REFUSES it.
	///
	/// null = this file cannot be opened, and `refusalReason` (when given) then
	/// carries ONE plain sentence for the person using the editor. The only
	/// refusal is the MASTER: a master node that is not the PBR one, or no
	/// master at all (an absent/empty `masternode`, or one naming a node the
	/// file does not contain). The Blinn-Phong
	/// "Surface Material" master was deleted with its node class
	/// (LEGACY-MASTER-CRUD, 2026-09-20 — the owner: "we should have no legacy
	/// graphs, remove it"), and a graph written on a master this build does not
	/// have is not a graph this build can draw. It is refused WHOLE rather than
	/// half-loaded, because a silently master-less graph bakes nothing and looks
	/// like an empty canvas the user then saves over.
	///
	/// A graph stamped with another socket layout (kSocketLayoutVersion) is
	/// refused the same way (FORWARD-ONLY-1: no load-time renames or index
	/// shifts exist). Every caller must handle null. An unknown NODE type is
	/// skipped.
	static NodeGraph* deserialize(QJsonObject obj, NodeLibrary* lib,
	                              QString* refusalReason = nullptr);
	QJsonObject serializeMaterialSettings();
	static MaterialSettings deserializeMaterialSettings(QJsonObject obj);

	void setMaterialSettings(MaterialSettings setting);
};

#endif// NODEGRAPH2_H