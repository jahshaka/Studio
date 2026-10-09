// make_demo_san_miguel.js — DEMO-SCENES-1: the SAN MIGUEL courtyard as a project, from the
// downloaded archive, through OUR import path, polished by verbs only.
//
//   scenes/tools/demo_scene_run.sh scenes/tools/make_demo_san_miguel.js <build>/bin <data root> \
//       <San Miguel 2.1 dir holding san-miguel-low-poly.obj/.mtl + textures/> <evidence dir> :NN
//
// (@SRC@ / @OUT@ are substituted by the runner; a --script run has no argv.) Runs into ANY data
// root: it creates the project "San Miguel" (or the next free name), imports the OBJ into the
// library, places it, polishes it, sets the World tier High, saves, and shoots three views.
//
// WHAT THE MODEL DOOR GIVES AND WHAT THIS SCRIPT PUTS BACK (the OBJ/MTL is read by assimp at import):
//  - map_Kd arrives as the base colour map; Kd as the base colour of the untextured rows.
//  - map_Bump (McGuire's N_*.png, which ARE tangent-space normal maps) arrives as assimp's HEIGHT
//    slot, which our importer keeps in MeshMaterialData::hightTexture and never binds -> set here as
//    normalMap.
//  - the leaves' CUTOUT lives in the diffuse PNG's alpha (the MTL has no map_d); the import leaves
//    every material Opaque and back-culled -> alphaMode Masked + both faces drawn, here.
//  - Ns/Ks: the importer turns Ns into roughness with its own curve (1 - sqrt(Ns/128) * 0.9, Ns 16
//    -> 0.68, Ns 256 -> 0.10: near-mirror wood). This script uses the Blinn-Phong <-> GGX
//    equivalence instead: alpha = sqrt(2 / (Ns + 2)), perceptual roughness = sqrt(alpha)
//    (Ns 16 -> 0.58, Ns 256 -> 0.30). A COLOURED Ks (or a grey one at or over 0.3 on a dark Kd) is
//    a metal; d < 1 is glass.
// SRC is the PREPARED directory (scenes/tools/demo_obj_prepare.py: the archive hard-linked, the .mtl
// rewritten with forward slashes — its Windows paths resolve to NOTHING on Linux, every material white).
// The table below is GENERATED from the .mtl (scenes/tools/demo_mtl_table.py <mtl> <textures> --js),
// keyed by the diffuse map's name: that is the identity a placed node keeps back to its MTL row.
var SRC = "@SRC@", OUT = "@OUT@";
var DEMO_SHOT_W = 0, DEMO_SHOT_H = 0, DEMO_PLAN = false, DEMO_EV = 2.0, DEMO_WB = 7500;   // 0 = 1920 x 1080
var J = JSON.stringify;
function fail(m) { throw new Error("san miguel: " + m); }
function log() { console.log(Array.prototype.join.call(arguments, " ")); }

