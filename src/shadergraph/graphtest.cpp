#include "graphtest.h"

void registerModels(NodeGraph* graph)
{
    // mult
    graph->registerModel("vectorMultiply", []()
    {
        auto multNode = new VectorMultiplyNode();
        return multNode;
    });

    // normal
    graph->registerModel("worldNormal", []()
    {
        auto normalNode = new WorldNormalNode();
        return normalNode;
    });

    // float
    graph->registerModel("float", []()
    {
        auto floatNode = new FloatNodeModel();
        return floatNode;
    });

    // time
    graph->registerModel("time", []()
    {
        auto node = new TimeNode();
        return node;
    });


    // uv
    graph->registerModel("Texture Coordinate", []()
    {
        return new TextureCoordinateNode();
    });

    // sine
    graph->registerModel("sine", []()
    {
        return new SineNode();
    });

    //make color
    graph->registerModel("Make Color", [](){
        return new MakeColorNode();
    });
    // sine
    graph->registerModel("property", []()
    {
        return new PropertyNode();
    });
}

SurfaceMasterNode::SurfaceMasterNode()
{
    title = "Surface Material";
    typeName = "Material";
    addInputSocket(new Vector3SocketModel("diffuse"));
    addInputSocket(new Vector3SocketModel("specular"));
    addInputSocket(new FloatSocketModel("shininess"));
    addInputSocket(new Vector3SocketModel("normal", "v_normal"));
    addInputSocket(new Vector3SocketModel("ambient"));
    addInputSocket(new Vector3SocketModel("emission"));
    addInputSocket(new FloatSocketModel("alpha"));
}

void SurfaceMasterNode::process(ModelContext* ctx)
{
    QString code = "";
    auto context = (ShaderContext*)ctx;
    //context->addCodeChunk(this, "void surface(inout Material material){\n");

    auto diffVar = this->getValueFromInputSocket(0);
    auto specVar = this->getValueFromInputSocket(1);
    auto shininessVar = this->getValueFromInputSocket(2);
    auto normVar = this->getValueFromInputSocket(3);
    auto ambientVar = this->getValueFromInputSocket(4);
    auto emissionVar = this->getValueFromInputSocket(5);
    auto alphaVar = this->getValueFromInputSocket(6);

    code += "material.diffuse = " + diffVar + ";\n";
    code += "material.specular = " + specVar + ";\n";
    code += "material.shininess = " + shininessVar + ";\n";
    code += "material.normal = " + normVar + ";\n";
    code += "material.ambient = " + ambientVar + ";\n";
    code += "material.emission = " + emissionVar + ";\n";
    code += "material.alpha = " + alphaVar + ";\n";
    //context->addCodeChunk(this, "material.diffuse = " + diffVar + ";");

    context->addCodeChunk(this, code);
}
