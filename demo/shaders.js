// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
//= mirrored in Dice.cpp, DataRow. ditest --data reads them back.
const int ROW_PLANES = 0; //( normal, offset ) per face, body frame, metres
const int ROW_VERTS  = 1; //( vertex, 0 )
const int ROW_EDGE_A = 2; //( first end, 0 ) per edge
const int ROW_EDGE_B = 3; //( second end, 0 )
const int ROW_SLOTS  = 4; //five rows per numbering, two numberings (the d100's tens, units)
const int ROW_GLYPH  = 14;//( origin.xy, 0, 0 ) per digit, atlas px
const int ROW_CELL   = 15;//( x0, y0, x1, y1 ) per digit, atlas px
const int ROW_FACE   = 16;//( incentre, inradius ) per face
const int ROW_FACE_UP = 17;//( up, 0 ) per face: the frame a picture is mapped in

const int TEX_PLASTIC = 0;
const int TEX_MARBLE  = 1;
const int TEX_PEARL   = 2;
const int TEX_METAL   = 3;
const int TEX_GEM     = 4;
const int TEX_STONE   = 5;
const int TEX_WOOD    = 6;
const int TEX_GALAXY  = 7;
const int TEX_IMAGE   = 8;
const int TEX_WIRE    = 9;
const int TEX_CLIP    = 10;

const float SPREAD = 12.0;  //px, the atlas's distance range either side of an edge
const float ONEDGE = 128.0; //the byte on the outline
const float PI     = 3.14159265358979;

uniform sampler2D Data;
uniform sampler2D Atlas;
uniform sampler2D Clip;
uniform sampler2D Picture;

uniform vec2  Resolution;
uniform vec3  CamPos;
uniform vec3  CamRight;
uniform vec3  CamUp;
uniform vec3  CamForward;
uniform float TanHalf;
uniform float PixelAngle;

uniform int   Bodies;
uniform mat3  Rot[ 12 ];
uniform vec3  Pos[ 12 ];
uniform int   Part[ 12 ];

uniform int   FaceCount;
uniform int   EdgeCount;
uniform int   SlotsPerFace;
uniform float Inradius;
uniform float Circumradius;

uniform int   Material;
uniform vec3  Colour;
uniform vec3  Second;
uniform vec3  Ink;
uniform float Gloss;
uniform float Bevel;
uniform float LineWidth;
uniform float Weight;
uniform int   Engraved;
uniform int   Pips;
uniform float AtlasPx;
uniform vec2  AtlasSize;

uniform vec3  LightDir;
uniform float ShadowAmount;
uniform int   TableKind;
uniform vec3  TableColour;
uniform int   IsEffect;
uniform float Mix;
uniform vec2  ClipScale;//the input's used fraction of its texture (FFGL hardware padding)
uniform int   TestFlat; //ditest: dice as flat white, nothing else

vec4 dataAt( int row, int i )
{
	return texelFetch( Data, ivec2( i, row ), 0 );
}

//= mirrored in Maths.h, Pcg(). Integer only: the same on every GPU.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float hash3( ivec3 c )
{
	uint h = pcg( uint( c.x ) ^ pcg( uint( c.y ) ^ pcg( uint( c.z ) + 0x9e3779b9u ) ) );
	return float( h ) * ( 1.0 / 4294967296.0 );
}

float valueNoise( vec3 p )
{
	ivec3 i = ivec3( floor( p ) );
	vec3 f  = fract( p );
	vec3 u  = f * f * ( 3.0 - 2.0 * f );
	float a = mix( hash3( i ), hash3( i + ivec3( 1, 0, 0 ) ), u.x );
	float b = mix( hash3( i + ivec3( 0, 1, 0 ) ), hash3( i + ivec3( 1, 1, 0 ) ), u.x );
	float c = mix( hash3( i + ivec3( 0, 0, 1 ) ), hash3( i + ivec3( 1, 0, 1 ) ), u.x );
	float d = mix( hash3( i + ivec3( 0, 1, 1 ) ), hash3( i + ivec3( 1, 1, 1 ) ), u.x );
	return mix( mix( a, b, u.y ), mix( c, d, u.y ), u.z );
}

float fbm( vec3 p )
{
	float sum = 0.0, amp = 0.5;
	for( int o = 0; o < 4; ++o )
	{
		sum += amp * valueNoise( p );
		p    = p * 2.03 + vec3( 17.1, 3.7, 9.3 );
		amp *= 0.5;
	}
	return sum;
}