//@TABLE-BEGIN (generated; do not edit by hand)
var MTL_MAPS = {"0001_carros":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"027_cola_caballo_06-30-1997":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"052terresable":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"aglaonema_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"aglaonema_leaf":{"c":0.659,"d":1.0,"ks":0.04,"r":0.577},"ampelopsis_brevipedunculata_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"arco_frente":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_arco_frente_displace_inv.png","r":0.577},"arcos_lisos_2_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_arcos_lisos_2_bump.png","r":0.577},"arcos_lisos_3_color_1":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_arcos_lisos_3_bump_1.png","r":0.577},"bark06mi":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_bark06mi.png","r":0.577},"barro_2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"bl05lef2":{"c":0.658,"d":1.0,"ks":0.04,"r":0.577},"bs01brk":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"bs01flo1":{"c":0.874,"d":1.0,"ks":0.04,"r":0.577},"bs01flo2":{"c":0.894,"d":1.0,"ks":0.04,"r":0.577},"bs01lef":{"c":0.681,"d":1.0,"ks":0.04,"r":0.577},"bs04brk":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"bs04flo":{"c":0.577,"d":1.0,"ks":0.04,"r":0.577},"bs04lef":{"c":0.558,"d":1.0,"ks":0.04,"r":0.577},"bwk_1024":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_bwk_1024_Bump.png","r":0.577,"s":"bwk_1024_Spec2.png"},"candil_madera":{"c":0.0,"d":1.0,"ks":0.007,"r":0.577},"cantera_naranja_liso":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"cenicero":{"c":0.0,"d":1.0,"ks":0.196,"r":0.297},"ceramic_tile":{"c":0.0,"d":1.0,"ks":0.196,"r":0.297},"citrus_limon_leaf":{"c":0.664,"d":1.0,"ks":0.04,"r":0.577},"columna_b_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_columna_b_displacement_3_inv.png","r":0.577},"concreto_01":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"concreto_02":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7758":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7768":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7785":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7812":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7815":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7833":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"d30_smiguel_2003_7843":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"detmoldura_01_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_01_bump.png","r":0.577},"detmoldura_02_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_02_bump.png","r":0.577},"detmoldura_03_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_03_bump.png","r":0.577},"detmoldura_04_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_04_bump.png","r":0.577},"detmoldura_05_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_05_bump.png","r":0.577},"detmoldura_06_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_detmoldura_06_bump.png","r":0.577},"dracaena_fragrans_leaf":{"c":0.635,"d":1.0,"ks":0.04,"r":0.577},"elaeagnus_umbellate_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"escalera_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_escalera_bump.png","r":0.577},"escurridera_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_escurridera_bump.png","r":0.577},"eu15lef":{"c":0.676,"d":1.0,"ks":0.04,"r":0.577},"fierro_a":{"c":0.0,"d":1.0,"ks":0.08,"n":"N_Fierro_A_Bump.png","r":0.76},"fierro_b":{"c":0.0,"d":1.0,"ks":0.013,"r":0.577},"finishes.flooring.carpet.loop.5":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Finishes.Flooring.Carpet.Loop.5.png","r":0.577},"fittonia_verschaffeltii_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl03lef1":{"c":0.447,"d":1.0,"ks":0.04,"r":0.577},"fl04cnt1":{"c":0.308,"d":1.0,"ks":0.04,"r":0.577},"fl04cnt2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl04lef1":{"c":0.583,"d":1.0,"ks":0.04,"r":0.577},"fl04lef2":{"c":0.457,"d":1.0,"ks":0.04,"r":0.577},"fl04pet1":{"c":0.527,"d":1.0,"ks":0.04,"r":0.577},"fl04stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl11lef1":{"c":0.582,"d":1.0,"ks":0.04,"r":0.577},"fl11lef2":{"c":0.761,"d":1.0,"ks":0.04,"r":0.577},"fl11pet1":{"c":0.538,"d":1.0,"ks":0.04,"r":0.577},"fl11pet2":{"c":0.454,"d":1.0,"ks":0.04,"r":0.577},"fl11pet3":{"c":0.539,"d":1.0,"ks":0.04,"r":0.577},"fl11pet4":{"c":0.479,"d":1.0,"ks":0.04,"r":0.577},"fl11sta":{"c":0.514,"d":1.0,"ks":0.04,"r":0.577},"fl11stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl12lef1":{"c":0.487,"d":1.0,"ks":0.04,"r":0.577},"fl12pet1":{"c":0.481,"d":1.0,"ks":0.04,"r":0.577},"fl12pet2":{"c":0.484,"d":1.0,"ks":0.04,"r":0.577},"fl12pis1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl12sta1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl12stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl13lef1":{"c":0.414,"d":1.0,"ks":0.04,"r":0.577},"fl13lef4":{"c":0.538,"d":1.0,"ks":0.04,"r":0.577},"fl13lef5":{"c":0.103,"d":1.0,"ks":0.04,"r":0.577},"fl13pet1":{"c":0.651,"d":1.0,"ks":0.04,"r":0.577},"fl13pet2":{"c":0.459,"d":1.0,"ks":0.04,"r":0.577},"fl13pet3":{"c":0.529,"d":1.0,"ks":0.04,"r":0.577},"fl13stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl15cnt1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl15cnt2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl15lef1":{"c":0.575,"d":1.0,"ks":0.04,"r":0.577},"fl15lef2":{"c":0.352,"d":1.0,"ks":0.04,"r":0.577},"fl15pet2":{"c":0.678,"d":1.0,"ks":0.04,"r":0.577},"fl15stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl16lef1":{"c":0.374,"d":1.0,"ks":0.04,"r":0.577},"fl16lef3":{"c":0.456,"d":1.0,"ks":0.04,"r":0.577},"fl16pet1":{"c":0.427,"d":1.0,"ks":0.04,"r":0.577},"fl16stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl17cnt":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl17lef1":{"c":0.384,"d":1.0,"ks":0.04,"r":0.577},"fl17lef2":{"c":0.391,"d":1.0,"ks":0.04,"r":0.577},"fl17lef3":{"c":0.599,"d":1.0,"ks":0.04,"r":0.577},"fl17lef5":{"c":0.691,"d":1.0,"ks":0.04,"r":0.577},"fl17lef6":{"c":0.585,"d":1.0,"ks":0.04,"r":0.577},"fl17pet1":{"c":0.282,"d":1.0,"ks":0.04,"r":0.577},"fl17pet2":{"c":0.302,"d":1.0,"ks":0.04,"r":0.577},"fl17pet5":{"c":0.291,"d":1.0,"ks":0.04,"r":0.577},"fl17stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl19lef2":{"c":0.442,"d":1.0,"ks":0.04,"r":0.577},"fl19lef3":{"c":0.454,"d":1.0,"ks":0.04,"r":0.577},"fl19lef4":{"c":0.536,"d":1.0,"ks":0.04,"r":0.577},"fl19pe13":{"c":0.328,"d":1.0,"ks":0.04,"r":0.577},"fl19pe14":{"c":0.392,"d":1.0,"ks":0.04,"r":0.577},"fl19pe15":{"c":0.525,"d":1.0,"ks":0.04,"r":0.577},"fl19pe16":{"c":0.379,"d":1.0,"ks":0.04,"r":0.577},"fl19stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl24stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl29cnt1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl29cnt2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl29lef":{"c":0.62,"d":1.0,"ks":0.04,"r":0.577},"fl29pet1":{"c":0.641,"d":1.0,"ks":0.04,"r":0.577},"fl29pet2":{"c":0.636,"d":1.0,"ks":0.04,"r":0.577},"fl29stm1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl29stm2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fl29twg":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"forja_macetas":{"c":0.0,"d":1.0,"ks":0.007,"n":"N_Forja_Macetas_bump.png","r":0.577},"fuente_azulejo":{"c":0.0,"d":1.0,"ks":0.0,"r":0.577},"fuente_piedra_01":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"fuente_piedra_02":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp01lef1":{"c":0.505,"d":1.0,"ks":0.04,"r":0.577},"hp01lef2":{"c":0.447,"d":1.0,"ks":0.04,"r":0.577},"hp01pet3":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp01stm1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp01stm2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp04lef2":{"c":0.449,"d":1.0,"ks":0.04,"r":0.577},"hp07lef1":{"c":0.452,"d":1.0,"ks":0.04,"r":0.577},"hp07lef2":{"c":0.452,"d":1.0,"ks":0.04,"r":0.577},"hp07stm1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp07stm2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp11lef2":{"c":0.789,"d":1.0,"ks":0.005,"n":"N_hp13lef1_bump.png","r":0.577},"hp11stm":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_HP11stm.png","r":0.577},"hp13lef2":{"c":0.589,"d":1.0,"ks":0.04,"r":0.577},"hp13lef3":{"c":0.552,"d":1.0,"ks":0.04,"r":0.577},"hp13stm2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp17lef1":{"c":0.662,"d":1.0,"ks":0.04,"r":0.577},"hp17lef2":{"c":0.456,"d":1.0,"ks":0.04,"r":0.577},"hp17stm":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp19lef2":{"c":0.579,"d":1.0,"ks":0.04,"r":0.577},"hp19lef3":{"c":0.605,"d":1.0,"ks":0.04,"r":0.577},"hp19stm1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hp19stm2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"hypoestes_phyllostachya_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"impatiens_newguinea_hybrids_petal":{"c":0.764,"d":1.0,"ks":0.04,"r":0.577},"individual_b":{"c":0.0,"d":1.0,"ks":0.0,"r":0.577},"jardinera_1_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_jardinera_1_displacement_2.png","r":0.577},"l04-upper":{"c":0.451,"d":1.0,"ks":0.04,"n":"N_l04-upper.png","r":0.577},"l33-upper":{"c":0.397,"d":1.0,"ks":0.04,"n":"N_l33-upper.png","r":0.577},"l37-upper":{"c":0.448,"d":1.0,"ks":0.04,"n":"N_l37-upper.png","r":0.577},"losa":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"maceta_a2_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_A_Bump.png","r":0.577},"maceta_a_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_A_Bump.png","r":0.577},"maceta_b2_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_B_Bump.png","r":0.577},"maceta_b_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_B_Bump.png","r":0.577},"maceta_c2_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_C_Bump.png","r":0.577},"maceta_c_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_C_Bump.png","r":0.577},"maceta_d2_color_0":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_Maceta_D_Bump_0.png","r":0.577},"madera_barandal_esc_2":{"c":0.0,"d":1.0,"ks":0.0,"n":"N_madera_barandal_esc_2_bump.png","r":0.577},"madera_marcos":{"c":0.0,"d":1.0,"ks":0.006,"r":0.577},"madera_rustica_2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"madera_triplay_01":{"c":0.0,"d":1.0,"ks":0.005,"r":0.577},"marco_puerta_1":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_marco_puerta_1_bump.png","r":0.577},"metal_viejo_2":{"c":0.0,"d":1.0,"ks":0.012,"n":"N_metal_viejo_2.png","r":0.577},"mold_arco_01_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_mold_arco_01_bump.png","r":0.577},"moldura2piso_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_moldura2piso_bump.png","r":0.577},"moldura_techo":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_moldura_techo_bump.png","r":0.577},"moldura_volado":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"molduraterraza__color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_molduraterraza_bump.png","r":0.577},"muros_a":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_b":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_b1":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_muros_b1.png","r":0.577},"muros_b2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_c2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_d":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_e":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_f":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_g":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_h":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_j":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_k":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_l":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_m":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_n":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_p_":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_q":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_q3":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_q4":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"muros_q_patio2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"pared_barro_afinado":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"pared_calle":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"piso_patio_exterior":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_piso_patio_exterior_displace_inv.png","r":0.577},"piso_patio_exterior2":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"piso_rustico":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_piso_rustico_displace2.png","r":0.577,"s":"piso_rustico_Spec.png"},"plato_a":{"c":0.0,"d":1.0,"ks":0.196,"r":0.297},"postes_barandal_color":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_postes_barandal_bump.png","r":0.577},"puerta":{"c":0.0,"d":1.0,"ks":0.002,"n":"N_puerta_bump.png","r":0.577},"quercus_rubra_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"rust_a1":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"rust_detalle":{"c":0.0,"d":1.0,"ks":0.0,"n":"N_rust_detalle_bump.png","r":0.577},"silla_d_piel":{"c":0.0,"d":1.0,"ks":0.003,"n":"N_silla_d_piel_bump.png","r":0.577},"sm_hoja_c_seca":{"c":0.318,"d":1.0,"ks":0.04,"r":0.577},"sm_leaf_02a":{"c":0.448,"d":1.0,"ks":0.04,"n":"N_sm_leaf_02_bump.png","r":0.577},"sm_leaf_02b":{"c":0.448,"d":1.0,"ks":0.04,"n":"N_sm_leaf_02_bump.png","r":0.577},"sm_leaf_03a":{"c":0.448,"d":1.0,"ks":0.04,"n":"N_sm_leaf_02_bump.png","r":0.577},"sm_leaf_seca_02b":{"c":0.448,"d":1.0,"ks":0.04,"r":0.577},"sm_leaf_seca_03b":{"c":0.448,"d":1.0,"ks":0.04,"r":0.577},"sm_tronco":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_sm_tronco_bump.png","r":0.577},"tapa_talabera":{"c":0.0,"d":1.0,"ks":0.0,"r":0.577},"techo":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"techo_01":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"tela_blanca":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_tela_blanca.png","r":0.577},"tela_mesa_b":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"tela_mesa_d":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"tela_silla_b":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"tr02lef5":{"c":0.708,"d":1.0,"ks":0.04,"r":0.577},"tr14lef1":{"c":0.654,"d":1.0,"ks":0.04,"r":0.577},"vigas_a2":{"c":0.0,"d":1.0,"ks":0.04,"n":"N_vigas_a2_bump.png","r":0.577},"wood.3.bubinga":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577},"wood08":{"c":0.0,"d":1.0,"ks":0.049,"n":"N_WOOD08_Bump.png","r":0.577},"zebrina_pendula_bark":{"c":0.0,"d":1.0,"ks":0.04,"r":0.577}};
var MTL_UNTEXTURED = [{"name":"CafeChair_Metal","kd":[0.48,0.48,0.48],"ks":[0.04,0.04,0.04],"r":0.297,"d":1.0},{"name":"Default","kd":[0.48,0.48,0.48],"ks":[0.04,0.04,0.04],"r":0.297,"d":1.0},{"name":"material_0","kd":[0.1,0.1,0.1],"ks":[0.5,0.5,0.5],"r":0.149,"d":1.0},{"name":"material_038","kd":[0.086,0.082,0.075],"ks":[0.449,0.404,0.355],"r":0.297,"d":1.0},{"name":"material_040","kd":[0.302,0.876,0.727],"ks":[0.302,0.876,0.727],"r":0.297,"d":1.0},{"name":"material_041","kd":[1.0,1.0,1.0],"ks":[1.0,1.0,1.0],"r":0.297,"d":0.5},{"name":"material_042","kd":[0.142,0.462,0.235],"ks":[0.142,0.462,0.235],"r":0.297,"d":1.0},{"name":"material_12","kd":[0.75,0.75,0.75],"ks":[0.04,0.04,0.04],"r":0.577,"d":1.0},{"name":"material_13","kd":[0.549,0.549,0.549],"ks":[0.04,0.04,0.04],"r":0.577,"d":1.0},{"name":"material_140","kd":[0.009,0.378,0.532],"ks":[0.0,0.015,0.021],"r":0.577,"d":1.0},{"name":"material_52","kd":[0.337,0.286,0.224],"ks":[0.02,0.018,0.015],"r":0.577,"d":1.0},{"name":"material_78","kd":[0.809,0.585,0.48],"ks":[0.032,0.023,0.019],"r":0.577,"d":1.0},{"name":"material_79","kd":[0.2,0.2,0.2],"ks":[0.3,0.3,0.3],"r":0.21,"d":1.0},{"name":"materialn","kd":[0.711,0.514,0.304],"ks":[0.2,0.2,0.2],"r":0.374,"d":1.0},{"name":"materialo","kd":[0.1,0.1,0.1],"ks":[0.25,0.25,0.25],"r":0.21,"d":1.0,"n":"water_bump.png"},{"name":"materialr","kd":[0.75,0.75,0.75],"ks":[0.04,0.04,0.04],"r":0.577,"d":1.0}];
//@TABLE-END

