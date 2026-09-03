// Warm launch: report what the startup build did. The whole assertion set lives
// in the driver — this script only has to observe.
console.log("SHADERCACHE " + JSON.stringify(app.shaderCache()));
app.quit();
