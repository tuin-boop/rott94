/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#ifdef DOS
#include <malloc.h>
#include <dos.h>
#include <conio.h>
#include <io.h>
#endif

#include <stdlib.h>
#include <sys/stat.h>

/* OpenGL's Windows declarations must be parsed before rt_def.h defines the
   legacy function-like LONG() conversion macro. */
#if USE_SDL
#include "SDL_opengl.h"
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#endif

#include "modexlib.h"
#include "lumpy.h"
//MED
#include "memcheck.h"
#include "rt_util.h"
#include "rt_net.h" // for GamePaused
#include "myprint.h"

static void StretchMemPicture ();
// GLOBAL VARIABLES

bool StretchScreen=0;//bn�++
extern bool iG_aimCross;
extern bool sdl_fullscreen;
extern int iG_X_center;
extern int iG_Y_center;
byte 	   *iG_buf_center;
  
int    linewidth;
//int    ylookup[MAXSCREENHEIGHT];
int    ylookup[ROTT_MAX_RENDER_HEIGHT];
byte  *page1start;
byte  *page2start;
byte  *page3start;
int    screensize;
byte  *bufferofs;
byte  *displayofs;
bool graphicsmode=false;
byte        *bufofsTopLimit;
byte        *bufofsBottomLimit;

void DrawCenterAim ();

#ifdef DOS

/*
====================
=
= GraphicsMode
=
====================
*/
void GraphicsMode ( void )
{
union REGS regs;

regs.w.ax = 0x13;
int386(0x10,&regs,&regs);
graphicsmode=true;
}

/*
====================
=
= SetTextMode
=
====================
*/
void SetTextMode ( void )
{

union REGS regs;

regs.w.ax = 0x03;
int386(0x10,&regs,&regs);
graphicsmode=false;

}

/*
====================
=
= TurnOffTextCursor
=
====================
*/
void TurnOffTextCursor ( void )
{

union REGS regs;

regs.w.ax = 0x0100;
regs.w.cx = 0x2000;
int386(0x10,&regs,&regs);

}

#if 0
/*
====================
=
= TurnOnTextCursor
=
====================
*/
void TurnOnTextCursor ( void )
{

union REGS regs;

regs.w.ax = 0x03;
int386(0x10,&regs,&regs);

}
#endif

/*
====================
=
= WaitVBL
=
====================
*/
void WaitVBL( void )
{
   unsigned char i;

   i=inp(0x03da);
   while ((i&8)==0)
      i=inp(0x03da);
   while ((i&8)==1)
      i=inp(0x03da);
}


/*
====================
=
= VL_SetLineWidth
=
= Line witdh is in WORDS, 40 words is normal width for vgaplanegr
=
====================
*/

void VL_SetLineWidth (unsigned width)
{
   int i,offset;

//
// set wide virtual screen
//
   outpw (CRTC_INDEX,CRTC_OFFSET+width*256);

//
// set up lookup tables
//
   linewidth = width*2;

   offset = 0;

   for (i=0;i<iGLOBAL_SCREENHEIGHT;i++)
      {
      ylookup[i]=offset;
      offset += linewidth;
      }
}

/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void VL_SetVGAPlaneMode ( void )
{
    GraphicsMode();
    VL_DePlaneVGA ();
    VL_SetLineWidth (48);
    screensize=208*iGLOBAL_SCREENBWIDE*2;//bna++ *2
    page1start=0xa0200;
    page2start=0xa0200+screensize;
    page3start=0xa0200+(2u*screensize);
    displayofs = page1start;
    bufferofs = page2start;
    XFlipPage ();
}

/*
=======================
=
= VL_CopyPlanarPage
=
=======================
*/
void VL_CopyPlanarPage ( byte * src, byte * dest )
{
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      memcpy(dest,src,screensize);
      }
}

/*
=======================
=
= VL_CopyPlanarPageToMemory
=
=======================
*/
void VL_CopyPlanarPageToMemory ( byte * src, byte * dest )
{
   byte * ptr;
   int plane,a,b;

   for (plane=0;plane<4;plane++)
      {
      ptr=dest+plane;
      VGAREADMAP(plane);
      for (a=0;a<200;a++)
         for (b=0;b<80;b++,ptr+=4)
            *(ptr)=*(src+(a*linewidth)+b);
      }
}

/*
=======================
=
= VL_CopyBufferToAll
=
=======================
*/
void VL_CopyBufferToAll ( byte *buffer )
{
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      if (page1start!=buffer)
         memcpy((byte *)page1start,(byte *)buffer,screensize);
      if (page2start!=buffer)
         memcpy((byte *)page2start,(byte *)buffer,screensize);
      if (page3start!=buffer)
         memcpy((byte *)page3start,(byte *)buffer,screensize);
      }
}

/*
=======================
=
= VL_CopyDisplayToHidden
=
=======================
*/
void VL_CopyDisplayToHidden ( void )
{
   VL_CopyBufferToAll ( displayofs );
}

/*
=================
=
= VL_ClearBuffer
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearBuffer (unsigned buf, byte color)
{
  VGAMAPMASK(15);
  memset((byte *)buf,color,screensize);
}

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (byte color)
{
  VGAMAPMASK(15);
  memset((byte *)(0xa000<<4),color,0x10000);
}

/*
=================
=
= VL_DePlaneVGA
=
=================
*/

void VL_DePlaneVGA (void)
{

//
// change CPU addressing to non linear mode
//

//
// turn off chain 4 and odd/even
//
        outp (SC_INDEX,SC_MEMMODE);
        outp (SC_DATA,(inp(SC_DATA)&~8)|4);

        outp (SC_INDEX,SC_MAPMASK);         // leave this set throughout

//
// turn off odd/even and set write mode 0
//
        outp (GC_INDEX,GC_MODE);
        outp (GC_DATA,inp(GC_DATA)&~0x13);

//
// turn off chain
//
        outp (GC_INDEX,GC_MISCELLANEOUS);
        outp (GC_DATA,inp(GC_DATA)&~2);

//
// clear the entire buffer space, because int 10h only did 16 k / plane
//
        VL_ClearVideo (0);

//
// change CRTC scanning from doubleword to byte mode, allowing >64k scans
//
        outp (CRTC_INDEX,CRTC_UNDERLINE);
        outp (CRTC_DATA,inp(CRTC_DATA)&~0x40);

        outp (CRTC_INDEX,CRTC_MODE);
        outp (CRTC_DATA,inp(CRTC_DATA)|0x40);
}


/*
=================
=
= XFlipPage
=
=================
*/

void XFlipPage ( void )
{
   displayofs=bufferofs;

//   _disable();

   outp(CRTC_INDEX,CRTC_STARTHIGH);
   outp(CRTC_DATA,((displayofs&0x0000ffff)>>8));

//   _enable();

   bufferofs += screensize;
   if (bufferofs > page3start)
      bufferofs = page1start;
}

#else

#include "SDL.h"

#ifndef STUB_FUNCTION