var timing = {};
var tAll = Date.now();
var name = project.nextFreeName("San Miguel");
project.create(name, {template: "basic"});
// The courtyard has its own ground: Basic's 100 m Floor would z-fight with it.
var floor = scene.find("Floor");
if (floor) node.remove(floor);

// ---- import (the model door) ----
// The import is the long part (22 min in the Debug+ASAN build on the rig): a library that already
// holds this model WITH ITS MAPS (an earlier run into the same data root) is reused, not re-imported.
var t0 = Date.now();
var lib = null;
var have = assets.list({scope: "store", type: "object", query: "san-miguel-low-poly"});
for (var h = 0; h < have.length && !lib; ++h)
    if (have[h].name === "san-miguel-low-poly" && assets.metadata(have[h].guid).textures > 0) lib = have[h].guid;
timing.reused = !!lib;
if (!lib) lib = assets.import(SRC + "/san-miguel-low-poly.obj", {units: "m"});
timing.importMs = Date.now() - t0;
var meta = assets.metadata(lib);
timing.triangles = meta.triangles; timing.meshes = meta.meshes; timing.materials = meta.materials;
timing.textures = meta.textures;
var pg = assets.addToProject(lib);
t0 = Date.now();
var root = assets.addToScene(pg, {position: {x: 0, y: 0, z: 0}});
timing.placeMs = Date.now() - t0;
node.rename(root, "San Miguel");
log("import", J(timing));

