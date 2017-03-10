//**********************************************************************//
//									//
//	Frozen.shader for Q3Radiant					//
//	by Sock - 14th August 2000					//
//									//

// Direction & elevation checked and adjusted - Speaker
//**********************************************************************//
// q3map_sun <red> <green> <blue> <intensity> <degrees> <elevation>
// color will be normalized, so it doesn't matter what range you use
// intensity falls off with angle but not distance 100 is a fairly bright sun
// degree of 0 = from the east, 90 = north, etc.  altitude of 0 = sunrise/set, 90 = noon

textures/skies/sb_frozen
{
	qer_editorimage env/sb_frozen/frozen_ft.tga
	surfaceparm noimpact
	surfaceparm nolightmap
	q3map_globaltexture
	q3map_lightsubdivide 256
	q3map_surfacelight 100
	surfaceparm sky
	q3map_sun 227 237 254 100 50 60
	skyparms env/sb_frozen/frozen - -
}

