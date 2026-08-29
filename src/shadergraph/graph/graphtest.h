#include "nodegraph.h"
//#include "nodemodel.h"
#include <QMouseEvent>
#include <QDebug>
#include <QLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QComboBox>

#include "scenewidget.h"
#include "graph/nodegraph.h"

// temporary variable
struct TempVar
{
    QString name;
    QString typeName;
};

struct CodeChunk
{
    //NodeModel* node = nullptr;
    QString owner;
    QString code;
};

class ShaderContext : public ModelContext
{
    QStringList uniforms;
    // mapped using the socket guid
    QMap<QString, TempVar> tempVars;
    QList<CodeChunk> codeChunks;
    //QString code;
    bool debugCode;
public:
    void addFunction(QString name, QString function);

    // created a temporary variable using the socket
    TempVar createTempVar(SocketModel* sock)
    {
        if (tempVars.contains(sock->id))
            return tempVars[sock->id];

        TempVar tempVar;
        tempVar.name = QString("temp%1").arg(tempVars.count());
        tempVar.typeName = sock->typeName;
        tempVars.insert(sock->id, tempVar);

        return tempVar;
    }

    QList<TempVar> getTempVars()
    {
        return tempVars.values();
    }

    void clear()
    {
        tempVars.clear();
    }

    void addCodeChunk(NodeModel* node, QString code)
    {
        QString ownerName = node->title + " - " + node->id;
        //CodeChunk chunk = {node, code};
        codeChunks.append({ownerName, code});
    }

    // add declaration for uniform here
    void addUniform(QString uniformDecl)
    {
        uniforms.append(uniformDecl);
    }

    QString generateVars()
    {
        QString finalCode = "";

        // generate temp vars
        for(auto& var : tempVars) {
            auto c = var.typeName + " " + var.name + ";\n";
            finalCode = c + finalCode;
        }

        return finalCode;
    }

    // generates uniforms from properties
    QString generateUniforms()
    {
        QString finalCode = "";

        // generate temp vars
        for(auto& uniform : uniforms) {
            finalCode += uniform + ";\n";
        }

        return finalCode;
    }

    QString generateCode(bool debug = false)
    {
        QString finalCode = "";

        // combine code chunks
        for (auto chunk : codeChunks) {
            if (debug)
                finalCode += "// "+chunk.owner+"\n";

            finalCode += chunk.code;
            finalCode += "\n";

            // extra space for debug mode
            if (debug)
                finalCode += "\n";
        }

        return finalCode;
    }
};

class ShaderGenerator
{
public:
    QString generateShader(NodeGraph* graph)
    {
        auto ctx = new ShaderContext();

        auto masterNode = graph->getMasterNode();

        generateProperties(graph, ctx);
        processNode(masterNode, ctx);

        auto code = ctx->generateUniforms();
        code += ctx->generateVars();

        code += "void surface(inout Material material){\n";
        code += ctx->generateCode(true);
        code += "}\n";

        return code;
    }

    void generateProperties(NodeGraph* graph, ShaderContext* ctx)
    {
        for(auto node : graph->nodes.values()) {
            for(auto sock : node->outSockets) {
                auto tempVar = ctx->createTempVar(sock);
                sock->setVarName(tempVar.name);
            }
        }
    }

    void preprocess();

    void processNode(NodeModel* node, ShaderContext* ctx)
    {
        for ( auto sock : node->inSockets) {
            if (sock->hasConnection()) {
                auto con = sock->getConnection();
                processNode(con->leftSocket->getNode(), ctx);
            }
        }

        node->process(ctx);
    }

    void postprocess();
};


class SurfaceMasterNode : public NodeModel
{
public:
    SurfaceMasterNode();
    virtual void process(ModelContext* ctx) override;
};


class FloatNodeModel : public NodeModel
{
    QLineEdit* lineEdit;

    FloatSocketModel* valueSock;
public:
    FloatNodeModel():
        NodeModel()
    {
		setNodeType(NodeType::Number);
		lineEdit = new QLineEdit();
        lineEdit->setValidator(new QDoubleValidator());
        lineEdit->setStyleSheet("border: 2px solid rgba(220,220,220,1);"
                                "border-radius: 2px;"
                                "padding: 2px 8px 2px 12px;"
                                "background: rgba(0,0,0,0);"
                                "color: rgba(250,250,250);");

       // lineEdit->setMaximumSize(lineEdit->sizeHint());
       // lineEdit->setMaximumSize(QSize(160,20));

        connect(lineEdit, &QLineEdit::textChanged,
                  this, &FloatNodeModel::editTextChanged);

        auto containerWidget = new QWidget();
        auto layout = new QVBoxLayout;
        containerWidget->setMaximumSize(170,55);
        containerWidget->setLayout(layout);
        containerWidget->setStyleSheet("background:rgba(0,0,0,0);");
        layout->addWidget(lineEdit);
        layout->setSpacing(0);

        this->widget = containerWidget;

        typeName = "float";
        title = "Float";

        // add output socket
        valueSock = new FloatSocketModel("value");
        addOutputSocket(valueSock);

        lineEdit->setText("0.0");
    }

