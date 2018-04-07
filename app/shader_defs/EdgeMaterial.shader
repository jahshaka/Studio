{
    "name"  : "EdgeMaterial",
    "guid"  : "00000000-0000-0000-0000-000000000003",
    "preview": false,
    "uniforms": [
        {
            "displayName"   : "Color",
            "name"          : "color",
            "type"          : "color",
            "value"         : "#ffffff",
            "uniform"       : "u_color"
        },
        {
            "displayName"   : "Edge Color",
            "name"          : "edge_color",
            "type"          : "color",
            "value"         : "#ffffff",
            "uniform"       : "u_edgeColor"
        },
        {
            "displayName"   : "Diffuse Texture",
            "name"          : "diffuseTex",
            "type"          : "texture",
            "uniform"       : "u_diffuseTex",
            "toggle"        : "u_useDiffuseTex"
        },
        {
            "displayName"   : "Fresnel Pow",
            "name"          : "fresnelPow",
            "type"          : "float",
            "uniform"       : "u_fresnelPow",
            "value"         : 1.2,
            "min"           : 1.0,
            "max"           : 4
        }
    ],
    "builtin": false,
    "vertex_shader": ":assets/shaders/surface.vert",
    "fragment_shader": "app/shaders/diffuse_surface.frag"
}