/* rt_def.h isn't included, so I just put this here... */
#if !defined(ANSIESC)
#define STUB_FUNCTION fprintf(stderr,"STUB: %s at " __FILE__ ", line %d, thread %d\n",__FUNCTION__,__LINE__,getpid())
#else
#define STUB_FUNCTION
#endif

#endif

/*
====================
=
= GraphicsMode
=
====================
*/
SDL_Window *sdl_window = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture *sdl_texture = NULL;
SDL_Surface *sdl_surface = NULL;
static SDL_Surface *unstretch_sdl_surface = NULL;
static SDL_Texture *sdl_hint_texture = NULL;
static bool advancedGpuRenderer = false;
static SDL_GLContext advancedGlContext = NULL;
static GLuint advancedGlTexture = 0;
static uint32_t *advancedGlPixels = NULL;
static bool advancedWorldReady = false;
static GLuint advancedWallTextures[1024];
static GLuint advancedFloorTexture = 0;
static GLuint advancedCeilingTexture = 0;
static GLuint advancedSkyTexture = 0;
static GLuint advancedHudTexture = 0;
static uint32_t *advancedHudPixels = NULL;
static int advancedHudWidth = 0;
static const byte *advancedFloorSource = NULL;
static const byte *advancedCeilingSource = NULL;
static int advancedSkyTextureRevision = -1;
typedef struct
{
	int lump;
	GLuint texture;
	int width,height;
} advanced_patch_texture_t;
static advanced_patch_texture_t advancedPatchTextures[512];
extern unsigned int lastInteraction;
extern byte *AdvancedRendererGetWallPixels(int lump);
extern byte *AdvancedRendererGetPatchPixels(int lump);
extern byte *AdvancedRendererGetTransPatchPixels(int lump);

void AdvancedRendererPrepareHud(void)
{
	int y,band;
	byte *pixels;
	if (!advancedGpuRenderer || !sdl_surface || !sdl_surface->pixels)
		return;
	band=iGLOBAL_SCREENHEIGHT<40 ? iGLOBAL_SCREENHEIGHT : 40;
	pixels=(byte *)sdl_surface->pixels;
	for (y=iGLOBAL_SCREENHEIGHT-band;y<iGLOBAL_SCREENHEIGHT;y++)
		memset(pixels+y*sdl_surface->pitch,255,(size_t)iGLOBAL_SCREENWIDTH);
}

static void AdvancedHudOverlay(int drawableWidth,int drawableHeight)
{
	int x,y,band;
	byte *source;
	SDL_Palette *palette;
	float top;
	if (!sdl_surface || !sdl_surface->pixels ||
		!sdl_surface->format->palette) return;
	band=iGLOBAL_SCREENHEIGHT<40 ? iGLOBAL_SCREENHEIGHT : 40;
	if (!advancedHudPixels || advancedHudWidth!=iGLOBAL_SCREENWIDTH)
	{
		free(advancedHudPixels);
		advancedHudPixels=(uint32_t *)malloc((size_t)iGLOBAL_SCREENWIDTH*
			(size_t)band*sizeof(uint32_t));
		if (!advancedHudPixels) return;
		advancedHudWidth=iGLOBAL_SCREENWIDTH;
		if (advancedHudTexture) glDeleteTextures(1,&advancedHudTexture);
		advancedHudTexture=0;
	}
	palette=sdl_surface->format->palette;
	for (y=0;y<band;y++)
	{
		source=(byte *)sdl_surface->pixels+
			(iGLOBAL_SCREENHEIGHT-band+y)*sdl_surface->pitch;
		for (x=0;x<iGLOBAL_SCREENWIDTH;x++)
		{
			byte index=source[x];
			if (index==255) advancedHudPixels[y*iGLOBAL_SCREENWIDTH+x]=0;
			else
			{
				SDL_Color c=palette->colors[index];
				advancedHudPixels[y*iGLOBAL_SCREENWIDTH+x]=0xff000000u|
					((uint32_t)c.r<<16)|((uint32_t)c.g<<8)|c.b;
			}
		}
	}
	if (!advancedHudTexture)
	{
		glGenTextures(1,&advancedHudTexture);
		glBindTexture(GL_TEXTURE_2D,advancedHudTexture);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,iGLOBAL_SCREENWIDTH,band,0,
			GL_BGRA,GL_UNSIGNED_BYTE,advancedHudPixels);
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D,advancedHudTexture);
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,iGLOBAL_SCREENWIDTH,band,
			GL_BGRA,GL_UNSIGNED_BYTE,advancedHudPixels);
	}
	top=1.0f-(float)band/(float)iGLOBAL_SCREENHEIGHT;
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER,0.05f);
	glColor3f(1,1,1);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0); glVertex2f(0,top);
	glTexCoord2f(1,0); glVertex2f(1,top);
	glTexCoord2f(1,1); glVertex2f(1,1);
	glTexCoord2f(0,1); glVertex2f(0,1);
	glEnd();
	glDisable(GL_ALPHA_TEST);
}

static GLuint AdvancedWallTexture(int lump)
{
	byte *source;
	uint32_t rgba[64*64];
	SDL_Palette *palette;
	int x,y;

	lump&=0x3ff;
	if (lump<=0) return 0;
	if (advancedWallTextures[lump]) return advancedWallTextures[lump];
	source=AdvancedRendererGetWallPixels(lump);
	if (!source || !sdl_surface || !sdl_surface->format->palette) return 0;
	palette=sdl_surface->format->palette;
	for (y=0;y<64;y++)
		for (x=0;x<64;x++)
		{
			SDL_Color c=palette->colors[source[(x<<6)+y]];
			rgba[(y<<6)+x]=0xff000000u|((uint32_t)c.r<<16)|
				((uint32_t)c.g<<8)|c.b;
		}
	glGenTextures(1,&advancedWallTextures[lump]);
	glBindTexture(GL_TEXTURE_2D,advancedWallTextures[lump]);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_BGRA,
		GL_UNSIGNED_BYTE,rgba);
	return advancedWallTextures[lump];
}

static void AdvancedWallFace(int lump,float x0,float y0,float z0,
	float x1,float y1,float z1,float x2,float y2,float z2,
	float x3,float y3,float z3)
{
	GLuint texture=AdvancedWallTexture(lump);
	if (!texture) return;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D,texture);
	glBegin(GL_QUADS);
	glTexCoord2f(0,(y2-y0)); glVertex3f(x0,y0,z0);
	glTexCoord2f(1,(y2-y0)); glVertex3f(x1,y1,z1);
	glTexCoord2f(1,0); glVertex3f(x2,y2,z2);
	glTexCoord2f(0,0); glVertex3f(x3,y3,z3);
	glEnd();
}

