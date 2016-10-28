namespace jah3d
{

AdvanceMaterial::AdvanceMaterial()
{
    diffuseTexture = nullptr;
    textureScale = 1.0f;

    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile("assets/simple.vert");
    //vshader->compileSourceFile("assets/advance.vert");

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile("assets/simple.frag");
    //fshader->compileSourceFile("assets/advance.frag");

    program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);
    program->bindAttributeLocation("a_pos", POS_ATTR_LOC);
    program->bindAttributeLocation("a_texCoord", TEXCOORD_ATTR_LOC);
    program->bindAttributeLocation("a_normal", NORMAL_ATTR_LOC);
    program->bindAttributeLocation("a_tangent", TANGENT_ATTR_LOC);

    program->link();

    program->bind();
    program->setUniformValue("u_useDiffuseTex",false);
    program->setUniformValue("u_useNormalTex",false);
    program->setUniformValue("u_useReflectionTex",false);
    program->setUniformValue("u_useSpecularTex",false);

    ambientColor = QColor(0,0,0);

    useNormalTex = false;
    normalIntensity = 1.0f;
    normalTexture = nullptr;

    diffuseColor = QColor(255,255,255);
    useDiffuseTex = false;
    diffuseTexture = nullptr;

    shininess = 100;
    useSpecularTex = false;
    specularColor = QColor(200,200,200);
    specularTexture = nullptr;

    float reflectionInfluence;
    bool useReflectonTex;
    QOpenGLTexture* reflectionTexture;

    this->setDiffuseColor(diffuseColor);
    this->setSpecularColor(specularColor);
    this->setShininess(0.01f);
}

void AdvanceMaterial::begin(QOpenGLFunctions* gl)
{
    program->bind();

    program->enableAttributeArray(POS_ATTR_LOC);
    program->enableAttributeArray(TEXCOORD_ATTR_LOC);
    program->enableAttributeArray(NORMAL_ATTR_LOC);
    program->enableAttributeArray(TANGENT_ATTR_LOC);

    //program->setUniformValue("u_material.diffuse",false);
    //program->setUniformValue("u_useNormalTex",false);

    //set textures
    for(int i=0;i<textures.size();i++)
    {
        auto tex = textures[i];
        if(tex->texture!=nullptr)
        {
            gl->glActiveTexture(GL_TEXTURE0+i);
            tex->texture->bind(i);
            program->setUniformValue(tex->name.toStdString().c_str(), i);
        }
        else
        {
            //unset texture
            //bug: setting this to -1 throws error when drawing array
            //program->setUniformValue(tex->name.toStdString().c_str(), 1000);
        }
    }

    //pass light data

}

void AdvanceMaterial::end()
{
    //unset textures
}

void AdvanceMaterial::setDiffuseTexture(QOpenGLTexture* tex)
{
    diffuseTexture=tex;
    auto matTex = new MaterialTexture();
    matTex->texture = tex;
    matTex->name = "u_diffuseTexture";
    textures.append(matTex);

    //useDiffuseTex
    program->bind();
    program->setUniformValue("u_useDiffuseTex",true);
    program->release();
}

/*
QOpenGLShader* AdvanceMaterial::loadShader(QOpenGLShader::ShaderType type,QString filePath)
{
    //http://stackoverflow.com/questions/17149454/reading-entire-file-to-qstring
    auto shader = new QOpenGLShader(type,this);
    shader->compileSourceFile(filePat);

}
*/

void AdvanceMaterial::setAmbientColor(QColor col)
{
    ambientColor = col;

    program->bind();
    program->setUniformValue("u_material.ambient",QVector3D(col.redF(),col.greenF(),col.blueF()));
    program->release();
}

void AdvanceMaterial::setDiffuseColor(QColor col)
{
    diffuseColor = col;

    program->bind();
    program->setUniformValue("u_material.diffuse",QVector3D(col.redF(),col.greenF(),col.blueF()));
    program->release();
}

QColor AdvanceMaterial::getDiffuseColor()
{
    return QColor();
}

void AdvanceMaterial::setNormalTexture(QOpenGLTexture* tex)
{
    normalTexture=tex;
    auto matTex = new MaterialTexture();
    matTex->texture = tex;
    matTex->name = "u_normalTexture";
    textures.append(matTex);

    //useDiffuseTex
    program->bind();
    program->setUniformValue("u_useNormalTex",true);
    program->release();
}

void AdvanceMaterial::setNormalIntensity(float intensity)
{
    normalIntensity = intensity;

    program->bind();
    program->setUniformValue("u_normalIntensity",normalIntensity);
    program->release();
}

float AdvanceMaterial::getNormalIntensity()
{
    return normalIntensity;
}


void AdvanceMaterial::setSpecularTexture(QOpenGLTexture* tex)
{
    specularTexture=tex;
    auto matTex = new MaterialTexture();
    matTex->texture = tex;
    matTex->name = "u_specularTexture";
    textures.append(matTex);

    //useDiffuseTex
    program->bind();
    program->setUniformValue("u_useSpecularTex",true);
    program->release();
}

void AdvanceMaterial::setSpecularColor(QColor col)
{
    specularColor = col;
    program->bind();
    auto spec = QVector3D(col.redF(),col.greenF(),col.blueF());
    program->setUniformValue("u_material.specular",spec);
    program->release();
}

QColor AdvanceMaterial::getSpecularColor()
{
    return specularColor;
}

void AdvanceMaterial::setShininess(float shininess)
{
    this->shininess = shininess;
    program->bind();
    program->setUniformValue("u_material.shininess",shininess);
    program->release();
}

float AdvanceMaterial::getShininess()
{
    return 0.0f;
}


void AdvanceMaterial::setReflectionTexture(QOpenGLTexture* tex)
{
    reflectionTexture=tex;
    auto matTex = new MaterialTexture();
    matTex->texture = tex;
    matTex->name = "u_reflectionTexture";
    textures.append(matTex);

    program->bind();
    program->setUniformValue("u_useReflectionTex",true);
    program->release();
}

void AdvanceMaterial::setReflectionInfluence(float intensity)
{
    reflectionInfluence = intensity;

    program->bind();
    program->setUniformValue("u_reflectionInfluence",intensity);
    program->release();
}

void AdvanceMaterial::setTextureScale(float scale)
{
    this->textureScale = scale;
    //qDebug()<<scale<<endl;
    //textureScaleParam->setValue(scale);

    program->bind();
    program->setUniformValue("u_textureScale",scale);
    program->release();
}

}
