{
    "name"      : "Glass",
    "guid"      : "00000000-0000-0000-0000-000000000005",
    "preview"   : false,

    "blendMode"     : "alphablend",
    "renderLayer"   : "transparent",

    "uniforms": [
        {
            "displayName"   : "Refractivity",
            "name"          : "refractivity",
            "type"          : "float",
            "value"         : 0.5,
            "min"           : 0,
            "max"           : 1,
            "uniform"       : "u_refractive_factor"
        }, {
            "displayName"   : "Influence",
            "name"          : "Influence",
            "type"          : "float",
            "value"         : 0.5,
            "min"           : 0,
            "max"           : 1,
            "uniform"       : "u_influence"
        }, {
            "displayName"   : "Transparency",
            "name"          : "Transparency",
            "type"          : "float",
            "value"         : 1.0,
            "min"           : 0,
            "max"           : 1,
            "uniform"       : "u_alpha"
        }
    ],
    "builtin": false,
    "vertex_shader": "app/shaders/custom.vert",
    "fragment_shader": "app/shaders/custom.frag"
}