//The table, lit, as seen in a reflection or through a gem.
vec3 tableAlbedo( vec2 xz )
{
	if( TableKind == 1 )
	{
		float fibre = valueNoise( vec3( xz * 2400.0, 0.0 ) ) * 0.5 + valueNoise( vec3( xz * 700.0, 3.0 ) ) * 0.5;
		return TableColour * ( 0.82 + 0.3 * fibre );
	}
	if( TableKind == 2 )
	{
		float plank = floor( xz.x / 0.045 );
		float grain = fbm( vec3( xz.x * 60.0, xz.y * 6.0 + plank * 7.3, plank ) );
		float ring  = 0.5 + 0.5 * sin( ( xz.x * 220.0 + grain * 9.0 ) );
		vec3 wood   = TableColour * ( 0.65 + 0.35 * ring ) * ( 0.85 + 0.3 * hash3( ivec3( int( plank ), 7, 1 ) ) );
		float seam  = smoothstep( 0.0, 0.0006, abs( fract( xz.x / 0.045 ) * 0.045 - 0.0005 ) );
		return wood * mix( 0.5, 1.0, seam );
	}
	return vec3( 0.18 );
}

//A photographer's studio: a soft dome, a softbox where the key light is, and
//the table (or a neutral floor) below the horizon.
vec3 env( vec3 d )
{
	if( d.y < 0.0 )
	{
		vec3 floorColour = TableKind == 0 ? vec3( 0.16 ) : tableAlbedo( d.xz * 0.05 ) * 0.6;
		return floorColour * ( 0.6 + 0.4 * clamp( -d.y * 3.0, 0.0, 1.0 ) );
	}
	vec3 sky  = mix( vec3( 0.32, 0.33, 0.36 ), vec3( 0.92, 0.94, 0.98 ), smoothstep( -0.05, 0.85, d.y ) );
	float box = max( dot( d, LightDir ), 0.0 );
	return sky + vec3( 6.0 ) * smoothstep( 0.965, 0.985, box ) + vec3( 0.6 ) * pow( box, 8.0 );
}
`;

// kDice, source/Shaders.cpp
export const DICE = `
//The latest entering plane and the earliest leaving one. Body frame is where
//the planes live; the rotation is orthonormal, so t is metres in both.
bool hitDie( int i, vec3 ro, vec3 rd, out float tN, out float tF, out int face )
{
	tN = 0.0; tF = 0.0; face = -1;
	vec3 oc  = ro - Pos[ i ];
	float b  = dot( oc, rd );
	float c  = dot( oc, oc ) - Circumradius * Circumradius;
	if( b * b - c < 0.0 || ( c > 0.0 && b > 0.0 ) )
		return false;
	mat3 toBody = transpose( Rot[ i ] );
	vec3 o = toBody * oc, d = toBody * rd;
	tN = -1e30; tF = 1e30;
	for( int k = 0; k < FaceCount; ++k )
	{
		vec4 pl    = dataAt( ROW_PLANES, k );
		float den  = dot( pl.xyz, d );
		float dist = dot( pl.xyz, o ) - pl.w;
		if( abs( den ) < 1e-9 )
		{
			if( dist > 0.0 )
				return false;
			continue;
		}
		float t = -dist / den;
		if( den < 0.0 )
		{
			if( t > tN )
			{
				tN   = t;
				face = k;
			}
		}
		else
			tF = min( tF, t );
	}
	return face >= 0 && tN <= tF && tN > 0.0;
}

//How far inside the die (negative) or outside (positive) a body-frame point
//is, by its planes: a lower bound on the true distance, exact at a face.
float field( vec3 p )
{
	float f = -1e30;
	for( int k = 0; k < FaceCount; ++k )
	{
		vec4 pl = dataAt( ROW_PLANES, k );
		f       = max( f, dot( pl.xyz, p ) - pl.w );
	}
	return f;
}

//Signed distance to digit g's outline at glyph-local p (H units), + inside.
float glyphDist( int g, vec2 p )
{
	vec4 o    = dataAt( ROW_GLYPH, g );
	vec4 cell = dataAt( ROW_CELL, g );
	vec2 a    = o.xy + p * AtlasPx;
	if( a.x < cell.x + 0.5 || a.y < cell.y + 0.5 || a.x > cell.z - 0.5 || a.y > cell.w - 0.5 )
		return -SPREAD / AtlasPx;
	float v = texture( Atlas, a / AtlasSize ).r * 255.0;
	return ( v - ONEDGE ) / ( ONEDGE / SPREAD ) / AtlasPx;
}

