#include <QColor>

#include "graphicsdevice.h"
#include "rendertarget.h"
#include "texture2d.h"
#include "vertexlayout.h"
#include "shader.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLFunctions>

namespace iris
{

VertexBuffer::VertexBuffer(VertexLayout vertexLayout)
{
    this->vertexLayout = vertexLayout;
    bufferId = -1;
    data = nullptr;
    dataSize = 0;
    _isDirty = true;
}

void VertexBuffer::setData(void *bufferData, unsigned int sizeInBytes)
{
    if(data)
        delete data;

    data = new char[sizeInBytes];
    memcpy(this->data, bufferData, sizeInBytes);
    dataSize = sizeInBytes;

    _isDirty = true;
}

void VertexBuffer::destroy()
{
    if (data)
        delete data;
    // todo: delete gl buffer
}

void VertexBuffer::upload(QOpenGLFunctions_3_2_Core* gl)
{
    //auto gl = device->getGL();
    //auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    if (bufferId == -1)
        gl->glGenBuffers(1, &bufferId);

    gl->glBindBuffer(GL_ARRAY_BUFFER, bufferId);
    // todo : add buffer usage option (nick)
    gl->glBufferData(GL_ARRAY_BUFFER, dataSize, data, GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

    _isDirty = false;
}

IndexBuffer::IndexBuffer()
{
    this->device = device;
    bufferId = -1;
    data = nullptr;
    dataSize = 0;
    _isDirty = true;
}

void IndexBuffer::setData(void *bufferData, unsigned int sizeInBytes)
{
    if(data)
        delete data;

    data = new char[sizeInBytes];
    memcpy(this->data, bufferData, sizeInBytes);
    dataSize = sizeInBytes;

    _isDirty = true;
}

void IndexBuffer::upload(QOpenGLFunctions_3_2_Core* gl)
{
    //auto gl = device->getGL();
    //auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    if (bufferId == -1)
        gl->glGenBuffers(1, &bufferId);

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferId);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, dataSize, data, GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void IndexBuffer::destroy()
{
    if (data)
        delete data;
    // todo: delete gl buffer
}

QOpenGLFunctions_3_2_Core *GraphicsDevice::getGL() const
{
    return gl;
}

GraphicsDevicePtr GraphicsDevice::create()
{
    return GraphicsDevicePtr(new GraphicsDevice());
}

GraphicsDevice::GraphicsDevice()
{
    context = QOpenGLContext::currentContext();
    gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();

    // 8 texture units by default
    for(int i =0;i<8;i++)
        textureUnits.append(TexturePtr());

    gl->glGenVertexArrays(1, &defautVAO);

    _internalRT = RenderTarget::create(1024,1024);

    //set default blend and depth state
    this->setBlendState(BlendState::Opaque, true);
    this->setDepthState(DepthState::Default, true);
    this->setRasterizerState(RasterizerState::CullCounterClockwise, true);
    activeProgram = nullptr;
}

void GraphicsDevice::setViewport(const QRect& vp)
{
    viewport = vp;
    gl->glViewport(vp.x(), vp.y(), vp.width(),vp.height());
}

QRect GraphicsDevice::getViewport()
{
    return viewport;
}

void GraphicsDevice::setRenderTarget(RenderTargetPtr renderTarget)
{
    clearRenderTarget();

    activeRT = renderTarget;
    activeRT->bind();
}

void GraphicsDevice::setRenderTarget(Texture2DPtr colorTarget)
{
	setRenderTarget({ colorTarget }, iris::Texture2DPtr());
}

// the size of all the textures should be the same
void GraphicsDevice::setRenderTarget(QList<Texture2DPtr> colorTargets, Texture2DPtr depthTarget)
{
    clearRenderTarget();

    // set the initial size
    if(colorTargets.size()!=0) {
        auto tex = colorTargets[0];
        _internalRT->resize(tex->getWidth(), tex->getHeight(), false);
    }
    else if(!!depthTarget) {
        // set the rt size to the depth target size
        _internalRT->resize(depthTarget->getWidth(), depthTarget->getHeight(), false);
    }

    if(colorTargets.size()>0) {
        for(auto tex : colorTargets)
            _internalRT->addTexture(tex);
    }

    if(!!depthTarget)
        _internalRT->setDepthTexture(depthTarget);

    activeRT = _internalRT;
    activeRT->bind();
}

void GraphicsDevice::clearRenderTarget()
{
    // reset to default rt
    if (!!activeRT) {
        activeRT->unbind();

        // clear all textures from internal RT
        if (activeRT == _internalRT) {
            _internalRT->clearTextures();
            _internalRT->clearDepthTexture();
        }

        activeRT.reset();
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, context->defaultFramebufferObject());
}

void GraphicsDevice::clear(QColor color)
{
    clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT, color);
}

void GraphicsDevice::clear(GLuint bits)
{
    gl->glClear(bits);
}

void GraphicsDevice::clear(GLuint bits, QColor color, float depth, int stencil)
{
    gl->glClearColor(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    gl->glClearDepth(depth);
    gl->glClearStencil(stencil);
    gl->glClear(bits);
}

void GraphicsDevice::setShader(ShaderPtr shader, bool force)
{
	if (!!activeShader) {
		if ((activeShader != shader) || force) {
			int index = 0;
			auto& samplers = activeShader->samplers;

			// reset textures to 0
			// this step might not be needed if all textures units are set to null
			// when setting a shader
			for (auto& sampler : samplers)
			{
				clearTexture(index++);
			}
		}
	}

    activeShader = shader;
	if (!!activeShader) {
		if (activeShader->isDirty)
			compileShader(activeShader);
		shader->program->bind();
		activeProgram = shader->program;
	}
	else {
		activeProgram = nullptr;
		gl->glUseProgram(0);
	}

	// nullify all textures
	/*
	{
		int index = 0;
		auto& samplers = activeShader->samplers;
		for (auto& sampler : samplers)
		{
			clearTexture(index++);
		}
	}
	*/
}

void GraphicsDevice::compileShader(iris::ShaderPtr shader)
{
	QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
	vshader->compileSourceCode(shader->vertexShader);

	QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
	fshader->compileSourceCode(shader->fragmentShader);

	if (!shader->program)
		shader->program = new QOpenGLShaderProgram;

	auto& program = shader->program;
	program->removeAllShaders();

	program->addShader(vshader);
	program->addShader(fshader);

	program->bindAttributeLocation("a_pos", (int)VertexAttribUsage::Position);
	program->bindAttributeLocation("a_color", (int)VertexAttribUsage::Color);
	program->bindAttributeLocation("a_texCoord", (int)VertexAttribUsage::TexCoord0);
	program->bindAttributeLocation("a_texCoord1", (int)VertexAttribUsage::TexCoord1);
	program->bindAttributeLocation("a_texCoord2", (int)VertexAttribUsage::TexCoord2);
	program->bindAttributeLocation("a_texCoord3", (int)VertexAttribUsage::TexCoord3);
	program->bindAttributeLocation("a_normal", (int)VertexAttribUsage::Normal);
	program->bindAttributeLocation("a_tangent", (int)VertexAttribUsage::Tangent);
	program->bindAttributeLocation("a_boneIndices", (int)VertexAttribUsage::BoneIndices);
	program->bindAttributeLocation("a_boneWeights", (int)VertexAttribUsage::BoneWeights);

	program->link();

	//todo: check for errors

	//get attribs, uniforms and samplers
	//http://stackoverflow.com/questions/440144/in-opengl-is-there-a-way-to-get-a-list-of-all-uniforms-attribs-used-by-a-shade
	auto programId = shader->program->programId();
	GLint count;
	GLint size;
	GLenum type;

	const GLsizei bufSize = 64;
	GLchar name[bufSize];
	GLsizei length;

	//attributes
	gl->glGetProgramiv(programId, GL_ACTIVE_ATTRIBUTES, &count);
	shader->attribs.clear();
	shader->uniforms.clear();
	shader->samplers.clear();

	for (int i = 0; i<count; i++)
	{
		gl->glGetActiveAttrib(programId, i, bufSize, &length, &size, &type, name);
		auto attrib = new ShaderValue();
		attrib->location = gl->glGetAttribLocation(programId, name);
		attrib->name = std::string(name);
		attrib->type = type;
		shader->attribs.insert(QString(name), attrib);
	}

	//uniforms and samplers
	gl->glGetProgramiv(programId, GL_ACTIVE_UNIFORMS, &count);

	for (int i = 0; i<count; i++)
	{
		gl->glGetActiveUniform(programId, i, bufSize, &length, &size, &type, name);

		if (type == GL_SAMPLER_1D || type == GL_SAMPLER_2D || type == GL_SAMPLER_3D || type == GL_SAMPLER_CUBE)
		{
			auto sampler = new ShaderSampler();
			sampler->location = gl->glGetUniformLocation(programId, name);
			sampler->name = std::string(name);
			shader->samplers.insert(QString(name), sampler);
		}
		else
		{
			auto uniform = new ShaderValue();
			uniform->location = gl->glGetUniformLocation(programId, name);
			uniform->name = std::string(name);
			uniform->type = type;
			shader->uniforms.insert(QString(name), uniform);
		}

	}

	shader->isDirty = false;
}

void GraphicsDevice::setTexture(int target, Texture2DPtr texture)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    if (!!texture)
        gl->glBindTexture(GL_TEXTURE_2D, texture->getTextureId());
    else
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
}

void GraphicsDevice::clearTexture(int target)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
}