    void editTextChanged(const QString& text)
    {
        valueSock->setValue(text);
        emit valueChanged(this, 0);
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        auto res = this->getOutputSocketVarName(0);

        auto code = res +" = "+valueSock->getValue()+";";
        //ctx->addCodeChunk(this, code);
        valueSock->setVarName(valueSock->getValue());
    }

    virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override
    {
        return lineEdit->text().toDouble();
    }

    virtual void deserializeWidgetValue( QJsonValue val, int widgetIndex = 0) override
    {
        auto value = val.toDouble();
        lineEdit->setText(QString("%1").arg(value));
    }
};

/*
class ColorNode : public NodeModel
{
    QLineEdit* lineEdit;

    FloatSocketModel* valueSock;
public:
    ColorNode():
        ColorNode()
    {
        lineEdit = new QLineEdit();
        lineEdit->setValidator(new QDoubleValidator());
        lineEdit->setMaximumSize(lineEdit->sizeHint());

        connect(lineEdit, &QLineEdit::textChanged,
                  this, &FloatNodeModel::editTextChanged);

        this->widget = lineEdit;

        typeName = "float";
        title = "Float";

        // add output socket
        valueSock = new FloatSocketModel("value");
        addOutputSocket(valueSock);

        lineEdit->setText("0.0");
    }

    void editTextChanged(const QString& text)
    {
        valueSock->setValue(text);
        emit valueChanged(this, 0);
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        auto res = this->getOutputSocketVarName(0);

        auto code = res +" = "+valueSock->getValue()+";";
        //ctx->addCodeChunk(this, code);
        valueSock->setVarName(valueSock->getValue());
    }
};
*/

class VectorMultiplyNode : public NodeModel
{
public:
    VectorMultiplyNode()
    {
		setNodeType(NodeType::Calculation);
		title = "Vector Multiply";
        typeName = "vectorMultiply";

        addInputSocket(new Vector3SocketModel("A"));
        addInputSocket(new Vector3SocketModel("B"));
        addOutputSocket(new Vector3SocketModel("Result"));
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        auto valA = this->getValueFromInputSocket(0);
        auto valB = this->getValueFromInputSocket(1);
        auto res = this->getOutputSocketVarName(0);

        //valA = SocketHelper::convertValue(valA,inSockets[0], outSockets[0]);
        //valB = SocketHelper::convertValue(valB,inSockets[1], outSockets[0]);

        auto code = res +" = " + valA + " * " + valB + ";";
        ctx->addCodeChunk(this, code);
    }
};

class WorldNormalNode : public NodeModel
{
public:
    WorldNormalNode()
    {
		setNodeType(NodeType::Surface);

        title = "World Normal";
        typeName = "worldNormal";

        addOutputSocket(new Vector3SocketModel("World Normal","v_normal"));
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        auto res = this->getOutputSocketVarName(0);

        auto code = res +" = v_normal;";
        //ctx->addCodeChunk(this, code);
        outSockets[0]->setVarName("v_normal");
    }
};

class TimeNode : public NodeModel
{
public:
    TimeNode()
    {
		setNodeType(NodeType::Modifier);

        title = "Time";

        addOutputSocket(new FloatSocketModel("Seconds","u_time"));
    }

    virtual void process(ModelContext* context) override
    {
        outSockets[0]->setVarName("u_time");
    }
};

class SineNode : public NodeModel
{
public:
    SineNode()
    {
		setNodeType(NodeType::Modifier);


        title = "Sine";

        addInputSocket(new Vector3SocketModel("Value"));
        addOutputSocket(new Vector3SocketModel("Result"));
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        auto valA = this->getValueFromInputSocket(0);
        auto res = this->getOutputSocketVarName(0);

        auto code = res +" = sin(" + valA + ");";
        ctx->addCodeChunk(this, code);
    }
};

class MakeColorNode: public NodeModel
{
public:
    MakeColorNode(){
		setNodeType(NodeType::Surface);


        title = "Color";
        addInputSocket(new FloatSocketModel("Value R"));
        addInputSocket(new FloatSocketModel("Value G"));
        addInputSocket(new FloatSocketModel("Value B"));

        addOutputSocket(new Vector4SocketModel("color"));
    }

