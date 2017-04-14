#version 150
// https://dl.dropboxusercontent.com/u/11542084/ssao_nohalo_1.0

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;
uniform sampler2D u_depthTexture;

#define PI    3.14159265

float width = textureSize(u_depthTexture, 0).x; //texture width
float height = textureSize(u_depthTexture, 0).y; //texture height

//--------------------------------------------------------
//a list of user parameters

float near = 0.1; //Z-near
float far = 1000.0; //Z-far

int samples = 5; //samples on the first ring (3 - 5)
int rings = 5; //ring count (3 - 5)

float radius = 0.2; //ao radius

float diffarea = 0.5; //self-shadowing reduction
float gdisplace = 0.4; //gauss bell center

float lumInfluence = 0.8; //how much luminance affects occlusion

bool noise = false; //use noise instead of pattern for sample dithering?
bool onlyAO = false; //use only ambient occlusion pass?

//--------------------------------------------------------

out vec4 fragColor;

vec2 rand(in vec2 coord) //generating noise/pattern texture for dithering
{
  float noiseX = ((fract(1.0-coord.s*(width/2.0))*0.25)+(fract(coord.t*(height/2.0))*0.75))*2.0-1.0;
  float noiseY = ((fract(1.0-coord.s*(width/2.0))*0.75)+(fract(coord.t*(height/2.0))*0.25))*2.0-1.0;
  
  if (noise)
  {
      noiseX = clamp(fract(sin(dot(coord ,vec2(12.9898,78.233))) * 43758.5453),0.0,1.0)*2.0-1.0;
      noiseY = clamp(fract(sin(dot(coord ,vec2(12.9898,78.233)*2.0)) * 43758.5453),0.0,1.0)*2.0-1.0;
  }
  return vec2(noiseX,noiseY)*0.001;
}

float readDepth(in vec2 coord) 
{
  //if (gl_TexCoord[3].x<0.0||gl_TexCoord[3].y<0.0) return 1.0;
  return (2.0 * near) / (far + near - texture(u_depthTexture, coord ).x * (far-near));
}

float compareDepths(in float depth1, in float depth2,inout int far)
{   
  float garea = 2.0; //gauss bell width    
  float diff = (depth1 - depth2)*100.0; //depth difference (0-100)
  //reduce left bell width to avoid self-shadowing 
  if (diff<gdisplace)
  {
  garea = diffarea;
  }else{
  far = 1;
  }
  
  float gauss = pow(2.7182,-2.0*(diff-gdisplace)*(diff-gdisplace)/(garea*garea));
  return gauss;
}  

float calAO(float depth,float dw, float dh)
{   
  float dd = (1.0-depth)*radius;

  float temp = 0.0;
  float temp2 = 0.0;
  float coordw = v_texCoord.x + dw*dd;
  float coordh = v_texCoord.y + dh*dd;
  float coordw2 = v_texCoord.x - dw*dd;
  float coordh2 = v_texCoord.y - dh*dd;
  
  vec2 coord = vec2(coordw , coordh);
  vec2 coord2 = vec2(coordw2, coordh2);
  
  int far = 0;
  temp = compareDepths(depth, readDepth(coord),far);
  //DEPTH EXTRAPOLATION:
  if (far > 0)
  {
    temp2 = compareDepths(readDepth(coord2),depth,far);
    temp += (1.0-temp)*temp2;
  }
  
  return temp;
} 

void main(void)
{
  vec2 noise = rand(v_texCoord); 
  float depth = readDepth(v_texCoord);
  float d;
  
  float w = (1.0 / width)/clamp(depth,0.25,1.0)+(noise.x*(1.0-noise.x));
  float h = (1.0 / height)/clamp(depth,0.25,1.0)+(noise.y*(1.0-noise.y));
  
  float pw;
  float ph;
  
  float ao;
  float s;
  
  int ringsamples;
  
  for (int i = 1; i <= rings; i += 1)
  {

    ringsamples = i * samples;
    for (int j = 0 ; j < ringsamples ; j += 1)
    {
      float step = PI*2.0 / float(ringsamples);
      pw = (cos(float(j)*step)*float(i));
      ph = (sin(float(j)*step)*float(i));
      ao += calAO(depth,pw*w,ph*h);
      s += 1.0;
    }
  }
  
  
  ao /= s;
  ao = 1.0-ao;  
  

  vec3 color = texture2D(u_sceneTexture,v_texCoord).rgb;
  
  vec3 lumcoeff = vec3(0.299,0.587,0.114);
  float lum = dot(color.rgb, lumcoeff);
  vec3 luminance = vec3(lum, lum, lum);
  
  //fragColor = vec4(vec3(color*mix(vec3(ao),vec3(1.0),luminance*lumInfluence)),1.0); //mix(color*ao, white, luminance)
  fragColor = vec4(color*vec3(ao),1.0);
  //fragColor = vec4(color,1.0);
  if(onlyAO)
  fragColor = vec4(vec3(mix(vec3(ao),vec3(1.0),luminance*lumInfluence)),1.0); //ambient occlusion only
}