void GraphicsDevice::setVertexBuffer(VertexBufferPtr vertexBuffer)
{
    vertexBuffers.clear();
    if (vertexBuffer->isDirty())
        vertexBuffer->upload(gl);
    vertexBuffers.append(vertexBuffer);
}

void GraphicsDevice::setVertexBuffers(QList<VertexBufferPtr> vertexBuffers)
{
    this->vertexBuffers.clear();
    for(auto& vertexBuffer : vertexBuffers)
    {
        if (vertexBuffer->isDirty())
            vertexBuffer->upload(gl);
        this->vertexBuffers.append(vertexBuffer);
    }
}

void GraphicsDevice::setIndexBuffer(IndexBufferPtr indexBuffer)
{
    if (!!indexBuffer) {
        this->indexBuffer = indexBuffer;
        if (indexBuffer->isDirty())
            indexBuffer->upload(gl);
    }
    else
        this->indexBuffer.clear();
}

void GraphicsDevice::clearIndexBuffer()
{
    this->indexBuffer.clear();
}

void GraphicsDevice::setBlendState(const BlendState &blendState, bool force)
{
    bool blendEnabled = true;
    if(blendState.colorSourceBlend == GL_ONE &&
       blendState.colorDestBlend == GL_ZERO &&
       blendState.alphaSourceBlend == GL_ONE &&
       blendState.alphaDestBlend == GL_ZERO) {
        blendEnabled = false;
    }

    if (force || this->lastBlendEnabled != blendEnabled) {
        if (blendEnabled)
            gl->glEnable(GL_BLEND);
        else
            gl->glDisable(GL_BLEND);
    }
    this->lastBlendEnabled = blendEnabled;

    // update blend equations
    if (force || (lastBlendState.alphaBlendEquation != blendState.alphaBlendEquation ||
        lastBlendState.colorBlendEquation != blendState.colorBlendEquation)) {

        gl->glBlendEquationSeparate(blendState.colorBlendEquation, blendState.alphaBlendEquation);
        lastBlendState.alphaBlendEquation = blendState.alphaBlendEquation;
        lastBlendState.colorBlendEquation = blendState.colorBlendEquation;
    }

    // update blend functions
    if(force || (lastBlendState.colorSourceBlend != blendState.colorSourceBlend ||
       lastBlendState.colorDestBlend != blendState.colorDestBlend ||
       lastBlendState.alphaSourceBlend != blendState.alphaSourceBlend ||
       lastBlendState.alphaDestBlend != blendState.alphaDestBlend))
    {
        gl->glBlendFuncSeparate(blendState.colorSourceBlend,
                                blendState.colorDestBlend,
                                blendState.alphaSourceBlend,
                                blendState.alphaDestBlend);

        lastBlendState.colorSourceBlend = blendState.colorSourceBlend;
        lastBlendState.colorDestBlend = blendState.colorDestBlend;
        lastBlendState.alphaSourceBlend = blendState.alphaSourceBlend;
        lastBlendState.alphaDestBlend = blendState.alphaDestBlend;
    }
}

