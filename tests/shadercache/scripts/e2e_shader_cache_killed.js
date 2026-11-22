// shadercache.app.killed: the harness kills the app on the startup gate's last line, before this script runs (see
// test_shader_cache_app.cpp --killed). Reached only if the kill came late: a clean quit, which the row reports as red.
console.log("SHADERCACHE-KILLED the script ran: the kill came after the gate's line");
app.quit();