static GLuint AdvancedPatchTexture(int lump,bool transparentPatch,
	int *outWidth,int *outHeight)
{
	byte *shape,*column;
	patch_t *patch;
	uint32_t *rgba;
	SDL_Palette *palette;
	int slot,x,y,width,height;
	int cacheKey=transparentPatch ? (lump|0x40000000) : lump;

	if (lump<0) return 0;
	for (slot=0;slot<512;slot++)
		if (advancedPatchTextures[slot].texture &&
			advancedPatchTextures[slot].lump==cacheKey)
			{
			if (outWidth) *outWidth=advancedPatchTextures[slot].width;
			if (outHeight) *outHeight=advancedPatchTextures[slot].height;
			return advancedPatchTextures[slot].texture;
			}
	for (slot=0;slot<512;slot++)
		if (!advancedPatchTextures[slot].texture) break;
	if (slot==512) return 0;
	shape=transparentPatch ? AdvancedRendererGetTransPatchPixels(lump) :
		AdvancedRendererGetPatchPixels(lump);
	if (!shape || !sdl_surface || !sdl_surface->format->palette) return 0;
	patch=(patch_t *)shape;
	if (transparentPatch)
	{
		transpatch_t *trans=(transpatch_t *)shape;
		width=trans->width;
		height=trans->height;
	}
	else
	{
		width=patch->width;
		height=patch->height;
	}
	if (width<=0 || width>320 || height<=0 || height>1024) return 0;
	rgba=(uint32_t *)calloc((size_t)width*(size_t)height,
		sizeof(uint32_t));
	if (!rgba) return 0;
	palette=sdl_surface->format->palette;
	for (x=0;x<width;x++)
	{
		int offset,length;
		column=shape+(transparentPatch ?
			((transpatch_t *)shape)->collumnofs[x] : patch->collumnofs[x]);
		offset=*column++;
		while (offset!=255)
		{
			length=*column++;
			if (*column==254)
			{
				column++;
				offset=*column++;
				continue;
			}
			for (y=0;y<length && offset+y<height;y++)
			{
				SDL_Color c=palette->colors[column[y]];
				rgba[(offset+y)*width+x]=0xff000000u|
					((uint32_t)c.r<<16)|((uint32_t)c.g<<8)|c.b;
			}
			column+=length;
			offset=*column++;
		}
	}
	advancedPatchTextures[slot].lump=cacheKey;
	advancedPatchTextures[slot].width=width;
	advancedPatchTextures[slot].height=height;
	glGenTextures(1,&advancedPatchTextures[slot].texture);
	glBindTexture(GL_TEXTURE_2D,advancedPatchTextures[slot].texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,
		GL_BGRA,GL_UNSIGNED_BYTE,rgba);
	free(rgba);
	if (outWidth) *outWidth=width;
	if (outHeight) *outHeight=height;
	return advancedPatchTextures[slot].texture;
}

static void AdvancedPatchFace(int lump,bool transparentPatch,
	float x0,float z0,float x1,float z1)
{
	GLuint texture=AdvancedPatchTexture(lump,transparentPatch,NULL,NULL);
	if (!texture) return;
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER,0.05f);
	glBindTexture(GL_TEXTURE_2D,texture);
	glBegin(GL_QUADS);
	glTexCoord2f(0,1); glVertex3f(x0,0,z0);
	glTexCoord2f(1,1); glVertex3f(x1,0,z1);
	glTexCoord2f(1,0); glVertex3f(x1,1,z1);
	glTexCoord2f(0,0); glVertex3f(x0,1,z0);
	glEnd();
	glDisable(GL_ALPHA_TEST);
}

static void AdvancedApplyLight(float level,int lightingEnabled,
	int darkAmbient,int coloredLights)
{
	float base,red,green,blue;

	if (!lightingEnabled)
	{
		glColor3f(1,1,1);
		return;
	}
	base=darkAmbient ? .30f : .72f;
	red=base+level*.085f;
	if (red>1.0f) red=1.0f;
	if (coloredLights && level>0)
	{
		green=base+(red-base)*.72f;
		blue=base+(red-base)*.38f;
	}
	else
		green=blue=red;
	glColor3f(red,green,blue);
}

static void AdvancedSetTileLight(const byte *lightLevels,int mapsize,
	int x,int y,int lightingEnabled,int darkAmbient,int coloredLights)
{
	float level=0;
	if (lightLevels && x>=0 && y>=0 && x<mapsize && y<mapsize)
		level=(float)lightLevels[x*mapsize+y];
	AdvancedApplyLight(level,lightingEnabled,darkAmbient,coloredLights);
}

static void AdvancedSetCornerLight(const byte *lightLevels,int mapsize,
	int x,int y,int lightingEnabled,int darkAmbient,int coloredLights)
{
	int dx,dy,count=0;
	float level=0;
	if (lightLevels)
		for (dx=-1;dx<=0;dx++)
			for (dy=-1;dy<=0;dy++)
				if (x+dx>=0 && y+dy>=0 && x+dx<mapsize && y+dy<mapsize)
				{
					level+=(float)lightLevels[(x+dx)*mapsize+y+dy];
					count++;
				}
	if (count) level/=(float)count;
	AdvancedApplyLight(level,lightingEnabled,darkAmbient,coloredLights);
}

static void AdvancedBillboard(int lump,float x,float y,float z,float yaw)
{
	int width,height;
	GLuint texture=AdvancedPatchTexture(lump,false,&width,&height);
	float radians=yaw*(float)(3.14159265358979323846/180.0);
	float halfWidth=(float)width/128.0f;
	float spriteHeight=(float)height/64.0f;
	float rightX=(float)sin(radians)*halfWidth;
	float rightZ=(float)-cos(radians)*halfWidth;
	if (!texture) return;
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER,0.05f);
	glBindTexture(GL_TEXTURE_2D,texture);
	glColor3f(1,1,1);
	glBegin(GL_QUADS);
	glTexCoord2f(0,1); glVertex3f(x-rightX,z,y-rightZ);
	glTexCoord2f(1,1); glVertex3f(x+rightX,z,y+rightZ);
	glTexCoord2f(1,0); glVertex3f(x+rightX,z+spriteHeight,y+rightZ);
	glTexCoord2f(0,0); glVertex3f(x-rightX,z+spriteHeight,y-rightZ);
	glEnd();
	glDisable(GL_ALPHA_TEST);
}

static void AdvancedScreenPatch(int lump,float centerX,int yOffset,
	int drawableWidth,int drawableHeight)
{
	int width,height;
	GLuint texture=AdvancedPatchTexture(lump,false,&width,&height);
	float aspect=(float)drawableWidth/(float)drawableHeight;
	float h=(float)height/200.0f;
	float w=((float)width/200.0f)/aspect;
	float bottom=1.0f+(float)yOffset/200.0f;
	if (!texture) return;
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER,0.05f);
	glBindTexture(GL_TEXTURE_2D,texture);
	glColor3f(1,1,1);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0); glVertex2f(centerX-w*.5f,bottom-h);
	glTexCoord2f(1,0); glVertex2f(centerX+w*.5f,bottom-h);
	glTexCoord2f(1,1); glVertex2f(centerX+w*.5f,bottom);
	glTexCoord2f(0,1); glVertex2f(centerX-w*.5f,bottom);
	glEnd();
	glDisable(GL_ALPHA_TEST);
}