void GraphicsDevice::setDepthState(const DepthState &depthStencil, bool force)
{
    // depth test
    if (force || (lastDepthState.depthBufferEnabled != depthStencil.depthBufferEnabled))
    {
        if (depthStencil.depthBufferEnabled)
            gl->glEnable(GL_DEPTH_TEST);
        else
            gl->glDisable(GL_DEPTH_TEST);

        lastDepthState.depthBufferEnabled = depthStencil.depthBufferEnabled;
    }

    // depth write
    if (force || (lastDepthState.depthWriteEnabled != depthStencil.depthWriteEnabled))
    {
        gl->glDepthMask(depthStencil.depthWriteEnabled);
        lastDepthState.depthWriteEnabled = depthStencil.depthWriteEnabled;
    }

    // depth func
    if (force || (lastDepthState.depthCompareFunc != depthStencil.depthCompareFunc))
    {
        gl->glDepthFunc(depthStencil.depthCompareFunc);
        lastDepthState.depthCompareFunc = depthStencil.depthCompareFunc;
    }
}

void GraphicsDevice::setRasterizerState(const RasterizerState &rasterState, bool force)
{
    // culling
    if (force || (lastRasterState.cullMode != rasterState.cullMode)) {
        if (rasterState.cullMode == CullMode::None)
            gl->glDisable(GL_CULL_FACE);
        else {
            gl->glEnable(GL_CULL_FACE);

            if (rasterState.cullMode == CullMode::CullClockwise)
                gl->glFrontFace(GL_CW);
            else
                gl->glFrontFace(GL_CCW);
        }

        lastRasterState.cullMode = rasterState.cullMode;
    }

    // polygon fill
    if (force || (lastRasterState.fillMode != rasterState.fillMode)) {
        gl->glPolygonMode(GL_FRONT_AND_BACK, rasterState.fillMode);
        lastRasterState.fillMode = rasterState.fillMode;
    }
}

void GraphicsDevice::drawPrimitives(GLenum primitiveType, int start, int count)
{
    gl->glBindVertexArray(defautVAO);
    for(auto buffer : vertexBuffers) {
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer->bufferId);
        buffer->vertexLayout.bind(gl);
    }

    gl->glDrawArrays(primitiveType, start, count);

    for(auto buffer : vertexBuffers) {
        buffer->vertexLayout.unbind(gl);
    }
    gl->glBindVertexArray(0);
}

// https://stackoverflow.com/a/30106751
#define BUFFER_OFFSET(i) ((char*)nullptr+(i))
void GraphicsDevice::drawIndexedPrimitives(GLenum primitiveType, int start, int count)
{
    gl->glBindVertexArray(defautVAO);
    for(auto buffer : vertexBuffers) {
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer->bufferId);
        buffer->vertexLayout.bind(gl);
    }

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer->bufferId);
    gl->glDrawElements(primitiveType,count,GL_UNSIGNED_INT,BUFFER_OFFSET(start));
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

    for(auto buffer : vertexBuffers) {
        buffer->vertexLayout.unbind(gl);
    }
    gl->glBindVertexArray(0);
}





}