//A slot's number (and its 6/9 mark) as a signed distance in H units, at
//slot-local q (H units: right, up from the slot's centre).
float slotDist( int base, int s, vec2 q )
{
	vec4 r1 = dataAt( base + 1, s );
	vec4 r2 = dataAt( base + 2, s );
	vec4 r3 = dataAt( base + 3, s );
	vec4 r4 = dataAt( base + 4, s );
	vec2 p  = q + r3.zw;
	float d = -1e3;
	int n   = int( r1.w + 0.5 );
	if( n > 0 )
		d = max( d, glyphDist( int( r2.x + 0.5 ), p - vec2( r3.x, 0.0 ) ) );
	if( n > 1 )
		d = max( d, glyphDist( int( r2.y + 0.5 ), p - vec2( r3.y, 0.0 ) ) );
	int mark = int( r2.z + 0.5 );
	if( mark == 1 )
		d = max( d, r4.z - length( p - r4.xy ) );
	else if( mark == 2 )
	{
		vec2 centre = vec2( 0.5 * ( r4.x + r4.y ), 0.5 * ( r4.z + r4.w ) );
		vec2 halfSz = vec2( 0.5 * ( r4.y - r4.x ), 0.5 * ( r4.w - r4.z ) );
		vec2 e      = abs( p - centre ) - halfSz;
		d = max( d, -( length( max( e, 0.0 ) ) + min( max( e.x, e.y ), 0.0 ) ) );
	}
	return d;
}

//The pips of a d6 face showing v, as a signed distance in metres, + inside.
float pipDist( vec2 q, int v, float room )
{
	float o = 0.5 * room, r = 0.17 * room;
	float best = 1e3;
	vec2 spots[ 6 ];
	int n = 0;
	if( v == 1 || v == 3 || v == 5 )
		spots[ n++ ] = vec2( 0.0 );
	if( v >= 2 )
	{
		spots[ n++ ] = vec2( -o, o );
		spots[ n++ ] = vec2( o, -o );
	}
	if( v >= 4 )
	{
		spots[ n++ ] = vec2( o, o );
		spots[ n++ ] = vec2( -o, -o );
	}
	if( v == 6 )
	{
		spots[ n++ ] = vec2( -o, 0.0 );
		spots[ n++ ] = vec2( o, 0.0 );
	}
	for( int k = 0; k < 6; ++k )
		if( k < n )
			best = min( best, length( q - spots[ k ] ) );
	return r - best;
}

//The ink on face f at body point p: coverage 0..1. Also, for an engraving,
//the ink's signed distance (metres, + inside), the scale of its strokes
//(metres: the glyph height, or a pip's radius) and the distance's gradient
//in the body frame. fp is the pixel's footprint on the face in metres; set
//picks the numbering.
float inkAt( int set, int f, vec3 p, vec3 n, float fp, out vec3 grad, out float dist, out float scale )
{
	grad  = vec3( 0.0 );
	dist  = -1e3;
	scale = 1.0;
	int base = ROW_SLOTS + 5 * set;
	for( int j = 0; j < SlotsPerFace; ++j )
	{
		int s    = f * SlotsPerFace + j;
		vec4 r0  = dataAt( base, s );
		vec4 r1  = dataAt( base + 1, s );
		vec3 up  = r1.xyz;
		vec3 rt  = cross( up, n );
		vec3 rel = p - r0.xyz;
		vec2 q   = vec2( dot( rel, rt ), dot( rel, up ) );
		float d, sc;
		vec2 g;
		if( Pips == 1 )
		{
			int v      = int( dataAt( base + 2, s ).w + 0.5 );
			float room = dataAt( ROW_FACE, f ).w;
			d  = pipDist( q, v, room );
			sc = 0.17 * room;
			float e = 0.01 * room;
			g = vec2( pipDist( q + vec2( e, 0.0 ), v, room ) - pipDist( q - vec2( e, 0.0 ), v, room ),
			          pipDist( q + vec2( 0.0, e ), v, room ) - pipDist( q - vec2( 0.0, e ), v, room ) ) / ( 2.0 * e );
		}
		else
		{
			float h = r0.w;
			vec2 qh = q / h;
			d  = ( slotDist( base, s, qh ) + Weight ) * h;
			sc = h;
			float e = 0.01;
			g = vec2( slotDist( base, s, qh + vec2( e, 0.0 ) ) - slotDist( base, s, qh - vec2( e, 0.0 ) ),
			          slotDist( base, s, qh + vec2( 0.0, e ) ) - slotDist( base, s, qh - vec2( 0.0, e ) ) ) / ( 2.0 * e );
		}
		if( d > dist )
		{
			dist  = d;
			scale = sc;
			grad  = g.x * rt + g.y * up;
		}
	}
	return clamp( dist / max( fp, 1e-7 ) + 0.5, 0.0, 1.0 );
}