static GLuint AdvancedPlaneTexture(const byte *source,GLuint *texture,
	const byte **cachedSource)
{
	uint32_t rgba[128*128];
	SDL_Palette *palette;
	int i;

	if (!source || !sdl_surface || !sdl_surface->format->palette) return 0;
	if (*texture && *cachedSource==source) return *texture;
	if (*texture) glDeleteTextures(1,texture);
	palette=sdl_surface->format->palette;
	for (i=0;i<128*128;i++)
	{
		SDL_Color c=palette->colors[source[i]];
		rgba[i]=0xff000000u|((uint32_t)c.r<<16)|((uint32_t)c.g<<8)|c.b;
	}
	glGenTextures(1,texture);
	glBindTexture(GL_TEXTURE_2D,*texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,128,128,0,GL_BGRA,
		GL_UNSIGNED_BYTE,rgba);
	*cachedSource=source;
	return *texture;
}

static GLuint AdvancedSkyTexture(const byte *source,int revision)
{
	uint32_t *rgba;
	SDL_Palette *palette;
	int x,y;

	if (!source || !sdl_surface || !sdl_surface->format->palette) return 0;
	if (advancedSkyTexture && advancedSkyTextureRevision==revision)
		return advancedSkyTexture;
	if (advancedSkyTexture) glDeleteTextures(1,&advancedSkyTexture);
	palette=sdl_surface->format->palette;
	rgba=(uint32_t *)malloc(256u*400u*sizeof(uint32_t));
	if (!rgba) return 0;
	for (y=0;y<400;y++)
		for (x=0;x<256;x++)
	{
		SDL_Color c=palette->colors[source[x*400+y]];
		rgba[y*256+x]=0xff000000u|((uint32_t)c.r<<16)|
			((uint32_t)c.g<<8)|c.b;
	}
	glGenTextures(1,&advancedSkyTexture);
	glBindTexture(GL_TEXTURE_2D,advancedSkyTexture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,400,0,GL_BGRA,
		GL_UNSIGNED_BYTE,rgba);
	free(rgba);
	advancedSkyTextureRevision=revision;
	return advancedSkyTexture;
}

bool AdvancedRendererActive(void)
{
	return advancedGpuRenderer;
}