// ---- the polish ----
var texName = {};            // texture asset guid -> its file name, lower case, no extension
function nameOfTexture(guid) {
    if (!guid) return "";
    if (texName[guid] === undefined) {
        var m = assets.metadata(guid);
        var n = (m && m.name) ? String(m.name) : "";
        texName[guid] = n.replace(/\.[^.]*$/, "").toLowerCase();
    }
    return texName[guid];
}
var imported = {};           // file -> texture asset guid (each picture imported ONCE)
function tex(file) {
    if (!file) return null;
    if (imported[file] === undefined) {
        try { imported[file] = assets.importFile(SRC + "/textures/" + file); }
        catch (e) { log("texture refused", file, e); imported[file] = null; }
    }
    return imported[file];
}
// The importer stores Kd as the base colour SRGB-ENCODED (Kd 0.48 reads back #b8b8b8), so the
// match encodes the table's Kd the same way before comparing.
function srgb(x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * Math.pow(x, 1 / 2.4) - 0.055; }
function nearestUntextured(c) {
    var best = null, bd = 1e9;
    for (var i = 0; i < MTL_UNTEXTURED.length; ++i) {
        var k = MTL_UNTEXTURED[i].kd;
        var d = Math.abs(srgb(k[0]) - c.r) + Math.abs(srgb(k[1]) - c.g) + Math.abs(srgb(k[2]) - c.b);
        if (d < bd) { bd = d; best = MTL_UNTEXTURED[i]; }
    }
    return bd < 0.03 ? best : null;
}
function colourOf(v) {        // material.get's colour -> {r,g,b} in 0..1
    if (!v) return {r: 1, g: 1, b: 1};
    if (typeof v === "string" && v.charAt(0) === "#") {
        var h = v.length >= 9 ? v.substring(3) : v.substring(1);
        return {r: parseInt(h.substr(0, 2), 16) / 255, g: parseInt(h.substr(2, 2), 16) / 255,
                b: parseInt(h.substr(4, 2), 16) / 255};
    }
    if (v.r !== undefined) return {r: v.r > 1 ? v.r / 255 : v.r, g: v.g > 1 ? v.g / 255 : v.g, b: v.b > 1 ? v.b / 255 : v.b};
    return {r: 1, g: 1, b: 1};
}
var stats = {meshes: 0, mapped: 0, untextured: 0, unmatched: 0, cutouts: 0, normals: 0, metals: 0, glass: 0};
var unmatched = {}, whiteSeen = 0, byUntex = {};
var rows = scene.nodes({subtree: root});
for (var i = 0; i < rows.length; ++i) {
    var row = rows[i];
    if (row.type !== "mesh" && row.type !== "MeshNode" && row.type !== "Mesh") continue;
    var m;
    try { m = material.get(row.id); } catch (e) { continue; }
    stats.meshes++;
    var u = null, set = {}, cutout = false, metal = false, glass = false, normal = null;
    var key = nameOfTexture(m.textureAssets ? m.textureAssets.baseColorMap : "");
    var t = key ? MTL_MAPS[key] : null;
    if (t) {
        stats.mapped++;
        set.roughness = t.r;
        set.metallic = 0.0;
        normal = t.n || null;
        cutout = t.c > 0.02;
        glass = t.d < 1.0;
    } else if (!key) {
        u = nearestUntextured(colourOf(m.baseColor));
        if (u) {
            stats.untextured++;
            byUntex[u.name] = (byUntex[u.name] || 0) + 1;
            set.roughness = u.r;
            // METAL, by the MTL's own evidence: a DARK diffuse under a strong specular (Kd <= 0.15,
            // Ks >= 0.3: material_0's chrome, material_038's brass) is a metal whose colour is Ks.
            // CafeChair_Metal says so by name — and "Default" carries the SAME Kd/Ks/Ns, so the two
            // cannot be told apart after the import: both are read as the chairs' dark wrought iron.
            // Coloured Ks equal to Kd (040/042) is glazed ceramic/glass, not metal.
            var lum = (u.kd[0] + u.kd[1] + u.kd[2]) / 3, ksMax = Math.max(u.ks[0], u.ks[1], u.ks[2]);
            var iron = /metal/i.test(u.name);
            metal = iron || (lum <= 0.15 && ksMax >= 0.3 && !u.n);
            set.metallic = metal ? 1.0 : 0.0;
            if (iron) { set.baseColor = [0.05, 0.05, 0.05]; set.roughness = 0.45; }
            else if (metal) set.baseColor = u.ks.map(function (x) { return Math.min(1, x * 1.6); });
            if (u.n) set.roughness = 0.05;                 // the fountain's water: a smooth dielectric
            glass = u.d < 1.0;
            normal = u.n || null;
        } else { stats.unmatched++; unmatched[J(m.baseColor)] = (unmatched[J(m.baseColor)] || 0) + 1; }
    } else { stats.unmatched++; unmatched[key] = 1; }
    if (normal) { var g = tex(normal); if (g) { set.normalMap = g; stats.normals++; } }
    if (cutout) { set.alphaMode = 1; set.alphaCutoff = 0.5; stats.cutouts++; }
    // d < 1 (material_041, Kd = Ks = 1, d 0.5: the tumblers, carafes and window panes) is clear glass.
    // Translucent at the MTL's own coverage reads as the reference's clear glassware; the Glass mode
    // (a reflection-only surface) photographed as opaque white jugs in this courtyard's bright sky.
    if (glass) { set.alphaMode = 2; set.alpha = 0.25; set.roughness = 0.05; stats.glass++; }
    if (metal) stats.metals++;
    if (set.baseColor) set.baseColor = "#" + set.baseColor.map(function (x) {
        var s = Math.round(srgb(Math.max(0, Math.min(1, x))) * 255).toString(16); return s.length < 2 ? "0" + s : s; }).join("");
    if (!material.set(row.id, set)) log("material.set refused", row.name, J(set));
    // A leaf card is ONE quad: both faces must draw (the import left it back-culled).
    if (cutout || glass) node.setProperty(row.id, "faceCullingMode", "none");
}
log("polish", J(stats), "unmatched", J(unmatched).substring(0, 800), "untextured", J(byUntex));

