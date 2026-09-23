// A TEST JOB, not product media (tests/atom/media, registered by the cluster
// harness only): runs the cluster cut's GLSL half — the product pieces
// JahLevelRule_piece_cs.any (the one currency and the column scale) and
// JahClusterCut.glsl, included by name — over one mesh's DAG tables for N views,
// one thread per (view, cluster), and writes 1 where the rule draws the cluster.
// engine.lod_rule_parity compares that set with the C++ half's (Types.h
// clusterCut) to the bit.
@insertpiece( SetCrossPlatformSettings )
@insertpiece( JahLevelRuleScale )
@insertpiece( JahLevelRuleCurrency )
@insertpiece( JahClusterCut )

struct ParityView
{
	vec4 row[3];        // the instance's 3x4 transform, ROW i in element i
	vec4 eyeScale;      // xyz the eye, w unused (the scale is derived here, from the rows)
	vec4 lod;           // x tolerance (samples), y proj[1][1], z viewport height
};

layout( std430, ogre_U0 ) readonly restrict buffer countLayout { uvec4 counts; };
layout( std430, ogre_U1 ) readonly restrict buffer viewLayout { ParityView views[]; };
layout( std430, ogre_U2 ) readonly restrict buffer groupLayout { JahClusterGroup groups[]; };
layout( std430, ogre_U3 ) readonly restrict buffer clusterLayout { JahCluster clusters[]; };
layout( std430, ogre_U4 ) writeonly restrict buffer drawnLayout { uint drawn[]; };

layout( local_size_x = @value( threads_per_group_x ),
		local_size_y = @value( threads_per_group_y ),
		local_size_z = @value( threads_per_group_z ) ) in;

bool jahParityAffordable( uint g, uint v )
{
	ParityView pv = views[v];
	// The level rule's scale: the longest COLUMN of the rows (the cull's own helper).
	float scale = jahWorldMaxAxisScale( pv.row[0], pv.row[1], pv.row[2] );
	float allowed = jahClusterGroupAllowed( groups[g].sphere, pv.row[0], pv.row[1], pv.row[2],
											scale, pv.eyeScale.xyz, pv.lod.x, pv.lod.y, pv.lod.z );
	return jahClusterGroupAffordable( groups[g].error.x, allowed );
}

void main()
{
	uint id = gl_GlobalInvocationID.x;
	uint clusterCount = counts.x;
	if( id >= clusterCount * counts.y )
		return;
	uint v = id / clusterCount;
	uint c = id - v * clusterCount;
	uvec4 range = clusters[c].range;
	bool isLevel0 = range.w == 0xFFFFFFFFu;
	bool own = jahParityAffordable( range.z, v );
	bool refined = isLevel0 ? false : jahParityAffordable( range.w, v );
	drawn[id] = jahClusterDrawn( own, isLevel0, refined ) ? 1u : 0u;
}