void AdvancedRendererDrawWorld(const word *tiles,const byte *specialOrientation,
	const word *specialTexture,
	const signed short *specialPatches,
	int mapsize,int worldHeight,float playerx,float playery,float cameraHeight,
	float yaw,float pitch,
	const byte *floorPixels,const byte *ceilingPixels,
	const byte *skyPixels,int skyRevision,int spriteCount,
	const float *spriteX,const float *spriteY,const float *spriteZ,
	const int *spriteLump,int weaponMain,int weaponAlt,
	int weaponXOffset,int weaponYOffset,const byte *lightLevels,
	int lightingEnabled,int darkAmbient,int coloredLights)
{
	int drawableWidth,drawableHeight;
	int minx,maxx,miny,maxy,x,y;
	double aspect,top,right;
	GLuint floorTexture,ceilingTexture,skyTexture;

	if (!advancedGpuRenderer || !advancedGlContext || !tiles)
		return;

	SDL_GL_GetDrawableSize(sdl_window,&drawableWidth,&drawableHeight);
	if (drawableHeight<1) drawableHeight=1;
	aspect=(double)drawableWidth/(double)drawableHeight;
	top=tan(75.0*3.14159265358979323846/360.0)*0.05;
	right=top*aspect;

	glViewport(0,0,drawableWidth,drawableHeight);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glClearColor(0.035f,0.025f,0.055f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-right,right,-top,top,0.05,96.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	/* ROTT's stored pitch has the opposite sign from the model-view rotation. */
	glRotatef(pitch,1,0,0);
	/* ROTT's positive view angle turns its forward vector from +X toward
	   -Y.  World Y is represented as OpenGL -Z here, so the corresponding
	   view transform rotates by +yaw (not -yaw). */
	glRotatef(90.0f+yaw,0,1,0);

	/* Keep the sky centered on the viewer so it behaves as an infinitely
	   distant background while still responding naturally to free look. */
	skyTexture=AdvancedSkyTexture(skyPixels,skyRevision);
	if (skyTexture)
	{
		const float radius=24.0f,lo=-18.0f,hi=18.0f;
		const int slices=64;
		int slice;
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,skyTexture);
		glColor3f(1,1,1);
		/* ROTT's source is a cylindrical panorama, not an equirectangular
		   sphere.  Preserve that projection to avoid pinched cloud rings. */
		glBegin(GL_QUADS);
		for (slice=0;slice<slices;slice++)
		{
			double lon0=6.28318530717958647692*(double)slice/(double)slices;
			double lon1=6.28318530717958647692*(double)(slice+1)/(double)slices;
			float u0=(float)slice/(float)slices,u1=(float)(slice+1)/(float)slices;
			glTexCoord2f(u0,1); glVertex3f((float)cos(lon0)*radius,lo,(float)sin(lon0)*radius);
			glTexCoord2f(u1,1); glVertex3f((float)cos(lon1)*radius,lo,(float)sin(lon1)*radius);
			glTexCoord2f(u1,0); glVertex3f((float)cos(lon1)*radius,hi,(float)sin(lon1)*radius);
			glTexCoord2f(u0,0); glVertex3f((float)cos(lon0)*radius,hi,(float)sin(lon0)*radius);
		}
		glEnd();
		/* Stable dark caps are preferable to stretching a panorama into a
		   singularity when the player looks completely vertical. */
		glDisable(GL_TEXTURE_2D);
		glColor3f(.025f,.02f,.035f);
		glBegin(GL_TRIANGLE_FAN);
		glVertex3f(0,hi,0);
		for (slice=0;slice<=slices;slice++)
		{
			double lon=6.28318530717958647692*(double)slice/(double)slices;
			glVertex3f((float)cos(lon)*radius,hi,(float)sin(lon)*radius);
		}
		glEnd();
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_TEXTURE_2D);
	}
	glTranslatef(-playerx,-cameraHeight,playery);

	minx=(int)playerx-32; if(minx<1) minx=1;
	maxx=(int)playerx+32; if(maxx>mapsize-2) maxx=mapsize-2;
	miny=(int)playery-32; if(miny<1) miny=1;
	maxy=(int)playery+32; if(maxy>mapsize-2) maxy=mapsize-2;

	/* Level-wide 128x128 flats repeat across the original tile grid. */
	floorTexture=AdvancedPlaneTexture(floorPixels,&advancedFloorTexture,
		&advancedFloorSource);
	ceilingTexture=AdvancedPlaneTexture(ceilingPixels,&advancedCeilingTexture,
		&advancedCeilingSource);
	if (floorTexture)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,floorTexture);
		glColor3f(1,1,1);
		glBegin(GL_QUADS);
		for (x=minx;x<=maxx;x++)
			for (y=miny;y<=maxy;y++)
				if (tiles[x*mapsize+y]==0 || (tiles[x*mapsize+y]&0x8000))
				{
					float x0=(float)x,x1=x0+1.0f;
					float z0=(float)-y,z1=z0-1.0f;
					AdvancedSetCornerLight(lightLevels,mapsize,x,y,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x0*.5f,y*.5f); glVertex3f(x0,0,z0);
					AdvancedSetCornerLight(lightLevels,mapsize,x+1,y,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x1*.5f,y*.5f); glVertex3f(x1,0,z0);
					AdvancedSetCornerLight(lightLevels,mapsize,x+1,y+1,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x1*.5f,(y+1)*.5f); glVertex3f(x1,0,z1);
					AdvancedSetCornerLight(lightLevels,mapsize,x,y+1,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x0*.5f,(y+1)*.5f); glVertex3f(x0,0,z1);
				}
		glEnd();
	}
	if (ceilingTexture)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,ceilingTexture);
		glColor3f(1,1,1);
		glBegin(GL_QUADS);
		for (x=minx;x<=maxx;x++)
			for (y=miny;y<=maxy;y++)
				if (tiles[x*mapsize+y]==0 || (tiles[x*mapsize+y]&0x8000))
				{
					float x0=(float)x,x1=x0+1.0f;
					float z0=(float)-y,z1=z0-1.0f;
					AdvancedSetCornerLight(lightLevels,mapsize,x,y+1,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x0*.5f,(y+1)*.5f); glVertex3f(x0,(float)worldHeight,z1);
					AdvancedSetCornerLight(lightLevels,mapsize,x+1,y+1,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x1*.5f,(y+1)*.5f); glVertex3f(x1,(float)worldHeight,z1);
					AdvancedSetCornerLight(lightLevels,mapsize,x+1,y,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x1*.5f,y*.5f); glVertex3f(x1,(float)worldHeight,z0);
					AdvancedSetCornerLight(lightLevels,mapsize,x,y,
						lightingEnabled,darkAmbient,coloredLights);
					glTexCoord2f(x0*.5f,y*.5f); glVertex3f(x0,(float)worldHeight,z0);
				}
		glEnd();
	}
	glDisable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);
	for (x=minx;x<=maxx;x++)
		for (y=miny;y<=maxy;y++)
		{
			word tile=tiles[x*mapsize+y];
			float x0=(float)x,x1=x0+1.0f;
			float z0=(float)-y,z1=z0-1.0f;

			if ((tile&0xc000)==0xc000 && specialOrientation &&
			         specialOrientation[x*mapsize+y] &&
			         (!specialPatches ||
			          (specialPatches[(x*mapsize+y)*3]<0 &&
			           specialPatches[(x*mapsize+y)*3+1]<0 &&
			           specialPatches[(x*mapsize+y)*3+2]<0)))
			{
				/* Door/masked-wall geometry uses the authoritative orientation
				   supplied by the game objects rather than propagated tile flags. */
				glColor3f(0.80f,0.25f,0.15f);
				if (specialOrientation[x*mapsize+y]==1)
				{
					glVertex3f(x0+0.5f,0,z0); glVertex3f(x0+0.5f,0,z1);
					glVertex3f(x0+0.5f,(float)worldHeight,z1); glVertex3f(x0+0.5f,(float)worldHeight,z0);
				}
				else
				{
					glVertex3f(x0,0,z0-0.5f); glVertex3f(x1,0,z0-0.5f);
					glVertex3f(x1,(float)worldHeight,z0-0.5f); glVertex3f(x0,(float)worldHeight,z0-0.5f);
				}
			}
		}
	glEnd();
	/* Doors use the original masked patch artwork.  Masked gates remain the
	   red diagnostic planes until their three-layer patches are composed. */
	if (specialOrientation && specialTexture)
		for (x=minx;x<=maxx;x++)
			for (y=miny;y<=maxy;y++)
			{
				int cell=x*mapsize+y;
				if ((tiles[cell]&0x8000) && !(tiles[cell]&0x4000) &&
					specialOrientation[cell] && specialTexture[cell])
				{
					float x0=(float)x,x1=x0+1.0f;
					float z0=(float)-y,z1=z0-1.0f;
					AdvancedSetTileLight(lightLevels,mapsize,x,y,
						lightingEnabled,darkAmbient,coloredLights);
					if (specialOrientation[cell]==1)
						AdvancedPatchFace(specialTexture[cell],false,x0+.5f,z0,x0+.5f,z1);
					else
						AdvancedPatchFace(specialTexture[cell],false,x0,z0-.5f,x1,z0-.5f);
				}
			}
	/* Masked gates use a transparent lower patch in the original renderer.
	   At the common one-level wall height, that is the complete visible gate. */
	if (specialOrientation && specialPatches)
		for (x=minx;x<=maxx;x++)
			for (y=miny;y<=maxy;y++)
			{
				int cell=x*mapsize+y;
				int lump=specialPatches[cell*3];
				bool transparentPatch=true;
				if (lump<0) { lump=specialPatches[cell*3+1]; transparentPatch=false; }
				if (lump<0) { lump=specialPatches[cell*3+2]; transparentPatch=false; }
				if ((tiles[cell]&0xc000)==0xc000 && specialOrientation[cell] && lump>=0)
				{
					float x0=(float)x,x1=x0+1.0f,z0=(float)-y,z1=z0-1.0f;
					AdvancedSetTileLight(lightLevels,mapsize,x,y,
						lightingEnabled,darkAmbient,coloredLights);
					if (specialOrientation[cell]==1)
						AdvancedPatchFace(lump,transparentPatch,x0+.5f,z0,x0+.5f,z1);
					else
						AdvancedPatchFace(lump,transparentPatch,x0,z0-.5f,x1,z0-.5f);
				}
			}
	/* Sprites remain classic camera-facing artwork, now placed in the true
	   perspective/depth buffer instead of the old column scaler. */
	if (spriteX && spriteY && spriteZ && spriteLump)
		for (x=0;x<spriteCount;x++)
			AdvancedBillboard(spriteLump[x],spriteX[x],-spriteY[x],
				spriteZ[x],yaw);
	for (x=minx;x<=maxx;x++)
		for (y=miny;y<=maxy;y++)
			if (tiles[x*mapsize+y]==0)
			{
				float x0=(float)x,x1=x0+1.0f;
				float z0=(float)-y,z1=z0-1.0f;
				word wall=tiles[(x-1)*mapsize+y];
				AdvancedSetTileLight(lightLevels,mapsize,x,y,
					lightingEnabled,darkAmbient,coloredLights);
				if (wall && !(wall&0x8000))
					AdvancedWallFace(wall,x0,0,z1,x0,0,z0,x0,(float)worldHeight,z0,x0,(float)worldHeight,z1);
				wall=tiles[(x+1)*mapsize+y];
				if (wall && !(wall&0x8000))
					AdvancedWallFace(wall,x1,0,z0,x1,0,z1,x1,(float)worldHeight,z1,x1,(float)worldHeight,z0);
				wall=tiles[x*mapsize+y-1];
				if (wall && !(wall&0x8000))
					AdvancedWallFace(wall,x0,0,z0,x1,0,z0,x1,(float)worldHeight,z0,x0,(float)worldHeight,z0);
				wall=tiles[x*mapsize+y+1];
				if (wall && !(wall&0x8000))
					AdvancedWallFace(wall,x1,0,z1,x0,0,z1,x0,(float)worldHeight,z1,x1,(float)worldHeight,z1);
			}
	/* First-person artwork is screen-space UI and must be the final world
	   layer so depth-tested walls and sprites can never overwrite it. */
	if (weaponMain>=0)
	{
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0,1,1,0,-1,1);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		AdvancedScreenPatch(weaponMain,.5f+(float)weaponXOffset/320.0f,
			weaponYOffset,drawableWidth,drawableHeight);
		if (weaponAlt>=0)
			AdvancedScreenPatch(weaponAlt,.5f-80.0f/320.0f,
				weaponYOffset,drawableWidth,drawableHeight);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glEnable(GL_DEPTH_TEST);
	}
	/* Reuse the classic status renderer for health/ammo, but composite only
	   its transparent bottom strip over the native-resolution 3D view. */
	glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,1,0,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	AdvancedHudOverlay(drawableWidth,drawableHeight);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	advancedWorldReady=true;
}

