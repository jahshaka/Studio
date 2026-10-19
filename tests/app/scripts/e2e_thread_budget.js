// pool.doc::thread_budget — app.threadBudget() (THREADING-1 stage 1): THE ONE CORE BUDGET, read through the verb
// from the live app, and what the process applied at its start (main.cpp's processbudget::apply): Bullet's bound
// scheduler, Qt's global pool's maximum, the bake pool's started workers. The law itself is threading.budget's (every
// box size); this arm proves the app's own process carries it.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var b = app.threadBudget();
console.log("THREAD_BUDGET " + JSON.stringify(b));
assert(b && b.hardware >= 1 && b.live, "app.threadBudget() reports the tiers and the live pools");
assert(b.bulletThreads === b.game + b.foreground && b.foreground >= 1,
       "Bullet's threads are the game thread + the foreground share (" + b.bulletThreads + ")");
assert(b.dedicated === b.writer + b.streaming + b.multiload, "the dedicated tier is the writer + streaming + multiload");
assert(b.normalPriority === b.game + b.foreground + b.render + b.ogrePool + b.dedicated,
       "normalPriority sums the normal-priority tiers (" + b.normalPriority + ")");
assert(b.background >= 2 && b.total === b.normalPriority + b.background, "the background holds its floor of 2");
var floors = b.game + 1 + b.render + b.ogrePool + b.dedicated;
assert(b.hardware < floors || b.normalPriority <= b.hardware,
       "THE LAW: the normal-priority tiers fit the box (" + b.normalPriority + " of " + b.hardware + ")");
assert(b.live.bulletThreads === b.bulletThreads,
       "the app bound Bullet at the budget's threads (" + b.live.bulletThreads + ")");
assert(b.live.qtPoolMax === b.background,
       "Qt's global pool is capped at the background width (" + b.live.qtPoolMax + ")");
assert(b.live.bakePoolThreads === b.background,
       "the bake pool runs the background width's workers (" + b.live.bakePoolThreads + ")");
