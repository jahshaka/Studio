{
    "name"  : "Matcap",
    "guid"  : "00000000-0000-0000-0000-000000000006",
    "uniforms": [
        {
            "displayName"   : "Matcap Texture",
            "name"          : "matTexture",
            "type"          : "texture",
            "value"         : "",
            "uniform"       : "u_matTexture",
            "toggle"        : "u_useMatTexture"
        }
    ],
    "builtin": true,
    "vertex_shader": "app/shaders/matcap.vert",
    "fragment_shader": "app/shaders/matcap.frag"
}