void BuildHintTexture()
{
	lastInteraction = SDL_GetTicks();
	SDL_RWops *file = SDL_RWFromFile("buttons.bmp", "rb");
	if (file)
	{
		SDL_Surface *image = SDL_LoadBMP_RW(file, SDL_TRUE);
		if (!image)
		{
			Error("Bitmap error: %s\n", SDL_GetError());
		}
		SDL_SetColorKey(image, SDL_TRUE, SDL_MapRGBA(image->format, 255, 255, 255, 0));
		sdl_hint_texture = SDL_CreateTextureFromSurface(sdl_renderer, image);
		if (!sdl_hint_texture)
		{
			Error("Texture error: %s\n", SDL_GetError());
		}
		SDL_FreeSurface(image);
	}
}

void GraphicsMode ( void )
{
    Uint32 flags = 0;
    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
	advancedGpuRenderer=(CheckParm("advancedrenderer")!=0);

	if (SDL_InitSubSystem (SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	{
	    Error ("Could not initialize SDL: %s\n", SDL_GetError());
	}

    #if defined(PLATFORM_MACOSX)
        // FIXME: remove this.  --ryan.
        flags = SDL_WINDOW_FULLSCREEN;
        SDL_WM_GrabInput(SDL_GRAB_ON);
    #endif
    SDL_ShowCursor (0);
    if (sdl_fullscreen)
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (advancedGpuRenderer)
		flags |= SDL_WINDOW_OPENGL;
    sdl_window = SDL_CreateWindow("Rise of the Triad",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT,
                              flags);
	if (sdl_window == NULL)
	{
		Error ("Could not open window: %s\n", SDL_GetError());
	}
	if (advancedGpuRenderer)
	{
		advancedGlContext=SDL_GL_CreateContext(sdl_window);
		if (!advancedGlContext)
			Error("Could not create Advanced OpenGL context: %s\n",SDL_GetError());
		SDL_GL_SetSwapInterval(1);
		glGenTextures(1,&advancedGlTexture);
		memset(advancedWallTextures,0,sizeof(advancedWallTextures));
		memset(advancedPatchTextures,0,sizeof(advancedPatchTextures));
		advancedFloorTexture=advancedCeilingTexture=advancedSkyTexture=0;
		advancedFloorSource=advancedCeilingSource=NULL;
		advancedSkyTextureRevision=-1;
		glBindTexture(GL_TEXTURE_2D,advancedGlTexture);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,iGLOBAL_SCREENWIDTH,
			iGLOBAL_SCREENHEIGHT,0,GL_BGRA,GL_UNSIGNED_BYTE,NULL);
		advancedGlPixels=(uint32_t *)malloc((size_t)iGLOBAL_SCREENWIDTH*
			iGLOBAL_SCREENHEIGHT*sizeof(uint32_t));
		if (!advancedGlPixels)
			Error("Could not allocate Advanced renderer upload buffer\n");
	}
	else
	{
		sdl_renderer = SDL_CreateRenderer(sdl_window, -1, rendererFlags);
		/* Some remote-desktop and compatibility drivers do not expose an
		   accelerated renderer.  Preserve a working fallback in that case. */
		if (sdl_renderer == NULL)
			sdl_renderer = SDL_CreateRenderer(sdl_window, -1, 0);
		if (sdl_renderer == NULL)
			Error ("Could not create renderer: %s\n", SDL_GetError());
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		SDL_RenderSetLogicalSize(sdl_renderer,iGLOBAL_SCREENWIDTH,iGLOBAL_SCREENHEIGHT);
		sdl_texture = SDL_CreateTexture(sdl_renderer,SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,iGLOBAL_SCREENWIDTH,iGLOBAL_SCREENHEIGHT);
		if (sdl_texture == NULL)
			Error ("Texture error: %s\n", SDL_GetError());
	}
	sdl_surface = SDL_CreateRGBSurface(0, iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, 8, 0, 0, 0, 0);
	if (sdl_surface == NULL)
	{
		Error ("Could not create surface: %s\n", SDL_GetError());
	}
	if (!advancedGpuRenderer)
		BuildHintTexture();
}

static void blitScreen32(uint32_t *dst, int dstpitch)
{
    uint8_t *src = sdl_surface->pixels;
    SDL_Palette* palette = sdl_surface->format->palette;
    uint32_t colors[256];
    int x, y, i;

    for (i=0;i<256;i++)
       {
       SDL_Color color=palette->colors[i];
       colors[i]=(color.a<<24)|(color.r<<16)|(color.g<<8)|color.b;
       }

    for (y=0;y<iGLOBAL_SCREENHEIGHT;y++)
       {
       uint32_t *row=(uint32_t *)((byte *)dst+y*dstpitch);
       for (x=0;x<iGLOBAL_SCREENWIDTH;x++)
          row[x]=colors[*src++];
       }
}

static void UpdateScreenTexture(void)
{
    void *pixels;
    int pitch;

    if (SDL_LockTexture(sdl_texture,NULL,&pixels,&pitch)<0)
       Error("Could not lock screen texture: %s\n",SDL_GetError());
    blitScreen32((uint32_t *)pixels,pitch);
    SDL_UnlockTexture(sdl_texture);
}

static void PresentAdvancedScreen(void)
{
	int width,height;
	if (advancedWorldReady)
	{
		SDL_GL_SwapWindow(sdl_window);
		advancedWorldReady=false;
		return;
	}

	blitScreen32(advancedGlPixels,iGLOBAL_SCREENWIDTH*(int)sizeof(uint32_t));
	glBindTexture(GL_TEXTURE_2D,advancedGlTexture);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,iGLOBAL_SCREENWIDTH,
		iGLOBAL_SCREENHEIGHT,GL_BGRA,GL_UNSIGNED_BYTE,advancedGlPixels);
	SDL_GL_GetDrawableSize(sdl_window,&width,&height);
	glViewport(0,0,width,height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,1,1,0,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_TEXTURE_2D);
	glColor4f(1,1,1,1);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0); glVertex2f(0,0);
	glTexCoord2f(1,0); glVertex2f(1,0);
	glTexCoord2f(1,1); glVertex2f(1,1);
	glTexCoord2f(0,1); glVertex2f(0,1);
	glEnd();
	SDL_GL_SwapWindow(sdl_window);
}