//Light reaching world point p past every die but \`self\`: soft, with a
//penumbra that widens with distance from the occluder (a key light about
//7 degrees across). The plane field along the shadow ray is convex, so a
//ternary search finds its minimum: <= 0 means the ray passes through.
float shadowAt( vec3 p, int self )
{
	float vis = 1.0;
	for( int j = 0; j < Bodies; ++j )
	{
		if( j == self )
			continue;
		vec3 oc  = Pos[ j ] - p;
		float tc = dot( oc, LightDir );
		if( tc <= 0.0 )
			continue;
		float d2 = dot( oc, oc ) - tc * tc;
		float R  = Circumradius + 0.06 * tc;
		if( d2 > R * R )
			continue;
		mat3 toBody = transpose( Rot[ j ] );
		vec3 o = toBody * ( p - Pos[ j ] ), d = toBody * LightDir;
		float a = max( tc - Circumradius, 0.0 ), b = tc + Circumradius;
		for( int it = 0; it < 12; ++it )
		{
			float m1 = a + ( b - a ) / 3.0, m2 = b - ( b - a ) / 3.0;
			if( field( o + d * m1 ) < field( o + d * m2 ) )
				b = m2;
			else
				a = m1;
		}
		float tm = 0.5 * ( a + b );
		float s  = clamp( field( o + d * tm ) / ( 0.06 * tm + 1e-5 ), 0.0, 1.0 );
		vis *= s * s * ( 3.0 - 2.0 * s );
	}
	return vis;
}

//Ambient occlusion on the table from the dice standing on it. It reaches
//three circumradii and no further, exactly: beyond that the table (or, on
//the effect, the clip) is untouched, not darkened by a percent.
float contactAO( vec3 p )
{
	float ao = 1.0;
	for( int j = 0; j < Bodies; ++j )
	{
		vec3 dv  = Pos[ j ] - p;
		float d2 = max( dot( dv, dv ), 1e-10 );
		float up = max( dv.y, 0.0 ) * inversesqrt( d2 );
		float reach = 1.0 - smoothstep( 1.5 * Circumradius, 3.0 * Circumradius, sqrt( d2 ) );
		ao *= 1.0 - 0.75 * clamp( 1.5 * Inradius * Inradius / d2 * up, 0.0, 1.0 ) * reach;
	}
	return ao;
}
`;

// kMaterial, source/Shaders.cpp
export const MATERIAL = `
vec3 hueShift( vec3 c, float a )
{
	const vec3 k = vec3( 0.57735 );
	float ca = cos( a );
	return c * ca + cross( k, c ) * sin( a ) + k * dot( k, c ) * ( 1.0 - ca );
}

//Face-planar coordinates in 0..1 for a picture: the face's own frame,
//scaled to its inscribed circle.
vec2 faceUV( int f, vec3 p, vec3 n )
{
	vec4 fc = dataAt( ROW_FACE, f );
	vec3 up = dataAt( ROW_FACE_UP, f ).xyz;
	vec3 rt = cross( up, n );
	vec3 rel = p - fc.xyz;
	return 0.5 + vec2( dot( rel, rt ), dot( rel, up ) ) / ( 2.4 * fc.w );
}

vec3 toLinear( vec3 c )
{
	return pow( max( c, 0.0 ), vec3( 2.2 ) );
}

