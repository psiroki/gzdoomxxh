// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2017 Magnus Norddahl
// Copyright(C) 2018 Rachael Alexanderson
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#include <math.h>
#include "c_dispatch.h"
#include "c_cvars.h"
#include "v_video.h"

#define NUMSCALEMODES 6

EXTERN_CVAR(Int, vid_aspect)
CVAR(Int, vid_scale_customwidth, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vid_scale_customheight, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, vid_scale_customlinear, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, vid_scale_customstretched, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{
	struct v_ScaleTable
	{
		bool isValid;
		bool isLinear;
		uint32_t(*GetScaledWidth)(uint32_t Width);
		uint32_t(*GetScaledHeight)(uint32_t Height);
		bool isScaled43;
		bool isCustom;
	};
	v_ScaleTable vScaleTable[NUMSCALEMODES] =
	{
		//	isValid,	isLinear,	GetScaledWidth(),									            	GetScaledHeight(),										        	isScaled43, isCustom
		{ true,			false,		[](uint32_t Width)->uint32_t { return Width; },		        	[](uint32_t Height)->uint32_t { return Height; },	        		false,  	false   },	// 0  - Native
		{ true,			true,		[](uint32_t Width)->uint32_t { return Width; },			        [](uint32_t Height)->uint32_t { return Height; },	        		false,  	false   },	// 1  - Native (Linear)
		{ true,			false,		[](uint32_t Width)->uint32_t { return 320; },		            	[](uint32_t Height)->uint32_t { return 200; },			        	true,   	false   },	// 2  - 320x200
		{ true,			false,		[](uint32_t Width)->uint32_t { return 640; },		            	[](uint32_t Height)->uint32_t { return 400; },				        true,   	false   },	// 3  - 640x400
		{ true,			true,		[](uint32_t Width)->uint32_t { return 1280; },		            	[](uint32_t Height)->uint32_t { return 800; },	        			true,   	false   },	// 4  - 1280x800		
		{ true,			true,		[](uint32_t Width)->uint32_t { return vid_scale_customwidth; },	[](uint32_t Height)->uint32_t { return vid_scale_customheight; },	true,   	true    },	// 5  - Custom
	};
	bool isOutOfBounds(int x)
	{
        if (vScaleTable[x].isCustom)
            return ((vid_scale_customwidth < 80) || (vid_scale_customheight < 50));
		return (x < 0 || x >= NUMSCALEMODES || vScaleTable[x].isValid == false);
	}
}

CUSTOM_CVAR(Float, vid_scalefactor, 1.0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0.05 || self > 2.0)
		self = 1.0;
}

CUSTOM_CVAR(Int, vid_scalemode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (isOutOfBounds(self))
		self = 0;
}

CVAR(Bool, vid_cropaspect, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

bool ViewportLinearScale()
{
	if (isOutOfBounds(vid_scalemode))
		vid_scalemode = 0;
	// hack - use custom scaling if in "custom" mode
	if (vScaleTable[vid_scalemode].isCustom)
		return vid_scale_customlinear;
	// vid_scalefactor > 1 == forced linear scale
	return (vid_scalefactor > 1.0) ? true : vScaleTable[vid_scalemode].isLinear;
}

int ViewportScaledWidth(int width, int height)
{
	if (isOutOfBounds(vid_scalemode))
		vid_scalemode = 0;
	if (vid_cropaspect && height > 0)
		width = ((float)width/height > ActiveRatio(width, height)) ? (int)(height * ActiveRatio(width, height)) : width;
	return vScaleTable[vid_scalemode].GetScaledWidth((int)((float)width * vid_scalefactor));
}

int ViewportScaledHeight(int width, int height)
{
	if (isOutOfBounds(vid_scalemode))
		vid_scalemode = 0;
	if (vid_cropaspect && height > 0)
		height = ((float)width/height < ActiveRatio(width, height)) ? (int)(width / ActiveRatio(width, height)) : height;
	return vScaleTable[vid_scalemode].GetScaledHeight((int)((float)height * vid_scalefactor));
}

bool ViewportIsScaled43()
{
	if (isOutOfBounds(vid_scalemode))
		vid_scalemode = 0;
	// hack - use custom scaling if in "custom" mode
	if (vScaleTable[vid_scalemode].isCustom)
		return vid_scale_customstretched;
	return vScaleTable[vid_scalemode].isScaled43;
}

void R_ShowCurrentScaling()
{
	int x1 = screen->GetClientWidth(), y1 = screen->GetClientHeight(), x2 = ViewportScaledWidth(x1, y1), y2 = ViewportScaledHeight(x1, y1);
	Printf("Current vid_scalefactor: %f\n", (float)(vid_scalefactor));
	Printf("Real resolution: %i x %i\nEmulated resolution: %i x %i\n", x1, y1, x2, y2);
}

bool R_CalcsShouldBeBlocked()
{
	if (vid_scalemode < 0 || vid_scalemode > 1)
	{
		Printf("vid_scalemode should be 0 or 1 before using this command.\n");
		return true;
	}
	if (vid_aspect != 0 && vid_cropaspect == true)
	{   // just warn ... I'm not going to fix this, it's a pretty niche condition anyway.
        Printf("Warning: Using this command while vid_aspect is not 0 will yield results based on FULL screen geometry, NOT cropped!.\n");
		return false;
	}
	return false;	
}

CCMD (vid_showcurrentscaling)
{
	R_ShowCurrentScaling();
}

CCMD (vid_scaletowidth)
{
	if (R_CalcsShouldBeBlocked())
		return;	

	if (argv.argc() > 1)
		vid_scalefactor = (float)((double)atof(argv[1]) / screen->GetClientWidth());

	R_ShowCurrentScaling();
}

CCMD (vid_scaletoheight)
{
	if (R_CalcsShouldBeBlocked())
		return;	

	if (argv.argc() > 1)
		vid_scalefactor = (float)((double)atof(argv[1]) / screen->GetClientHeight());

	R_ShowCurrentScaling();
}

inline bool atob(char* I)
{
    if (stricmp (I, "true") == 0 || stricmp (I, "1") == 0)
        return true;
    return false;
}

CCMD (vid_setscale)
{
    if (argv.argc() > 2)
    {
        vid_scale_customwidth = atoi(argv[1]);
        vid_scale_customheight = atoi(argv[2]);
        if (argv.argc() > 3)
        {
            vid_scale_customlinear = atob(argv[3]);
            if (argv.argc() > 4)
            {
                vid_scale_customstretched = atob(argv[4]);
            }
        }
        vid_scalemode = 5;
    }
    else
    {
        Printf("Usage: vid_setscale <x> <y> [bool linear] [bool long-pixel-shape]\nThis command will create a custom viewport scaling mode.\n");
    }
}
