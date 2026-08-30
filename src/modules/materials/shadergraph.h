#include "graph/nodegraph.h"

class ShaderGraph : public NodeGraph
{
public:

	static ShaderGraph* createDefaultShaderGraph();
	static ShaderGraph* createParticleShaderGraph();
	static ShaderGraph* createPBRShaderGraph();

};