//Albedo, metalness, roughness of the body at body point p (face f, normal n).
//Everything is a function of the BODY position, so it travels with the die.
void material( int f, vec3 p, vec3 n, vec3 v, out vec3 albedo, out float metal, out float rough )
{
	vec3 q = p / Inradius;
	metal  = 0.0;
	rough  = mix( 0.55, 0.06, Gloss );
	albedo = Colour;
	if( Material == TEX_MARBLE )
	{
		float s    = abs( sin( ( q.x + 0.7 * q.y + 0.4 * q.z ) * 2.6 + 5.0 * fbm( q * 1.3 + 4.0 ) ) );
		float vein = 1.0 - smoothstep( 0.0, 0.22, s );
		float soft = smoothstep( 0.3, 0.8, fbm( q * 0.9 + 11.0 ) );
		albedo     = mix( Colour, mix( Colour, Second, 0.35 ), soft );
		albedo     = mix( albedo, Second, vein );
	}
	else if( Material == TEX_PEARL )
	{
		//Nacre: a soft swirl of the second colour, and an interference
		//sheen whose hue turns with the viewing angle.
		float swirl = fbm( q * 1.2 + vec3( 0.0, 0.0, fbm( q * 0.6 ) * 2.0 ) );
		float sheen = pow( 1.0 - max( dot( n, v ), 0.0 ), 2.0 );
		albedo      = mix( Colour, Second, smoothstep( 0.45, 0.75, swirl ) * 0.3 );
		albedo      = mix( albedo, hueShift( albedo + 0.12, 3.0 * sheen + 1.5 * swirl ), 0.5 * sheen + 0.08 );
		rough       = mix( 0.35, 0.05, Gloss );
	}
	else if( Material == TEX_METAL )
	{
		metal = 1.0;
		float brush = valueNoise( vec3( q.x * 1.5, q.y * 90.0, q.z * 1.5 ) );
		albedo = Colour * ( 0.9 + 0.1 * brush );
		rough  = mix( 0.45, 0.04, Gloss );
	}
	else if( Material == TEX_STONE )
	{
		float mottle = fbm( q * 2.2 );
		float fleck  = step( 0.74, valueNoise( q * 14.0 ) );
		float dark   = step( 0.80, valueNoise( q * 9.0 + 5.0 ) );
		albedo = Colour * ( 0.7 + 0.5 * mottle );
		albedo = mix( albedo, Second, fleck * 0.85 );
		albedo = mix( albedo, albedo * 0.25, dark * 0.8 );
		rough  = mix( 0.8, 0.25, Gloss );
	}
	else if( Material == TEX_WOOD )
	{
		vec3 w     = q + 0.35 * vec3( fbm( q * 1.1 ), 0.0, fbm( q * 1.1 + 7.0 ) );
		float ring = fract( length( w.xz ) * 4.0 );
		float band = smoothstep( 0.0, 0.25, ring ) * smoothstep( 1.0, 0.6, ring );
		float grain = valueNoise( vec3( length( w.xz ) * 60.0, q.y * 3.0, 0.0 ) );
		albedo = mix( Second, Colour, band ) * ( 0.85 + 0.25 * grain );
		rough  = mix( 0.7, 0.2, Gloss );
	}
	else if( Material == TEX_GALAXY )
	{
		float neb  = pow( fbm( q * 1.2 + 2.0 ), 2.2 ) * 2.2;
		float neb2 = pow( fbm( q * 2.1 + 9.0 ), 3.0 ) * 1.5;
		albedo = Colour * 0.15 + Second * neb * 0.5 + hueShift( Second, 1.6 ) * neb2 * 0.4;
		vec3 cellP = q * 9.0;
		ivec3 cell = ivec3( floor( cellP ) );
		vec3 star  = vec3( hash3( cell ), hash3( cell + 31 ), hash3( cell + 77 ) );
		float dist = length( fract( cellP ) - star );
		albedo += vec3( 1.0 ) * smoothstep( 0.09, 0.0, dist ) * step( 0.55, hash3( cell + 5 ) ) * 3.0;
		rough  = mix( 0.4, 0.04, Gloss );
	}
	else if( Material == TEX_IMAGE )
		albedo = toLinear( texture( Picture, faceUV( f, p, n ) ).rgb );
	else if( Material == TEX_CLIP )
		albedo = toLinear( texture( Clip, faceUV( f, p, n ) * ClipScale ).rgb );
}

vec3 ambient( vec3 n, float height )
{
	//The table bounces a little light up; the underside of a die is darker
	//where it nearly meets the table.
	vec3 sky  = vec3( 0.42, 0.43, 0.46 );
	vec3 bounce = ( TableKind == 0 ? vec3( 0.10 ) : tableAlbedo( vec2( 0.0 ) ) * 0.35 );
	float occ = mix( 0.55, 1.0, smoothstep( 0.0, 1.4 * Inradius, height ) );
	return mix( bounce, sky, 0.5 + 0.5 * n.y ) * occ;
}

