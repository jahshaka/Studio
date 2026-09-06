// scripting.e2e.graph_edit — the materials graph learns to READ and to EDIT
// (verb-coverage audit F2).
//
// Before this the graph verbs could only GROW a graph: add a node, connect two
// sockets, set a value. There was no way to ask what a graph was wired like
// (only what nodes it had), no way to learn a node type's socket names without
// instantiating one, and no way to take anything back out. So a script could
// build a material and never edit one — and an assistant asked to "unhook the
// roughness texture" had to rebuild the whole graph.
//
// Four verbs close that: connections(), nodeInfo(type), removeNode(id) and
// disconnect(). This suite drives them over a SCRIPT graph
// (materials.createGraph — no Materials page in a --script run), which is the
// half the headless matrix guarantees. The page half — where the same two
// removals go through the canvas's own undo commands so graph.undo covers them
// — is gated by the shadergraph.selection unit suite, which has a real
// GraphNodeScene and a real QUndoStack.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("graph edit");

// ---- nodeInfo: socket names WITHOUT instantiating anything ----------------

var types = graph.nodeTypes();
assert(types.length > 10, "graph.nodeTypes() lists the library (" + types.length + " types)");

var master = graph.nodeInfo("PbrMaterial");
console.log("nodeInfo(PbrMaterial).inputs: "
            + JSON.stringify(master.inputs.map(function (s) { return s.name; })));
assert(master.inputs.length > 4, "the master node has its PBR input sockets");
assert(master.outputs.length === 0, "...and no outputs — it IS the output");
var names = master.inputs.map(function (s) { return s.name.toLowerCase(); });
assert(names.indexOf("base color") >= 0 || names.indexOf("basecolor") >= 0,
       "the base-colour socket is named in the report: " + JSON.stringify(names));
for (var i = 0; i < master.inputs.length; i++)
    assert(master.inputs[i].index === i, "input " + i + " reports its index");

var vec = graph.nodeInfo("vector3");
assert(vec.outputs.length >= 1, "a vector3 node reports an output socket");

var unknown = false;
try { graph.nodeInfo("definitely-not-a-node"); } catch (e) { unknown = true; }
assert(unknown, "an unknown type is refused with the same message addNode gives");

// ---- build something to read and to edit ---------------------------------

assert(materials.createGraph("edit me").length > 0, "materials.createGraph");
// createGraph already installs the PbrMaterial master — adding a second one
// would only orphan the first.
var m = graph.nodes().filter(function (n) { return n.master; })[0].id;
var a = graph.addNode("vector3");
var b = graph.addNode("float");
assert(graph.nodes().length === 3, "three nodes in the graph");

// Find the master's colour-ish and scalar-ish input sockets by NAME, through
// the verb that reports them.
var info = graph.nodeInfo("PbrMaterial");
var colourSocket = info.inputs[0].name;
var scalarSocket = info.inputs[info.inputs.length - 1].name;

assert(graph.connect(a, 0, m, colourSocket), "connect the vector3 to " + colourSocket);
assert(graph.connect(b, 0, m, scalarSocket), "connect the float to " + scalarSocket);

// ---- connections(): the topology, at last ---------------------------------

var cons = graph.connections();
console.log("graph.connections(): " + JSON.stringify(cons));
assert(cons.length === 2, "both pipes are reported");
for (var c = 0; c < cons.length; c++) {
    assert(cons[c].id && cons[c].id.length > 0, "every pipe has an id");
    assert(cons[c].to === m, "...running INTO the master");
    assert(cons[c].from === a || cons[c].from === b, "...from one of the two sources");
    assert(typeof cons[c].fromSocket === "string" && cons[c].fromSocket.length > 0,
           "...naming its output socket (" + cons[c].fromSocket + ")");
    assert(typeof cons[c].toSocket === "string" && cons[c].toSocket.length > 0,
           "...and its input socket (" + cons[c].toSocket + ")");
    assert(cons[c].fromIndex >= 0 && cons[c].toIndex >= 0,
           "...as indices too, so graph.connect can be handed either spelling");
}

// ---- disconnect by the INPUT END, the shape a caller actually has ----------

assert(graph.disconnect({ to: m, toSocket: colourSocket }),
       "graph.disconnect({to, toSocket}) — an input holds at most one pipe, so the pair "
       + "names exactly one");
var after = graph.connections();
assert(after.length === 1, "one pipe left");
assert(after[0].toSocket !== colourSocket, "...and it is the other one");

var already = false;
try { graph.disconnect({ to: m, toSocket: colourSocket }); } catch (e) { already = true; }
assert(already, "disconnecting an input with nothing on it is refused, not silently true");

var noSocket = false;
try { graph.disconnect({ to: m, toSocket: "Nonexistent Socket" }); } catch (e) { noSocket = true; }
assert(noSocket, "an input socket that does not exist is refused");

// ---- disconnect by ID -----------------------------------------------------

assert(graph.disconnect(after[0].id), "graph.disconnect(connectionId)");
assert(graph.connections().length === 0, "the graph has no pipes left");

// A connection id that does not exist USED TO CRASH: NodeGraph::removeConnection
// had its guard commented out, so `connections[id]` inserted a null entry and
// then dereferenced it. Surviving this call is half the assertion.
var badId = false;
try { graph.disconnect("no-such-connection"); } catch (e) { badId = true; }
assert(badId, "an unknown connection id is refused (and does not crash)");

// ---- removeNode -----------------------------------------------------------

// Re-wire so the removal has connections to take with it.
assert(graph.connect(a, 0, m, colourSocket), "re-connect for the removal test");
assert(graph.connections().length === 1, "one pipe again");

assert(graph.removeNode(a), "graph.removeNode(the vector3)");
assert(graph.nodes().length === 2, "the node is gone");
assert(graph.connections().length === 0,
       "...and it took its pipe with it — a dangling connection is not a thing");

var masterRefused = false;
try { graph.removeNode(m); } catch (e) { masterRefused = true; }
assert(masterRefused, "the MASTER node is refused: it is the graph's output");
assert(graph.nodes().length === 2, "...and it is still there");

var noNode = false;
try { graph.removeNode("no-such-node"); } catch (e) { noNode = true; }
assert(noNode, "an unknown node id is refused");

// The graph still evaluates after all that surgery.
var evaluated = graph.evaluate();
assert(evaluated.hasPbrMaster === true, "the edited graph still evaluates");

console.log("e2e_graph_edit: ALL OK");