// ---- the light: a late-afternoon sun over the courtyard (the Basic template's sun + Sky Light + the
// physical sky; the first directional IS the sun and casts the shadows) ----
// A directional light's rotation x is its tilt FROM STRAIGHT DOWN toward -Z (x 0 = noon overhead,
// measured: x 40 -> direction (0, -0.77, -0.64)), y turns that about the vertical. So the
// elevation E above the horizon is x = 90 - E.
var SUN_ELEVATION = 42, SUN_AZIMUTH = -70;   // light travelling toward +X, along the courtyard
var sun = world.sunLight();
if (!sun) fail("the Basic template brought no sun");
node.transform(sun, {rotation: {x: 90 - SUN_ELEVATION, y: SUN_AZIMUTH, z: 0}});
log("sun", J(world.sun()));
world.shadows({enabled: true});
// EXPOSURE: 0 EV is the grade for a SUNLIT grey card; the courtyard is mostly in the shade of its own
// arcades, which a photographer opens up by about two stops (the reference is exposed for the shade).
// WHITE BALANCE: light in the shade is the blue sky's (about 7500 K); the film is balanced for it, as a
// photographer would, or the whole courtyard reads cold blue.
world.postFx({exposureEv: DEMO_EV, whiteTemperature: DEMO_WB});