    virtual void process(ModelContext *context) override
    {
        auto ctx = (ShaderContext*)context;
        auto valr = this->getValueFromInputSocket(0);
        auto valg = this->getValueFromInputSocket(1);
        auto valb = this->getValueFromInputSocket(2);
        auto res = this->getOutputSocketVarName(0);

        auto code = res +" = vec4(" + valr + ","+valg +"," + valb +", 1);";
        ctx->addCodeChunk(this, code);
    }



};

class TextureCoordinateNode : public NodeModel
{
    QComboBox* combo;
    QString uv;
public:
    TextureCoordinateNode()
    {
		setNodeType(NodeType::Surface);

		title = "Texture Coordinate";

        combo = new QComboBox();
        combo->addItem("TexCoord0");
        combo->addItem("TexCoord1");
        combo->addItem("TexCoord2");
        combo->addItem("TexCoord3");

        connect(combo, &QComboBox::currentTextChanged,
                  this, &TextureCoordinateNode::comboTextChanged);

        combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        combo->setStyleSheet("QComboBox{border: 2px solid rgba(250,250,250,0);color : rgba(230,230,230,1);}"
                             "QComboBox QAbstractItemView{selection-background-color: lightgray; background: rgba(20,20,20,1); color : rgba(230,230,230,1); border: none; }"
                          //   "QComboBox::drop-down {border: 1px solid black; }"
                             "QComboBox::drop-arrow{color: rgba(230,230,230,1);}");

        auto containerWidget = new QWidget();
        auto layout = new QVBoxLayout;
        containerWidget->setMaximumSize(170,55);
        containerWidget->setLayout(layout);
        containerWidget->setStyleSheet("background:rgba(0,0,0,0);");
        layout->addWidget(combo);
        layout->setSpacing(0);

        this->widget = containerWidget;

        //this->widget = combo;

        typeName = "float";

        addOutputSocket(new Vector2SocketModel("UV"));
        uv = "v_texCoord";
    }

    virtual void process(ModelContext* context) override
    {
        outSockets[0]->setVarName(uv);
    }

    void comboTextChanged(const QString& text)
    {
        if (text == "TexCoord0") {
            uv = "v_texCoord";
        } else if (text == "TexCoord1") {
            uv = "v_texCoord1";
        } else if (text == "TexCoord2") {
            uv = "v_texCoord2";
        } else if (text == "TexCoord3") {
            uv = "v_texCoord3";
        }

        emit valueChanged(this, 0);
    }
};

class PropertyNode : public NodeModel
{
    Property* prop;
public:
    PropertyNode()
    {
        this->typeName = "property";
    }

    // doesnt own property
    void setProperty(Property* property)
    {
        this->prop = property;
        this->title = property->displayName;

        // add output based on property type
        switch(property->type) {
        case PropertyType::Float:
            this->addOutputSocket(new FloatSocketModel("float"));
        break;

        case PropertyType::Vec2:
            this->addOutputSocket(new Vector2SocketModel("vector2"));
        break;

        case PropertyType::Vec3:
            this->addOutputSocket(new Vector3SocketModel("vector3"));
        break;

        case PropertyType::Vec4:
            this->addOutputSocket(new Vector4SocketModel("vector4"));
        break;

        default:
            //todo: throw error or something
            Q_ASSERT(false);
            break;
        }
    }

    virtual QJsonValue serializeWidgetValue(int widgetIndex = 0) override
    {
        if (this->prop) {
            return this->prop->id;
        }

        return "";
    }

    virtual void process(ModelContext* context) override
    {
        auto ctx = (ShaderContext*)context;
        ctx->addUniform(prop->getUniformString());

        if (this->outSockets.count() > 0)
            outSockets[0]->setVarName(prop->getUniformName());
    }
};

void registerModels(NodeGraph* graph);