/*
====================
=
= SetTextMode
=
====================
*/
void SetTextMode ( void )
{
	if (SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO) {
		if (advancedGlContext != NULL) {
			int i;
			for (i=0;i<1024;i++)
				if (advancedWallTextures[i])
					glDeleteTextures(1,&advancedWallTextures[i]);
			for (i=0;i<512;i++)
				if (advancedPatchTextures[i].texture)
					glDeleteTextures(1,&advancedPatchTextures[i].texture);
			if (advancedFloorTexture) glDeleteTextures(1,&advancedFloorTexture);
			if (advancedCeilingTexture) glDeleteTextures(1,&advancedCeilingTexture);
			if (advancedSkyTexture) glDeleteTextures(1,&advancedSkyTexture);
			glDeleteTextures(1,&advancedGlTexture);
			free(advancedGlPixels);
			advancedGlPixels=NULL;
			SDL_GL_DeleteContext(advancedGlContext);
			advancedGlContext=NULL;
		}
		if (sdl_renderer != NULL) {
			SDL_DestroyRenderer(sdl_renderer);
			sdl_renderer = NULL;
		}
		if (sdl_window != NULL) {
			SDL_DestroyWindow(sdl_window);
			sdl_window = NULL;
		}
		
		SDL_QuitSubSystem (SDL_INIT_VIDEO);
	}
}

/*
====================
=
= TurnOffTextCursor
=
====================
*/
void TurnOffTextCursor ( void )
{
}

/*
====================
=
= WaitVBL
=
====================
*/
void WaitVBL( void )
{
	SDL_Delay (16667/1000);
}

/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void VL_SetVGAPlaneMode ( void )
{
   int i,offset;

    GraphicsMode();

//
// set up lookup tables
//
//bna--   linewidth = 320;
   linewidth = iGLOBAL_SCREENWIDTH;

   offset = 0;

   for (i=0;i<iGLOBAL_SCREENHEIGHT;i++)
      {
      ylookup[i]=offset;
      offset += linewidth;
      }

//    screensize=MAXSCREENHEIGHT*MAXSCREENWIDTH;
    screensize=iGLOBAL_SCREENHEIGHT*iGLOBAL_SCREENWIDTH;



    page1start=sdl_surface->pixels;
    page2start=sdl_surface->pixels;
    page3start=sdl_surface->pixels;
    displayofs = page1start;
    bufferofs = page2start;

	iG_X_center = iGLOBAL_SCREENWIDTH / 2;
	iG_Y_center = (iGLOBAL_SCREENHEIGHT / 2)+10 ;//+10 = move aim down a bit

	iG_buf_center = bufferofs + (screensize/2);//(iG_Y_center*iGLOBAL_SCREENWIDTH);//+iG_X_center;

	bufofsTopLimit =  bufferofs + screensize - iGLOBAL_SCREENWIDTH;
	bufofsBottomLimit = bufferofs + iGLOBAL_SCREENWIDTH;

    // start stretched
    EnableScreenStretch();
    XFlipPage ();
}

/*
=======================
=
= VL_CopyPlanarPage
=
=======================
*/
void VL_CopyPlanarPage ( byte * src, byte * dest )
{
#ifdef DOS
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      memcpy(dest,src,screensize);
      }
#else
      memcpy(dest,src,screensize);
#endif
}

/*
=======================
=
= VL_CopyPlanarPageToMemory
=
=======================
*/
void VL_CopyPlanarPageToMemory ( byte * src, byte * dest )
{
#ifdef DOS
   byte * ptr;
   int plane,a,b;

   for (plane=0;plane<4;plane++)
      {
      ptr=dest+plane;
      VGAREADMAP(plane);
      for (a=0;a<200;a++)
         for (b=0;b<80;b++,ptr+=4)
            *(ptr)=*(src+(a*linewidth)+b);
      }
#else
      memcpy(dest,src,screensize);
#endif
}

/*
=======================
=
= VL_CopyBufferToAll
=
=======================
*/
void VL_CopyBufferToAll ( byte *buffer )
{
#ifdef DOS
   int plane;

   for (plane=0;plane<4;plane++)
      {
      VGAREADMAP(plane);
      VGAWRITEMAP(plane);
      if (page1start!=buffer)
         memcpy((byte *)page1start,(byte *)buffer,screensize);
      if (page2start!=buffer)
         memcpy((byte *)page2start,(byte *)buffer,screensize);
      if (page3start!=buffer)
         memcpy((byte *)page3start,(byte *)buffer,screensize);
      }
#endif
}

/*
=======================
=
= VL_CopyDisplayToHidden
=
=======================
*/
void VL_CopyDisplayToHidden ( void )
{
   VL_CopyBufferToAll ( displayofs );
}

/*
=================
=
= VL_ClearBuffer
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearBuffer (byte *buf, byte color)
{
#ifdef DOS
  VGAMAPMASK(15);
  memset((byte *)buf,color,screensize);
#else
  memset((byte *)buf,color,screensize);
#endif
}

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (byte color)
{
#ifdef DOS
  VGAMAPMASK(15);
  memset((byte *)(0xa000<<4),color,0x10000);
#else
//  SDL_Color renderColor = sdl_surface->format->palette->colors[color];
//  if (!SDL_SetRenderDrawColor(sdl_renderer, renderColor.r, renderColor.g, renderColor.b, renderColor.a))
//  {
//	  Error(SDL_GetError());
//  }
//  if (!SDL_RenderClear(sdl_renderer))
//  {
//	  Error(SDL_GetError());
//  }
  memset (sdl_surface->pixels, color, iGLOBAL_SCREENWIDTH*iGLOBAL_SCREENHEIGHT);
#endif
}

/*
=================
=
= VL_DePlaneVGA
=
=================
*/

void VL_DePlaneVGA (void)
{
}