// ---- the tier, the first view, the save ----
world.mode({mode: "high"});
world.photon({enabled: true, tier: "high"});
var VIEWS = [
    {name: "first", position: {x: 8.0, y: 1.7, z: 1.0},  lookAt: {x: 20.0, y: 1.8, z: 10.0}},
    {name: "walk1", position: {x: 21.0, y: 1.7, z: 0.5}, lookAt: {x: 9.0, y: 2.0, z: 9.0}},
    {name: "walk2", position: {x: 14.0, y: 1.6, z: 10.0}, lookAt: {x: 14.0, y: 4.0, z: -1.0}}
];
if (DEMO_PLAN) VIEWS.push({name: "plan", position: {x: 12.0, y: 70.0, z: 12.0}, lookAt: {x: 12.0, y: 0.0, z: 1.4}});
function look(v) { return editor.setCamera({position: v.position, lookAt: v.lookAt}); }
look(VIEWS[0]);
if (project.save() !== true) fail("save failed");
timing.totalMs = Date.now() - tAll;

if (DEMO_PLAN) {      // diagnostics: what surfaces sit where the shots read dark
    var probes = [[21, 1.7, 0.5, -1, 0.0, 0.25], [21, 1.7, 0.5, -1, 0.1, 0.6], [21, 1.7, 0.5, -0.3, 0.0, 1],
                  [8, 1.7, 1, 1, 0.0, 0.2], [8, 1.7, 1, 1, 0.1, 0.8]];
    for (var p = 0; p < probes.length; ++p) {
        var q = probes[p];
        var hits = scene.raycast({x: q[0], y: q[1], z: q[2]}, {x: q[3], y: q[4], z: q[5]}, {maxDistance: 60});
        if (!hits.length) { log("ray", p, "nothing"); continue; }
        var h0 = hits[0], mm = material.get(h0.id);
        log("ray", p, h0.name, "root", h0.rootId === root, "d", h0.distance.toFixed(2), "base", mm.baseColor,
            "map", nameOfTexture(mm.textureAssets ? mm.textureAssets.baseColorMap : ""), "alpha", mm.alphaMode,
            "metal", mm.metallic, "rough", mm.roughness);
    }
}

// ---- evidence ----
var W = DEMO_SHOT_W || 1920, H = DEMO_SHOT_H || 1080;
for (var v = 0; v < VIEWS.length; ++v) {
    look(VIEWS[v]);
    var shot = editor.screenshot(OUT + "/san_miguel_" + VIEWS[v].name + ".png", W, H, [], "scene");
    log("shot", VIEWS[v].name, J(shot.center));
}
look(VIEWS[0]);
editor.frame(120);
var rs = app.renderStats();
var gs = world.giStatus();
var report = {timing: timing, polish: stats, frameMs: rs.frameMs, p95Ms: rs.p95Ms, sceneTriangles: rs.sceneTriangles,
              giAtRest: gs.giAtRest, cascades: gs.cascades, cards: gs.cards, mode: gs.mode};
log("REPORT", J(report));
0;
