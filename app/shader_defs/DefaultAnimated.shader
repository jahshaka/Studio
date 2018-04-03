{
    "name"          : "DefaultAnimated",
    "preview"       : false,
    "guid"          : "00000000-0000-0000-0000-000000000002",

    "zWrite"        : true,
    "depthTest"     : true,
    "blendMode"     : "none",
    "cullMode"      : "back",

    "renderLayer"   : "opaque",
    "fog"           : true,
    "castShadows"   : true,
    "receiveShadows": true,
    "lighting"      : true,

    "uniforms": [
        {
            "displayName"   : "Ambient Color",
            "name"          : "ambientColor",
            "type"          : "color",
            "value"         : "#353535",
            "uniform"       : "u_material.ambient"
        }, {
            "displayName"   : "Diffuse Color",
            "name"          : "diffuseColor",
            "type"          : "color",
            "value"         : "#FFF",
            "uniform"       : "u_material.diffuse"
        }, {
            "displayName"   : "Diffuse Texture",
            "name"          : "diffuseTexture",
            "type"          : "texture",
            "value"         : "",
            "uniform"       : "u_diffuseTexture",
            "toggle"        : "u_useDiffuseTex"
        }, {
            "displayName"   : "Specular Color",
            "name"          : "specularColor",
            "type"          : "color",
            "value"         : "#FFF",
            "uniform"       : "u_material.specular"
        }, {
            "displayName"   : "Specular Texture",
            "name"          : "specularTexture",
            "type"          : "texture",
            "value"         : "",
            "uniform"       : "u_specularTexture",
            "toggle"        : "u_useSpecularTex"
        }, {
            "displayName"   : "Shininess",
            "name"          : "shininess",
            "type"          : "float",
            "value"         : 0,
            "min"           : 0,
            "max"           : 512,
            "uniform"       : "u_material.shininess"
        }, {
            "displayName"   : "Normal Texture",
            "name"          : "normalTexture",
            "type"          : "texture",
            "value"         : "",
            "uniform"       : "u_normalTexture",
            "toggle"        : "u_useNormalTex"
        }, {
            "displayName"   : "Normal Intensity",
            "name"          : "normalIntensity",
            "type"          : "float",
            "value"         : 0,
            "min"           : -1,
            "max"           : 1,
            "uniform"       : "u_normalIntensity"
        }, {
            "displayName"   : "Reflection Texture",
            "name"          : "reflectionTexture",
            "type"          : "texture",
            "value"         : "",
            "uniform"       : "u_reflectionTexture",
            "toggle"        : "u_useReflectionTex"
        }, {
            "displayName"   : "Reflection Influence",
            "name"          : "reflectionInfluence",
            "type"          : "float",
            "value"         : 0,
            "min"           : 0,
            "max"           : 1,
            "uniform"       : "u_reflectionInfluence"
        }, {
            "displayName"   : "Texture Scale",
            "name"          : "textureScale",
            "type"          : "float",
            "value"         : 1,
            "min"           : 0,
            "max"           : 16,
            "uniform"       : "u_textureScale"
        }, {
            "displayName"   : "Use Alpha",
            "name"          : "useAlpha",
            "type"          : "bool",
            "value"         : false,
            "uniform"       : "u_useAlpha"
        }
    ],
    "builtin": true,
    "vertex_shader": ":assets/shaders/skinned_material.vert",
    "fragment_shader": ":assets/shaders/default_material.frag"
}