static void RenderCopyHintTexture()
{
	if (sdl_hint_texture)
	{
		unsigned int deltaInteraction = SDL_GetTicks() - lastInteraction;
		const unsigned int minDelay = 10000;
		if (deltaInteraction < minDelay + 4600)
		{
			if (deltaInteraction > minDelay + 4000)
			{
				unsigned int shade = (minDelay + 4600 - deltaInteraction) / 3;
				SDL_SetTextureAlphaMod(sdl_hint_texture, shade < 1 ? 0 : shade);
				SDL_RenderCopy(sdl_renderer, sdl_hint_texture, NULL, NULL);
			}
			else if (deltaInteraction > minDelay)
			{
				unsigned int shade = (deltaInteraction - minDelay) / 3;
				SDL_SetTextureAlphaMod(sdl_hint_texture, shade > 200 ? 200 : shade);
				SDL_RenderCopy(sdl_renderer, sdl_hint_texture, NULL, NULL);
			}
		}
	}
}

/* C version of rt_vh_a.asm */

void VH_UpdateScreen (void)
{ 	

	if (StretchScreen){//bna++
		StretchMemPicture ();
	}else{
		DrawCenterAim ();
	}
//	SDL_UpdateRect (SDL_GetVideoSurface (), 0, 0, 0, 0);
	if (advancedGpuRenderer)
		PresentAdvancedScreen();
	else
	{
		UpdateScreenTexture();
		SDL_RenderClear(sdl_renderer);
		SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
		RenderCopyHintTexture();
		SDL_RenderPresent(sdl_renderer);
	}
}


/*
=================
=
= XFlipPage
=
=================
*/

void XFlipPage ( void )
{
#ifdef DOS
   displayofs=bufferofs;

//   _disable();

   outp(CRTC_INDEX,CRTC_STARTHIGH);
   outp(CRTC_DATA,((displayofs&0x0000ffff)>>8));

//   _enable();

   bufferofs += screensize;
   if (bufferofs > page3start)
      bufferofs = page1start;
#else
 	if (StretchScreen){//bna++
		StretchMemPicture ();
	}else{
		DrawCenterAim ();
	}
//   SDL_UpdateRect (sdl_surface, 0, 0, 0, 0);
	if (advancedGpuRenderer)
		PresentAdvancedScreen();
	else
	{
		UpdateScreenTexture();
		SDL_RenderClear(sdl_renderer);
		SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
		RenderCopyHintTexture();
		SDL_RenderPresent(sdl_renderer);
	}
	{
		static Uint64 fpsStart = 0;
		static unsigned int fpsFrames = 0;
		Uint64 now = SDL_GetPerformanceCounter();
		Uint64 frequency = SDL_GetPerformanceFrequency();

		if (fpsStart == 0)
			fpsStart = now;
		fpsFrames++;
		if ((now - fpsStart) >= frequency)
		{
			char title[80];
			double elapsed = (double)(now - fpsStart) / (double)frequency;
			double fps = fpsFrames / elapsed;
			SDL_snprintf(title, sizeof(title), "Rise of the Triad - %.0f FPS", fps);
			SDL_SetWindowTitle(sdl_window, title);
			fpsFrames = 0;
			fpsStart = now;
		}
	}
 
#endif
}

#endif


void EnableScreenStretch(void)
{
   int i,offset;

   if (iGLOBAL_SCREENWIDTH <= 320 || StretchScreen) return;

   if (unstretch_sdl_surface == NULL)
   {
      /* should really be just 320x200, but there is code all over the
         places which crashes then */
      unstretch_sdl_surface = SDL_CreateRGBSurface(SDL_SWSURFACE,
         iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, 8, 0, 0, 0, 0);
   }

   displayofs = (byte*)unstretch_sdl_surface->pixels +
	(displayofs - (byte *)sdl_surface->pixels);
   bufferofs  = unstretch_sdl_surface->pixels;
   page1start = unstretch_sdl_surface->pixels;
   page2start = unstretch_sdl_surface->pixels;
   page3start = unstretch_sdl_surface->pixels;
   StretchScreen = 1;
}

void DisableScreenStretch(void)
{
   if (iGLOBAL_SCREENWIDTH <= 320 || !StretchScreen) return;

   displayofs = (byte*)sdl_surface->pixels +
	(displayofs - (byte *)unstretch_sdl_surface->pixels);
   bufferofs  = sdl_surface->pixels;
   page1start = sdl_surface->pixels;
   page2start = sdl_surface->pixels;
   page3start = sdl_surface->pixels;
   StretchScreen = 0;
}


// bna section -------------------------------------------
static void StretchMemPicture ()
{
  SDL_Rect src;
  SDL_Rect dest;

  src.x = 0;
  src.y = 0;
  src.w = 320;
  src.h = 200;

  dest.x = 0;
  dest.y = 0;
  dest.w = iGLOBAL_SCREENWIDTH;
  dest.h = iGLOBAL_SCREENHEIGHT;
  SDL_SoftStretch(unstretch_sdl_surface, &src, sdl_surface, &dest);
}

// bna function added start
extern	bool ingame;
int		iG_playerTilt;

void DrawCenterAim ()
{
	int x;

	int percenthealth = (locplayerstate->health * 10) / MaxHitpointsForCharacter(locplayerstate);
	int color = percenthealth < 3 ? egacolor[RED] : percenthealth < 4 ? egacolor[YELLOW] : egacolor[GREEN];

	if (iG_aimCross && !GamePaused){
		if (( ingame == true )&&(iGLOBAL_SCREENWIDTH>320)){
			  if ((iG_playerTilt <0 )||(iG_playerTilt >iGLOBAL_SCREENHEIGHT/2)){
					iG_playerTilt = -(2048 - iG_playerTilt);
			  }
			  if (iGLOBAL_SCREENWIDTH == 640){ x = iG_playerTilt;iG_playerTilt=x/2; }
			  iG_buf_center = bufferofs + ((iG_Y_center-iG_playerTilt)*iGLOBAL_SCREENWIDTH);//+iG_X_center;

			  for (x=iG_X_center-10;x<=iG_X_center-4;x++){
				  if ((iG_buf_center+x < bufofsTopLimit)&&(iG_buf_center+x > bufofsBottomLimit)){
					 *(iG_buf_center+x) = color;
				  }
			  }
			  for (x=iG_X_center+4;x<=iG_X_center+10;x++){
				  if ((iG_buf_center+x < bufofsTopLimit)&&(iG_buf_center+x > bufofsBottomLimit)){
					 *(iG_buf_center+x) = color;
				  }
			  }
			  for (x=10;x>=4;x--){
				  if (((iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) < bufofsTopLimit)&&((iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) > bufofsBottomLimit)){
					 *(iG_buf_center-(x*iGLOBAL_SCREENWIDTH)+iG_X_center) = color;
				  }
			  }
			  for (x=4;x<=10;x++){
				  if (((iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) < bufofsTopLimit)&&((iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) > bufofsBottomLimit)){
					 *(iG_buf_center+(x*iGLOBAL_SCREENWIDTH)+iG_X_center) = color;
				  }
			  }
		}
	}
}
// bna function added end




// bna section -------------------------------------------