vec3 lightSurface( vec3 albedo, float metal, float rough, vec3 n, vec3 v, vec3 p, float vis )
{
	vec3 l      = LightDir;
	float ndl   = max( dot( n, l ), 0.0 );
	vec3 h      = normalize( l + v );
	float shine = 2.0 / max( rough * rough, 1e-3 ) - 2.0;
	float spec  = pow( max( dot( n, h ), 0.0 ), shine ) * ( shine + 8.0 ) / 8.0;
	vec3 f0     = mix( vec3( 0.04 ), albedo, metal );
	vec3 fres   = f0 + ( 1.0 - f0 ) * pow( 1.0 - max( dot( n, v ), 0.0 ), 5.0 );
	vec3 diffuse = albedo * ( 1.0 - metal ) * ( ndl * vis * 1.25 + ambient( n, p.y ) );
	vec3 refl    = env( reflect( -v, n ) );
	//A rough surface sees a blurred studio: lean the reflection toward its mean.
	refl = mix( refl, vec3( 0.55 ), clamp( rough * 1.4, 0.0, 1.0 ) );
	return diffuse + fres * ( spec * ndl * vis * 1.25 + refl * mix( 0.9, 0.25, rough ) );
}

//The surface normal with the edges rounded: within Bevel of a neighbouring
//plane, lean toward it, reaching the bisector on the edge itself.
vec3 bevelNormal( int f, vec3 p )
{
	vec3 n = dataAt( ROW_PLANES, f ).xyz;
	if( Bevel <= 0.0 )
		return n;
	vec3 sum = n;
	for( int k = 0; k < FaceCount; ++k )
	{
		if( k == f )
			continue;
		vec4 pl = dataAt( ROW_PLANES, k );
		float e = pl.w - dot( pl.xyz, p );
		float w = 1.0 - smoothstep( 0.0, Bevel, e );
		sum += pl.xyz * w * w;
	}
	return normalize( sum );
}

//A gem: reflection off the front, refraction through the body to the far
//face (whose number is seen from inside, mirrored), absorbed on the way by
//the colour, out into the studio.
vec3 gem( int i, int f, vec3 pb, vec3 nb, vec3 rdb, float fp, int set )
{
	mat3 R   = Rot[ i ];
	vec3 v   = -rdb;
	float F  = 0.04 + 0.96 * pow( 1.0 - max( dot( nb, v ), 0.0 ), 5.0 );
	vec3 refl = env( R * reflect( rdb, nb ) );
	vec3 inside = refract( rdb, nb, 1.0 / 1.55 );
	float tE = 1e30;
	int ef   = 0;
	for( int k = 0; k < FaceCount; ++k )
	{
		vec4 pl   = dataAt( ROW_PLANES, k );
		float den = dot( pl.xyz, inside );
		if( den > 1e-6 )
		{
			float t = ( pl.w - dot( pl.xyz, pb ) ) / den;
			if( t < tE )
			{
				tE = t;
				ef = k;
			}
		}
	}
	vec3 pe    = pb + inside * tE;
	vec3 ne    = dataAt( ROW_PLANES, ef ).xyz;
	vec3 g;
	float gd, gs;
	float ink  = inkAt( set, ef, pe, ne, fp * 1.5, g, gd, gs );
	vec3 out_  = refract( inside, -ne, 1.55 );
	if( dot( out_, out_ ) < 0.5 )
		out_ = reflect( inside, -ne );
	//Beer-Lambert with the colour as the transmission across the die (two
	//inradii): thinner paths are paler, thicker ones deeper. The key light
	//scattered inside is what makes a gem glow on a dark table.
	vec3 absorb  = pow( max( Colour, vec3( 1e-3 ) ), vec3( tE / ( 2.0 * Inradius ) ) );
	vec3 through = env( R * out_ ) * absorb + Colour * 0.35 * sqrt( absorb ) * max( LightDir.y, 0.0 );
	through      = mix( through, Ink * absorb * 0.8, ink );
	float spec   = pow( max( dot( nb, normalize( transpose( R ) * LightDir + v ) ), 0.0 ), 300.0 ) * 6.0;
	return mix( through, refl, F ) + vec3( spec );
}
`;

// kFragment, source/Shaders.cpp
export const FRAGMENT = `
in vec2 uv;
out vec4 fragColour;

