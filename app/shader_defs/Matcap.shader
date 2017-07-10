{
    "name"                  : "Matcap",
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