/*
class ShaderGen
{
public:
    QString evaluateShader(NodeGraph* graph, NodeModel* masterNode)
    {
        ShaderContext ctx;

        // master node is expected to be shader node
        QString code;
        for(int i = 0; i < masterNode->inSockets.count(); i++){
            auto c = evalSocket(ctx, graph, masterNode, i);
            code += c;

            // assign values right away
            auto sock = masterNode->inSockets[i];
            auto tempName = ctx.createTempVar(sock).name;
            code += "material." + sock->name + " = " + tempName+";\n";
        }

        // prepend code with temp variables created
        for(auto& var : ctx.getTempVars()) {
            auto c = var.typeName + " " + var.name + ";\n";
            code = c + code;
        }

        code = "void surface(inout Material material){\n" + code;
        code += "}\n";
        return code;
        // assign values
        //"material.diffuse = " + ctx
    }

    // This is executed in the perspective of a connection
    QString evalSocket(ShaderContext& ctx, NodeGraph* graph, NodeModel* socketNode, int socketIndex)
    {
        QString c = "";
        auto inSock = socketNode->inSockets[socketIndex];
        //TempVar var;
        //NodeModel* leftNode;
        auto var = ctx.createTempVar(inSock);

        // get the connection so we can get the value
        auto con = graph->getConnectionFromOutputNode(socketNode,socketIndex);

        // if there is no connection then get the default value
        if (!con) {
            //auto var = ctx.createTempVar(inSock);
            return var.name + " = " + inSock->getDefaultValue() + ";\n";
        }

        // get node left of connection and create temp var
        auto leftNode = graph->nodes[con->leftNodeId];
        auto outSock = leftNode->outSockets[con->leftSocketIndex];
        auto leftVar = ctx.createTempVar(outSock);


        if (leftNode->typeName=="worldNormal") {
            c = var.name + " = v_normal;\n";
        }
        else if (leftNode->typeName=="float") {
            c = leftVar.name + " = " + leftNode->getSocketValue(socketIndex, nullptr) + ";\n";
            auto val = convertValue(leftVar.name, outSock, inSock);
            c += var.name + " = " + val + ";\n";
        }
        else if (leftNode->typeName == "add"){
            c += evalSocket(ctx, graph, leftNode, 0);
            c += evalSocket(ctx, graph, leftNode, 1);

            // do calculations with var names
            auto a = ctx.createTempVar(leftNode->inSockets[0]);
            auto b = ctx.createTempVar(leftNode->inSockets[1]);

            c += var.name + " = " + a.name + " + " + b.name + ";\n";
        }
        else if (leftNode->typeName == "multiply"){
            c += evalSocket(ctx, graph, leftNode, 0);
            c += evalSocket(ctx, graph, leftNode, 1);

            // do calculations with var names
            auto a = ctx.createTempVar(leftNode->inSockets[0]);
            auto b = ctx.createTempVar(leftNode->inSockets[1]);

            c += var.name + " = " + a.name + " * " + b.name + ";\n";
        }

        return c;
    }

    // fromValue could be a constant or a variable name
    QString convertValue(QString fromValue, SocketModel* from, SocketModel* to)
    {
        int numFrom = getNumComponents(from->typeName);
        int numTo = getNumComponents(to->typeName);

        Q_ASSERT(numFrom!= 0 && numTo != 0);

        QString suffix = ".";

        // float being casted to a vector
        //
        if (numFrom==1 && numTo>1) {
            return QString("vec%1(%2)").arg(numTo).arg(fromValue);
        }

        if (numFrom > numTo) {
            // eg vec4 > vec2 = var.xy
            for(int i = 0; i<numTo; i++) {
                suffix += getVectorComponent(i);
            }

            return fromValue+suffix;
        }
        if (numFrom < numTo)
        {
            // eg vec2 > vec4 = var.xyyy
            for(int i = 0; i<numTo; i++) {
                suffix += getVectorComponent(qMin(i, numTo));
            }
            return fromValue+suffix;
        }

        return fromValue;

    }

    QString getVectorComponent(int index)
    {
        switch(index)
        {
            case 1: return "x";
            case 2: return "y";
            case 3: return "z";
            case 4: return "w";
        }

        return "x";
    }

    int getNumComponents(QString valueType)
    {
        if (valueType=="float")
            return 1;

        if (valueType=="vec2")
            return 2;

        if (valueType=="vec3")
            return 3;

        if (valueType=="vec4")
            return 4;

        return 0;
    }
};


NodeGraph* createGraph()
{
    NodeGraph* graph = new NodeGraph();

    // master shader node
    auto masterNode = new NodeModel();
    masterNode->typeName = "material";
    masterNode->inSockets.append(new Vector3SocketModel("Diffuse"));
    masterNode->inSockets.append(new FloatSocketModel("Alpha"));
    graph->addNode(masterNode);
    graph->setMasterNode(masterNode);

    // mult
    auto multNode = new NodeModel();
    multNode->typeName = "multiply";
    multNode->inSockets.append(new Vector3SocketModel("a"));
    multNode->inSockets.append(new Vector3SocketModel("b"));
    multNode->outSockets.append(new Vector3SocketModel("result"));
    graph->addNode(multNode);

    // normal node
    auto normalNode = new NodeModel();
    normalNode->typeName = "worldNormal";
    normalNode->outSockets.append(new Vector3SocketModel("normal"));
    graph->addNode(normalNode);

    // constant node

    // connect nodes
    graph->addConnection(multNode, 0, masterNode, 0);
    graph->addConnection(normalNode, 0, multNode, 0);

    return graph;
}

QString graphTest()
{
    auto graph = createGraph();

    // eval shader
    ShaderGen shaderGen;
    auto res = shaderGen.evaluateShader(graph, graph->getMasterNode());
    return res;
}

*/