//One ray, premultiplied linear rgba.
vec4 trace( vec3 ro, vec3 rd )
{
	//-------------------------------------------------------------------
	// The wireframe draws every edge, front bright and back dim, and the
	// numbers on the faces turned toward the camera.
	//-------------------------------------------------------------------
	if( Material == TEX_WIRE )
	{
		vec3 glow   = vec3( 0.0 );
		float cover = 0.0;
		for( int i = 0; i < Bodies; ++i )
		{
			vec3 oc = ro - Pos[ i ];
			float b = dot( oc, rd );
			float R = Circumradius + 3.0 * LineWidth;
			if( b * b - ( dot( oc, oc ) - R * R ) < 0.0 )
				continue;
			mat3 toBody = transpose( Rot[ i ] );
			vec3 o = toBody * oc, d = toBody * rd;
			float tN, tF;
			int face;
			bool front = hitDie( i, ro, rd, tN, tF, face );
			for( int e = 0; e < EdgeCount; ++e )
			{
				vec3 a  = dataAt( ROW_EDGE_A, e ).xyz;
				vec3 v  = dataAt( ROW_EDGE_B, e ).xyz - a;
				vec3 w0 = o - a;
				float B = dot( d, v ), C = dot( v, v ), D = dot( d, w0 ), E = dot( v, w0 );
				float den = C - B * B;
				float te  = den > 1e-12 ? clamp( ( E - B * D ) / den, 0.0, 1.0 ) : 0.0;
				vec3 pe   = a + v * te;
				float sc  = max( dot( pe - o, d ), 0.0 );
				float dist = length( o + d * sc - pe );
				float fpx  = max( sc * PixelAngle, 1e-7 );
				float halfW = 0.5 * LineWidth;
				float core = clamp( ( halfW - dist ) / fpx + 0.5, 0.0, 1.0 ) * min( 1.0, LineWidth / fpx );
				float halo = exp( -dist / ( 0.7 * LineWidth ) ) * 0.5;
				bool near_ = !front || sc <= tN + LineWidth;
				float k    = near_ ? 1.0 : 0.3;
				glow += Colour * ( core + halo ) * k;
				cover = max( cover, min( 1.0, ( core + halo ) * k ) );
			}
			if( front )
			{
				vec3 pb  = o + d * tN;
				vec3 nb  = dataAt( ROW_PLANES, face ).xyz;
				float fp = tN * PixelAngle / max( abs( dot( nb, d ) ), 0.2 );
				vec3 g;
				float gd, gs;
				float ink = inkAt( Part[ i ], face, pb, nb, fp, g, gd, gs );
				glow += Ink * ink * 1.5;
				cover = max( cover, ink );
			}
		}
		vec4 table_ = vec4( 0.0 );
		if( rd.y < 0.0 && TableKind != 0 )
		{
			vec3 p = ro + rd * ( -ro.y / rd.y );
			table_ = vec4( tableAlbedo( p.xz ) * ( 1.25 * max( LightDir.y, 0.0 ) + 0.35 ), 1.0 );
		}
		cover = clamp( cover, 0.0, 1.0 );
		return vec4( glow, cover ) + table_ * ( 1.0 - cover );
	}

	//-------------------------------------------------------------------
	// The nearest die.
	//-------------------------------------------------------------------
	float best = 1e30;
	int hitI = -1, hitF = -1;
	for( int i = 0; i < Bodies; ++i )
	{
		float tN, tF;
		int face;
		if( hitDie( i, ro, rd, tN, tF, face ) && tN < best )
		{
			best = tN;
			hitI = i;
			hitF = face;
		}
	}

	if( hitI >= 0 && TestFlat == 1 )
		return vec4( 1.0 );
	if( hitI >= 0 )
	{
		mat3 R      = Rot[ hitI ];
		mat3 toBody = transpose( R );
		vec3 pw     = ro + rd * best;
		vec3 pb     = toBody * ( pw - Pos[ hitI ] );
		vec3 rdb    = toBody * rd;
		vec3 nFace  = dataAt( ROW_PLANES, hitF ).xyz;
		vec3 nb     = bevelNormal( hitF, pb );
		float fp    = best * PixelAngle / max( abs( dot( nFace, rdb ) ), 0.2 );
		int set     = Part[ hitI ];
		vec3 grad;
		float inkDist, inkScale;
		float ink   = inkAt( set, hitF, pb, nFace, fp, grad, inkDist, inkScale );
		float vis   = shadowAt( pw + R * nb * 1e-5, hitI );

		vec3 colour;
		if( Material == TEX_GEM )
		{
			colour = gem( hitI, hitF, pb, nb, rdb, fp, set );
			vec3 inkLit = lightSurface( Ink, 0.0, 0.5, R * nb, -rd, pw, vis );
			colour = mix( colour, inkLit, ink );
		}
		else
		{
			vec3 albedo;
			float metal, rough;
			material( hitF, pb, nb, -rdb, albedo, metal, rough );
			vec3 n = nb;
			if( Engraved == 1 )
			{
				//A V-groove: inside the outline the wall leans toward the
				//middle of the stroke, across a band 6% of the glyph's height,
				//so the light catches one side of every stroke and not the other.
				vec3 g     = grad - nFace * dot( grad, nFace );
				float band = 0.06 * inkScale;
				float wall = smoothstep( 0.0, 0.15 * band, inkDist ) * ( 1.0 - smoothstep( 0.6 * band, band, inkDist ) );
				if( length( g ) > 1e-6 )
					n = normalize( n + normalize( g ) * 1.2 * wall );
				albedo = mix( albedo, Ink, ink );
				metal  = mix( metal, 0.0, ink );
				rough  = mix( rough, 0.6, ink );
			}
			else
			{
				albedo = mix( albedo, Ink, ink );
				metal  = mix( metal, 0.0, ink );
				rough  = mix( rough, 0.4, ink );
			}
			colour = lightSurface( albedo, metal, rough, R * n, -rd, pw, vis );
		}
		return vec4( colour, 1.0 );
	}

	//-------------------------------------------------------------------
	// The table: drawn, or (None) only the shadows, as premultiplied black.
	//-------------------------------------------------------------------
	if( rd.y >= -1e-6 )
		return TableKind == 0 ? vec4( 0.0 ) : vec4( env( rd ), 1.0 );
	vec3 p    = ro + rd * ( -ro.y / rd.y );
	float vis = shadowAt( p, -1 );
	float ao  = contactAO( p );
	float lit = mix( 1.0, vis * ao, ShadowAmount );
	if( TableKind == 0 )
		return vec4( 0.0, 0.0, 0.0, clamp( 1.0 - lit, 0.0, 1.0 ) );
	vec3 albedo = tableAlbedo( p.xz );
	float ndl   = max( LightDir.y, 0.0 );
	vec3 c      = albedo * ( 1.25 * ndl * mix( 1.0, vis, ShadowAmount ) + 0.35 * mix( 1.0, ao, ShadowAmount ) );
	return vec4( c, 1.0 );
}

vec3 rayFor( vec2 pixel )
{
	vec2 ndc = pixel / Resolution * 2.0 - 1.0;
	float aspect = Resolution.x / Resolution.y;
	return normalize( CamForward + CamRight * ( ndc.x * TanHalf * aspect ) + CamUp * ( ndc.y * TanHalf ) );
}

//Highlights roll off instead of clipping; below 0.6 nothing changes.
vec3 shoulder( vec3 c )
{
	vec3 soft = 0.6 + 0.4 * ( 1.0 - exp( -( c - 0.6 ) / 0.4 ) );
	return mix( c, soft, step( 0.6, c ) );
}

void main()
{
	vec2 pixel = gl_FragCoord.xy;
	vec3 centre = rayFor( pixel );

	//Supersample only where a die is near: four rotated-grid rays inside a
	//bound one pixel wider than the die's circumsphere; one ray elsewhere.
	bool near_ = false;
	for( int i = 0; i < Bodies; ++i )
	{
		vec3 oc  = CamPos - Pos[ i ];
		float b  = dot( oc, centre );
		float d2 = dot( oc, oc ) - b * b;
		float glow = Material == TEX_WIRE ? 3.0 * LineWidth : 0.0;
		float R    = Circumradius + glow + 2.0 * PixelAngle * max( -b, 0.0 );
		if( d2 < R * R )
			near_ = true;
	}

	vec4 acc = vec4( 0.0 );
	if( near_ )
	{
		const vec2 offsets[ 4 ] = vec2[ 4 ]( vec2( 0.125, 0.375 ), vec2( -0.375, 0.125 ), vec2( -0.125, -0.375 ), vec2( 0.375, -0.125 ) );
		for( int s = 0; s < 4; ++s )
			acc += trace( CamPos, rayFor( pixel + offsets[ s ] ) );
		acc *= 0.25;
	}
	else
		acc = trace( CamPos, centre );

	//Linear to display, on straight colour; then premultiplied again.
	vec3 straight = acc.a > 1e-6 ? acc.rgb / acc.a : vec3( 0.0 );
	straight      = pow( shoulder( max( straight, 0.0 ) ), vec3( 1.0 / 2.2 ) );
	vec4 dice     = vec4( clamp( straight, 0.0, 1.0 ) * acc.a, acc.a );

	if( IsEffect == 1 )
	{
		vec4 under = texture( Clip, uv * ClipScale );
		vec4 over  = dice + under * ( 1.0 - dice.a );
		fragColour = mix( under, over, Mix );
	}
	else
		fragColour = dice;
}
`;
