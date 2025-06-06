/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Wolfenstein: Enemy Territory GPL Source Code (Wolf ET Source Code).  

Wolf ET Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Wolf ET Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wolf ET Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Wolf: ET Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Wolf ET Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
// cl.input.c  -- builds an intended movement command to send to the server

#include "client.h"

static unsigned frame_msec;
static int old_com_frameTime;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get qued
at the same time.

===============================================================================
*/

typedef struct {
	int			down[2];		// key nums holding it down
	unsigned	downtime;		// msec timestamp
	unsigned	msec;			// msec down this frame if both a down and up happened
	qboolean	active;			// current state
	qboolean	wasPressed;		// set when down, not cleared when up
} kbutton_t;

static kbutton_t kb[NUM_BUTTONS];

// Arnout: doubleTap button mapping
static const kbuttons_t dtmapping[] = {
	-1,                 // DT_NONE
	KB_MOVELEFT,        // DT_MOVELEFT
	KB_MOVERIGHT,       // DT_MOVERIGHT
	KB_FORWARD,         // DT_FORWARD
	KB_BACK,            // DT_BACK
	KB_WBUTTONS4,       // DT_LEANLEFT
	KB_WBUTTONS5,       // DT_LEANRIGHT
	KB_UP               // DT_UP
};


static cvar_t *cl_nodelta;

static cvar_t *cl_showSend;

static cvar_t *cl_sensitivity;
static cvar_t *cl_mouseAccel;
static cvar_t *cl_mouseAccelOffset;
static cvar_t *cl_mouseAccelStyle;
static cvar_t *cl_showMouseRate;

static cvar_t *cl_run;
static cvar_t *cl_freelook;

static cvar_t *cl_yawspeed;
static cvar_t *cl_pitchspeed;
static cvar_t *cl_anglespeedkey;

static cvar_t *cl_maxpackets;
static cvar_t *cl_packetdup;

static cvar_t *m_pitch;
static cvar_t *m_yaw;
static cvar_t *m_forward;
static cvar_t *m_side;
static cvar_t *m_filter;

static cvar_t *cl_recoilPitch;
cvar_t *cl_bypassMouseInput;       // NERVE - SMF
static cvar_t *cl_doubletapdelay;

static void IN_MLookDown( void ) {
	kb[KB_MLOOK].active = qtrue;
}


static void IN_MLookUp( void ) {
	kb[KB_MLOOK].active = qfalse;
}


static void IN_KeyDown( kbutton_t *b ) {
	const char *c;
	int	k;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		k = -1;		// typed manually at the console for continuous down
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}

	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}

	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	b->downtime = atoi(c);

	b->active = qtrue;
	b->wasPressed = qtrue;
}


static void IN_KeyUp( kbutton_t *b ) {
	unsigned uptime;
	const char *c;
	int		k;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->active = qfalse;
		return;
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without coresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = qfalse;

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	uptime = atoi(c);
	if ( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += frame_msec / 2;
	}

	b->active = qfalse;
}


/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
static float CL_KeyState( kbutton_t *key ) {
	float		val;
	int			msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = com_frameTime;
		} else {
			msec += com_frameTime - key->downtime;
		}
		key->downtime = com_frameTime;
	}

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = (float)msec / frame_msec;
	if ( val < 0 ) {
		val = 0;
	}
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}


static void IN_UpDown( void ) {IN_KeyDown( &kb[KB_UP] );}
static void IN_UpUp( void ) {IN_KeyUp( &kb[KB_UP] );}
static void IN_DownDown( void ) {IN_KeyDown( &kb[KB_DOWN] );}
static void IN_DownUp( void ) {IN_KeyUp( &kb[KB_DOWN] );}
static void IN_LeftDown( void ) {IN_KeyDown( &kb[KB_LEFT] );}
static void IN_LeftUp( void ) {IN_KeyUp( &kb[KB_LEFT] );}
static void IN_RightDown( void ) {IN_KeyDown( &kb[KB_RIGHT] );}
static void IN_RightUp( void ) {IN_KeyUp( &kb[KB_RIGHT] );}
static void IN_ForwardDown( void ) {IN_KeyDown( &kb[KB_FORWARD] );}
static void IN_ForwardUp( void ) {IN_KeyUp( &kb[KB_FORWARD] );}
static void IN_BackDown( void ) {IN_KeyDown( &kb[KB_BACK] );}
static void IN_BackUp( void ) {IN_KeyUp( &kb[KB_BACK] );}
static void IN_LookupDown( void ) {IN_KeyDown( &kb[KB_LOOKUP] );}
static void IN_LookupUp( void ) {IN_KeyUp( &kb[KB_LOOKUP] );}
static void IN_LookdownDown( void ) {IN_KeyDown( &kb[KB_LOOKDOWN] );}
static void IN_LookdownUp( void ) {IN_KeyUp( &kb[KB_LOOKDOWN] );}
static void IN_MoveleftDown( void ) {IN_KeyDown( &kb[KB_MOVELEFT] );}
static void IN_MoveleftUp( void ) {IN_KeyUp( &kb[KB_MOVELEFT] );}
static void IN_MoverightDown( void ) {IN_KeyDown( &kb[KB_MOVERIGHT] );}
static void IN_MoverightUp( void ) {IN_KeyUp( &kb[KB_MOVERIGHT] );}

static void IN_SpeedDown( void ) {IN_KeyDown( &kb[KB_SPEED] );}
static void IN_SpeedUp( void ) {IN_KeyUp( &kb[KB_SPEED] );}
static void IN_StrafeDown( void ) {IN_KeyDown( &kb[KB_STRAFE] );}
static void IN_StrafeUp( void ) {IN_KeyUp( &kb[KB_STRAFE] );}

static void IN_Button0Down( void ) {IN_KeyDown( &kb[KB_BUTTONS0] );}
static void IN_Button0Up( void ) {IN_KeyUp( &kb[KB_BUTTONS0] );}
static void IN_Button1Down( void ) {IN_KeyDown( &kb[KB_BUTTONS1] );}
static void IN_Button1Up( void ) {IN_KeyUp( &kb[KB_BUTTONS1] );}
static void IN_UseItemDown( void ) {IN_KeyDown( &kb[KB_BUTTONS2] );}
static void IN_UseItemUp( void ) {IN_KeyUp( &kb[KB_BUTTONS2] );}
static void IN_Button3Down( void ) {IN_KeyDown( &kb[KB_BUTTONS3] );}
static void IN_Button3Up( void ) {IN_KeyUp( &kb[KB_BUTTONS3] );}
static void IN_Button4Down( void ) {IN_KeyDown( &kb[KB_BUTTONS4] );}
static void IN_Button4Up( void ) {IN_KeyUp( &kb[KB_BUTTONS4] );}
// static void IN_Button5Down(void) {IN_KeyDown(&kb[KB_BUTTONS5]);}
// static void IN_Button5Up(void) {IN_KeyUp(&kb[KB_BUTTONS5]);}

// static void IN_Button6Down(void) {IN_KeyDown(&kb[KB_BUTTONS6]);}
// static void IN_Button6Up(void) {IN_KeyUp(&kb[KB_BUTTONS6]);}

// Rafael activate
static void IN_ActivateDown( void ) {IN_KeyDown( &kb[KB_BUTTONS6] );}
static void IN_ActivateUp( void ) {IN_KeyUp( &kb[KB_BUTTONS6] );}
// done.

static void IN_SprintDown( void ) {IN_KeyDown( &kb[KB_BUTTONS5] );}
static void IN_SprintUp( void ) {IN_KeyUp( &kb[KB_BUTTONS5] );}

// wbuttons (wolf buttons)
static void IN_Wbutton0Down( void )  { IN_KeyDown( &kb[KB_WBUTTONS0] );    }   //----(SA) secondary fire button
static void IN_Wbutton0Up( void )    { IN_KeyUp( &kb[KB_WBUTTONS0] );  }
static void IN_ZoomDown( void )      { IN_KeyDown( &kb[KB_WBUTTONS1] );    }   //----(SA)	zoom key
static void IN_ZoomUp( void )        { IN_KeyUp( &kb[KB_WBUTTONS1] );  }
static void IN_ReloadDown( void )    { IN_KeyDown( &kb[KB_WBUTTONS3] );    }   //----(SA)	manual weapon re-load
static void IN_ReloadUp( void )      { IN_KeyUp( &kb[KB_WBUTTONS3] );  }
static void IN_LeanLeftDown( void )  { IN_KeyDown( &kb[KB_WBUTTONS4] );    }   //----(SA)	lean left
static void IN_LeanLeftUp( void )    { IN_KeyUp( &kb[KB_WBUTTONS4] );  }
static void IN_LeanRightDown( void ) { IN_KeyDown( &kb[KB_WBUTTONS5] );    }   //----(SA)	lean right
static void IN_LeanRightUp( void )   { IN_KeyUp( &kb[KB_WBUTTONS5] );  }

// Rafael Kick
// Arnout: now wbutton prone
static void IN_ProneDown( void ) {IN_KeyDown( &kb[KB_WBUTTONS7] );}
static void IN_ProneUp( void ) {IN_KeyUp( &kb[KB_WBUTTONS7] );}

//==========================================================================


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
static void CL_AdjustAngles( void ) {
	float	speed;

	if ( kb[KB_SPEED].active ) {
		speed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.frametime;
	}

	if ( !kb[KB_STRAFE].active ) {
		cl.viewangles[YAW] -= speed * cl_yawspeed->value * CL_KeyState( &kb[KB_RIGHT] );
		cl.viewangles[YAW] += speed * cl_yawspeed->value * CL_KeyState( &kb[KB_LEFT] );
	}

	cl.viewangles[PITCH] -= speed * cl_pitchspeed->value * CL_KeyState( &kb[KB_LOOKUP] );
	cl.viewangles[PITCH] += speed * cl_pitchspeed->value * CL_KeyState( &kb[KB_LOOKDOWN] );
}


/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
static void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	//
	// adjust for speed key / running
	// the walking flag is to keep animations consistent
	// even during acceleration and develeration
	//
	if ( kb[KB_SPEED].active ^ cl_run->integer ) {
		movespeed = 127;
		cmd->buttons &= ~BUTTON_WALKING;
	} else {
		cmd->buttons |= BUTTON_WALKING;
		movespeed = 64;
	}

	forward = 0;
	side = 0;
	up = 0;
	if ( kb[KB_STRAFE].active ) {
		side += movespeed * CL_KeyState( &kb[KB_RIGHT] );
		side -= movespeed * CL_KeyState( &kb[KB_LEFT] );
	}

	side += movespeed * CL_KeyState( &kb[KB_MOVERIGHT] );
	side -= movespeed * CL_KeyState( &kb[KB_MOVELEFT] );

//----(SA)	added
	if ( cmd->buttons & BUTTON_ACTIVATE ) {
		if ( side > 0 ) {
			cmd->wbuttons |= WBUTTON_LEANRIGHT;
		} else if ( side < 0 ) {
			cmd->wbuttons |= WBUTTON_LEANLEFT;
		}

		side = 0;   // disallow the strafe when holding 'activate'
	}
//----(SA)	end

	up += movespeed * CL_KeyState( &kb[KB_UP] );
	up -= movespeed * CL_KeyState( &kb[KB_DOWN] );

	forward += movespeed * CL_KeyState( &kb[KB_FORWARD] );
	forward -= movespeed * CL_KeyState( &kb[KB_BACK] );

	// fretn - moved this to bg_pmove.c
	//if (!(cl.snap.ps.persistant[PERS_HWEAPON_USE]))
	//{
	cmd->forwardmove = ClampCharMove( forward );
	cmd->rightmove = ClampCharMove( side );
	cmd->upmove = ClampCharMove( up );
	//}

	// Arnout: double tap
	cmd->doubleTap = DT_NONE; // reset
	if ( cl_doubletapdelay->integer > 0 ) {
		if ( com_frameTime - cl.doubleTap.lastdoubleTap > cl_doubletapdelay->integer + 150 + cls.frametime ) {   // double tap only once every 500 msecs (add
																												// frametime for low(-ish) fps situations)
			int i;
			qboolean key_down;

			for ( i = 1; i < DT_NUM; i++ ) {
				key_down = kb[dtmapping[i]].active || kb[dtmapping[i]].wasPressed;

				if ( key_down && !cl.doubleTap.pressedTime[i] ) {
					cl.doubleTap.pressedTime[i] = com_frameTime;
				} else if ( !key_down && !cl.doubleTap.releasedTime[i]
							&& ( com_frameTime - cl.doubleTap.pressedTime[i] ) < ( cl_doubletapdelay->integer + cls.frametime ) ) {
					cl.doubleTap.releasedTime[i] = com_frameTime;
				} else if ( key_down && ( com_frameTime - cl.doubleTap.pressedTime[i] ) < ( cl_doubletapdelay->integer + cls.frametime )
							&& ( com_frameTime - cl.doubleTap.releasedTime[i] ) < ( cl_doubletapdelay->integer + cls.frametime ) ) {
					cl.doubleTap.pressedTime[i] = cl.doubleTap.releasedTime[i] = 0;
					cmd->doubleTap = i;
					cl.doubleTap.lastdoubleTap = com_frameTime;
				} else if ( !key_down && ( cl.doubleTap.pressedTime[i] || cl.doubleTap.releasedTime[i] ) ) {
					if ( com_frameTime - cl.doubleTap.pressedTime[i] >= ( cl_doubletapdelay->integer + cls.frametime ) ) {
						cl.doubleTap.pressedTime[i] = cl.doubleTap.releasedTime[i] = 0;
					}
				}
			}
		}
	}
}

/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy /*, int time*/ ) {
	if ( Key_GetCatcher() & KEYCATCH_UI ) {

		// NERVE - SMF - if we just want to pass it along to game
		if ( cl_bypassMouseInput->integer == 1 ) {
			cl.mouseDx[cl.mouseIndex] += dx;
			cl.mouseDy[cl.mouseIndex] += dy;
		} else {
			VM_Call( uivm, UI_MOUSE_EVENT, dx, dy );
		}

	} else if ( Key_GetCatcher() & KEYCATCH_CGAME ) {
		if ( cl_bypassMouseInput->integer == 1 ) {
			cl.mouseDx[cl.mouseIndex] += dx;
			cl.mouseDy[cl.mouseIndex] += dy;
		} else {
			VM_Call( cgvm, CG_MOUSE_EVENT, dx, dy );
		}
	} else {
		cl.mouseDx[cl.mouseIndex] += dx;
		cl.mouseDy[cl.mouseIndex] += dy;
	}
}


/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value /*, int time*/ ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		Com_Error( ERR_DROP, "CL_JoystickEvent: bad axis %i", axis );
	} else {
		cl.joystickAxis[axis] = value;
	}
}


/*
=================
CL_JoystickMove
=================
*/
static void CL_JoystickMove( usercmd_t *cmd ) {
	//int		movespeed;
	float	anglespeed;

	if ( kb[KB_SPEED].active ^ cl_run->integer ) {
		//movespeed = 2;
	} else {
		//movespeed = 1;
		cmd->buttons |= BUTTON_WALKING;
	}

	if ( kb[KB_SPEED].active ) {
		anglespeed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		anglespeed = 0.001 * cls.frametime;
	}

	if ( !kb[KB_STRAFE].active ) {
		cl.viewangles[YAW] += anglespeed * cl_yawspeed->value * cl.joystickAxis[AXIS_SIDE];
	} else {
		cmd->rightmove = ClampCharMove( cmd->rightmove + cl.joystickAxis[AXIS_SIDE] );
	}
	if ( kb[KB_MLOOK].active ) {
		cl.viewangles[PITCH] += anglespeed * cl_pitchspeed->value * cl.joystickAxis[AXIS_FORWARD];
	} else {
		cmd->forwardmove = ClampCharMove( cmd->forwardmove + cl.joystickAxis[AXIS_FORWARD] );
	}

	cmd->upmove = ClampCharMove( cmd->upmove + cl.joystickAxis[AXIS_UP] );
}


/*
=================
CL_MouseMove
=================
*/
static void CL_MouseMove( usercmd_t *cmd )
{
	// for normalizing m_forward/side to 125FPS
	const float deltaStrafeSensitivity = 0.008f * (1000.0f / (float)frame_msec);
	float mx, my;

	// allow mouse smoothing
	if (m_filter->integer)
	{
		mx = (cl.mouseDx[0] + cl.mouseDx[1]) * 0.5f;
		my = (cl.mouseDy[0] + cl.mouseDy[1]) * 0.5f;
	}
	else
	{
		mx = cl.mouseDx[cl.mouseIndex];
		my = cl.mouseDy[cl.mouseIndex];
	}

	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	if (mx == 0.0f && my == 0.0f)
		return;

	if ( cl_mouseAccel->value != 0.0f )
	{
		if ( cl_mouseAccelStyle->integer == 0 )
		{
			float accelSensitivity;
			float rate;

			rate = sqrt(mx * mx + my * my) / (float) frame_msec;

			accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
			mx *= accelSensitivity;
			my *= accelSensitivity;

			// Rafael - mg42
			if ( cl.snap.ps.persistant[PERS_HWEAPON_USE] )
			{
				mx *= 2.5; //(accelSensitivity * 0.1);
				my *= 2; //(accelSensitivity * 0.075);
			}
			else
			{
				mx *= accelSensitivity;
				my *= accelSensitivity;
			}

			if ( cl_showMouseRate->integer )
				Com_Printf( "rate: %f, accelSensitivity: %f\n", rate, accelSensitivity );
		}
		else
		{
			float rate[2];
			float power[2];
			float offset = cl_mouseAccelOffset->value;

			// clip at a small positive number to avoid division
			// by zero (or indeed going backwards!)
			if ( offset < 0.001f ) {
				offset = 0.001f;
			}

			// sensitivity remains pretty much unchanged at low speeds
			// cl_mouseAccel is a power value to how the acceleration is shaped
			// cl_mouseAccelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			rate[0] = fabsf( mx ) / (float) frame_msec;
			rate[1] = fabsf( my ) / (float) frame_msec;
			power[0] = powf( rate[0] / offset, cl_mouseAccel->value );
			power[1] = powf( rate[1] / offset, cl_mouseAccel->value );

			mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * offset);
			my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * offset);

			if(cl_showMouseRate->integer)
				Com_Printf("ratex: %f, ratey: %f, powx: %f, powy: %f\n", rate[0], rate[1], power[0], power[1]);
		}
	}
	else
	{
		mx *= cl_sensitivity->value;
		my *= cl_sensitivity->value;
	}

	// ingame FOV
	mx *= cl.cgameSensitivity;
	my *= cl.cgameSensitivity;

	// add mouse X/Y movement to cmd
	if(kb[KB_STRAFE].active)
		cmd->rightmove = ClampCharMove( cmd->rightmove + m_side->value * deltaStrafeSensitivity * mx );
	else
		cl.viewangles[YAW] -= m_yaw->value * mx;

	if ((kb[KB_MLOOK].active || cl_freelook->integer) && !kb[KB_STRAFE].active) 
		cl.viewangles[PITCH] += m_pitch->value * my;
	else
		cmd->forwardmove = ClampCharMove( cmd->forwardmove - m_forward->value * deltaStrafeSensitivity * my );
}


/*
==============
CL_CmdButtons
==============
*/
static void CL_CmdButtons( usercmd_t *cmd ) {
	int		i;

	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//
	// Note: increase this to < 8 for support for button7
	// And also add button7 to completion if desired
	for ( i = 0 ; i < 7 ; i++ ) {
		if ( kb[KB_BUTTONS0 + i].active || kb[KB_BUTTONS0 + i].wasPressed ) {
			cmd->buttons |= 1 << i;
		}
		kb[KB_BUTTONS0 + i].wasPressed = qfalse;
	}

	for ( i = 0 ; i < 8 ; i++ ) {     // Arnout: this was i < 7, but there are 8 wbuttons
		if ( kb[KB_WBUTTONS0 + i].active || kb[KB_WBUTTONS0 + i].wasPressed ) {
			cmd->wbuttons |= 1 << i;
		}
		kb[KB_WBUTTONS0 + i].wasPressed = qfalse;
	}

	if ( Key_GetCatcher() && !cl_bypassMouseInput->integer ) {
		cmd->buttons |= BUTTON_TALK;
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	if ( anykeydown && ( Key_GetCatcher() == 0 || cl_bypassMouseInput->integer ) ) {
		cmd->buttons |= BUTTON_ANY;
	}

	// Arnout: clear 'waspressed' from double tap buttons
	for ( i = 1; i < DT_NUM; i++ ) {
		kb[dtmapping[i]].wasPressed = qfalse;
	}
}


/*
==============
CL_FinishMove
==============
*/
static void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// copy the state that the cgame is currently sending
	cmd->weapon = cl.cgameUserCmdValue;

	cmd->flags = cl.cgameFlags;

	cmd->identClient = cl.cgameMpIdentClient;   // NERVE - SMF

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	for (i=0 ; i<3 ; i++) {
		cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i]);
	}
}


/*
=================
CL_CreateCmd
=================
*/
static usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd;
	vec3_t		oldAngles;
	float		recoilAdd;

	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
	CL_AdjustAngles ();

	Com_Memset( &cmd, 0, sizeof( cmd ) );

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove( &cmd );

	// get basic movement from mouse
	CL_MouseMove( &cmd );

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
	}

	// RF, set the kickAngles so aiming is affected
	recoilAdd = cl_recoilPitch->value;
	if ( Q_fabs( cl.viewangles[PITCH] + recoilAdd ) < 40 ) {
		cl.viewangles[PITCH] += recoilAdd;
	}
	// the recoilPitch has been used, so clear it out
	cl_recoilPitch->value = 0;

	// store out the final values
	CL_FinishMove( &cmd );

	// draw debug graphs of turning for mouse testing
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( fabsf( cl.viewangles[YAW] - oldAngles[YAW] ) );
		} else if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( fabsf( cl.viewangles[PITCH] - oldAngles[PITCH] ) );
		}
	}

	return cmd;
}


/*
=================
CL_CreateNewCommands

Create a new usercmd_t structure for this frame
=================
*/
static void CL_CreateNewCommands( void ) {
	int			cmdNum;

	// no need to create usercmds until we have a gamestate
	if ( cls.state < CA_PRIMED ) {
		return;
	}

	frame_msec = com_frameTime - old_com_frameTime;

	// if running over 1000fps, act as if each frame is 1ms
	// prevents divisions by zero
	if ( frame_msec < 1 ) {
		frame_msec = 1;
	}

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 ) {
		frame_msec = 200;
	}
	old_com_frameTime = com_frameTime;


	// generate a command for this frame
	cl.cmdNumber++;
	cmdNum = cl.cmdNumber & CMD_MASK;
	cl.cmds[cmdNum] = CL_CreateCmd();
}


/*
=================
CL_ReadyToSendPacket

Returns qfalse if we are over the maxpackets limit
and should choke back the bandwidth a bit by not sending
a packet this frame.  All the commands will still get
delivered in the next packet, but saving a header and
getting more delta compression will reduce total bandwidth.
=================
*/
static qboolean CL_ReadyToSendPacket( void ) {
	int		oldPacketNum;
	int		delta;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return qfalse;
	}

	// If we are downloading, we send no less than 50ms between packets
	if ( *cls.downloadTempName && cls.realtime - clc.lastPacketSentTime < 50 ) {
		return qfalse;
	}

	// if we don't have a valid gamestate yet, only send
	// one packet a second
	if ( cls.state != CA_ACTIVE &&
		cls.state != CA_PRIMED &&
		!*cls.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 1000 ) {
		return qfalse;
	}

	// send every frame for loopbacks
	if ( clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		return qtrue;
	}

	// send every frame for LAN
	if ( cl_lanForcePackets->integer && clc.netchan.isLANAddress ) {
		return qtrue;
	}

	oldPacketNum = (clc.netchan.outgoingSequence - 1) & PACKET_MASK;
	delta = cls.realtime - cl.outPackets[ oldPacketNum ].p_realtime;
	if ( delta < 1000 / cl_maxpackets->integer ) {
		// the accumulated commands will go out in the next packet
		return qfalse;
	}

	return qtrue;
}


/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[ MAX_MSGLEN_BUF ];
	int			i, j, n;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count, key;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return;
	}

	Com_Memset( &nullcmd, 0, sizeof(nullcmd) );
	oldcmd = &nullcmd;

	MSG_Init( &buf, data, MAX_MSGLEN );

	MSG_Bitstream( &buf );
	// write the current serverId so the server
	// can tell if this is from the current gameState
	MSG_WriteLong( &buf, cl.serverId );

	// write the last message we received, which can
	// be used for delta compression, and is also used
	// to tell if we dropped a gamestate
	MSG_WriteLong( &buf, clc.serverMessageSequence );

	// write the last reliable message we received
	MSG_WriteLong( &buf, clc.serverCommandSequence );

	// write any unacknowledged clientCommands
	// NOTE TTimo: if you verbose this, you will see that there are quite a few duplicates
	// typically several unacknowledged cp or userinfo commands stacked up
	n = clc.reliableSequence - clc.reliableAcknowledge;
	for ( i = 0; i < n; i++ ) {
		const int index = clc.reliableAcknowledge + 1 + i;
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, index );
		MSG_WriteString( &buf, clc.reliableCommands[ index & ( MAX_RELIABLE_COMMANDS - 1 ) ] );
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server

	oldPacketNum = (clc.netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	count = cl.cmdNumber - cl.outPackets[ oldPacketNum ].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}
	if ( count >= 1 ) {
		if ( cl_showSend->integer ) {
			Com_Printf( "(%i)", count );
		}

		// begin a client move command
		if ( cl_nodelta->integer || !cl.snap.valid || clc.demowaiting
			|| clc.serverMessageSequence != cl.snap.messageNum ) {
			MSG_WriteByte (&buf, clc_moveNoDelta);
		} else {
			MSG_WriteByte (&buf, clc_move);
		}

		// write the command count
		MSG_WriteByte( &buf, count );

		// use the checksum feed in the key
		key = clc.checksumFeed;
		// also use the message acknowledge
		key ^= clc.serverMessageSequence;
		// also use the last acknowledged server command in the key
		key ^= MSG_HashKey(clc.serverCommands[ clc.serverCommandSequence & (MAX_RELIABLE_COMMANDS-1) ], 32);

		// write all the commands, including the predicted command
		for ( i = 0 ; i < count ; i++ ) {
			j = (cl.cmdNumber - count + i + 1) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmdKey (&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.outPackets[ packetNum ].p_realtime = cls.realtime;
	cl.outPackets[ packetNum ].p_serverTime = oldcmd->serverTime;
	cl.outPackets[ packetNum ].p_cmdNumber = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%i ", buf.cursize );
	}

	CL_Netchan_Transmit( &clc.netchan, &buf );
}


/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( cls.state < CA_CONNECTED ) {
		return;
	}

	// don't send commands if paused
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return;
	}

	// we create commands even if a demo is playing,
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket() ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket();
}

static const cmdListItem_t input_cmds[] = {
	{ "-activate", IN_ActivateUp, NULL },
	{ "-attack", IN_Button0Up, NULL },
	{ "-attack2", IN_Wbutton0Up, NULL },
	{ "-back", IN_BackUp, NULL },
	{ "-button1", IN_Button1Up, NULL },
	{ "-button4", IN_Button4Up, NULL },
	{ "-forward", IN_ForwardUp, NULL },
	{ "-leanleft", IN_LeanLeftUp, NULL },
	{ "-leanright", IN_LeanRightUp, NULL },
	{ "-left", IN_LeftUp, NULL },
	{ "-lookdown", IN_LookdownUp, NULL },
	{ "-lookup", IN_LookupUp, NULL },
	{ "-mlook", IN_MLookUp, NULL },
	{ "-movedown", IN_DownUp, NULL },
	{ "-moveleft", IN_MoveleftUp, NULL },
	{ "-moveright", IN_MoverightUp, NULL },
	{ "-moveup", IN_UpUp, NULL },
	{ "-prone", IN_ProneUp, NULL },
	{ "-reload", IN_ReloadUp, NULL },
	{ "-right", IN_RightUp, NULL },
	{ "-salute", IN_Button3Up, NULL },
	{ "-speed", IN_SpeedUp, NULL },
	{ "-sprint", IN_SprintUp, NULL },
	{ "-strafe", IN_StrafeUp, NULL },
	{ "-useitem", IN_UseItemUp, NULL },
	{ "-zoom", IN_ZoomUp, NULL },
	{ "+activate", IN_ActivateDown, NULL },
	{ "+attack", IN_Button0Down, NULL },
	{ "+attack2", IN_Wbutton0Down, NULL },
	{ "+back", IN_BackDown, NULL },
	{ "+button1", IN_Button1Down, NULL },
	{ "+button4", IN_Button4Down, NULL },
	{ "+forward", IN_ForwardDown, NULL },
	{ "+leanleft", IN_LeanLeftDown, NULL },
	{ "+leanright", IN_LeanRightDown, NULL },
	{ "+left", IN_LeftDown, NULL },
	{ "+lookdown", IN_LookdownDown, NULL },
	{ "+lookup", IN_LookupDown, NULL },
	{ "+mlook", IN_MLookDown, NULL },
	{ "+movedown", IN_DownDown, NULL },
	{ "+moveleft", IN_MoveleftDown, NULL },
	{ "+moveright", IN_MoverightDown, NULL },
	{ "+moveup", IN_UpDown, NULL },
	{ "+prone", IN_ProneDown, NULL },
	{ "+reload", IN_ReloadDown , NULL },
	{ "+right", IN_RightDown, NULL },
	{ "+salute", IN_Button3Down, NULL },
	{ "+speed", IN_SpeedDown, NULL },
	{ "+sprint", IN_SprintDown, NULL },
	{ "+strafe", IN_StrafeDown, NULL },
	{ "+useitem", IN_UseItemDown, NULL },
	{ "+zoom", IN_ZoomDown, NULL },
};


/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	Cmd_RegisterArray( input_cmds, MODULE_INPUT );

	cl_nodelta = Cvar_Get( "cl_nodelta", "0", CVAR_DEVELOPER );
	Cvar_SetDescription( cl_nodelta, "Flag server to disable delta compression on server snapshots" );
	cl_debugMove = Cvar_Get( "cl_debugMove", "0", 0 );
	Cvar_CheckRange( cl_debugMove, "0", "2", CV_INTEGER );
	Cvar_SetDescription( cl_debugMove, "Prints a graph of view angle deltas\n 0: Disabled\n 1: Yaw\n 2: Pitch" );

	cl_showSend = Cvar_Get( "cl_showSend", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_showSend, "Prints client to server packet information" );

	cl_yawspeed = Cvar_Get( "cl_yawspeed", "140", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_yawspeed, "Yaw change speed when holding down +left or +right button" );
	cl_pitchspeed = Cvar_Get( "cl_pitchspeed", "140", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_pitchspeed, "Pitch change speed when holding down look +lookup or +lookdown button" );
	cl_anglespeedkey = Cvar_Get( "cl_anglespeedkey", "1.5", 0 );
	Cvar_SetDescription( cl_anglespeedkey, "Angle change scale when holding down +speed button" );

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "125", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_maxpackets, "15", "125", CV_INTEGER );
	Cvar_SetDescription( cl_maxpackets, "Set how many client packets are sent to the server per second, can't exceed \\com_maxFPS" );
	cl_packetdup = Cvar_Get( "cl_packetdup", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cl_packetdup, "0", "5", CV_INTEGER );
	Cvar_SetDescription( cl_packetdup, "Limits the number of previous client commands added in packet, helps in packet loss mitigation, increases client command packets size a bit" );

	cl_run = Cvar_Get( "cl_run", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_run, "Persistent player running movement" );
	cl_sensitivity = Cvar_Get( "sensitivity", "5", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_sensitivity, "Sets base mouse sensitivity (mouse speed)" );
	cl_mouseAccel = Cvar_Get( "cl_mouseAccel", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_mouseAccel, "Toggle the use of mouse acceleration the mouse speeds up or becomes more sensitive as it continues in one direction" );
	cl_freelook = Cvar_Get( "cl_freelook", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_freelook, "Mouse aiming" );

	// 0: legacy mouse acceleration
	// 1: new implementation
	cl_mouseAccelStyle = Cvar_Get( "cl_mouseAccelStyle", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_mouseAccelStyle, "Mouse Acceleration Style:\n" 
		" 0 - Vanilla Quake 3 / Enemy Territory style\n"
		" 1 - QuakeLive style" );
	// offset for the power function (for style 1, ignored otherwise)
	// this should be set to the max rate value
	cl_mouseAccelOffset = Cvar_Get( "cl_mouseAccelOffset", "5", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cl_mouseAccelOffset, "0.001", "50000", CV_FLOAT );
	Cvar_SetDescription( cl_mouseAccelOffset, "Sets how much base mouse delta will be doubled by acceleration. Requires '\\cl_mouseAccelStyle 1'" );

	cl_showMouseRate = Cvar_Get( "cl_showMouseRate", "0", 0 );
	Cvar_SetDescription( cl_showMouseRate, "Prints mouse acceleration info when 'cl_mouseAccel' has a value set (rate of mouse samples per frame)" );

	m_pitch = Cvar_Get( "m_pitch", "0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_pitch, "Mouse pitch scale" );
	m_yaw = Cvar_Get( "m_yaw", "0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_yaw, "Mouse yaw scale" );
	m_forward = Cvar_Get( "m_forward", "0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_forward, "Mouse forward scale" );
	m_side = Cvar_Get( "m_side", "0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_side, "Mouse side scale" );
//#ifdef __APPLE__
	// Input is jittery on OS X w/o this
//	m_filter = Cvar_Get( "m_filter", "1", CVAR_ARCHIVE_ND );
//#else
	m_filter = Cvar_Get( "m_filter", "0", CVAR_ARCHIVE_ND );
//#endif
	Cvar_SetDescription( m_filter, "Smooths mouse movement" );

	// RF
	cl_recoilPitch = Cvar_Get( "cg_recoilPitch", "0", CVAR_ROM | CVAR_NOTABCOMPLETE );

	cl_bypassMouseInput = Cvar_Get( "cl_bypassMouseInput", "0", 0 ); //CVAR_ROM );			// NERVE - SMF

	cl_doubletapdelay = Cvar_Get( "cl_doubletapdelay", "0", CVAR_ARCHIVE_ND ); // Arnout: double tap
	Cvar_CheckRange( cl_doubletapdelay, "0", NULL, CV_INTEGER );
	Cvar_SetDescription( cl_doubletapdelay, "Time in ms between keypresses required for double tap actions" );
}


/*
============
CL_ShutdownInput
============
*/
void CL_ShutdownInput( void ) {
	Cmd_UnregisterModule( MODULE_INPUT );
}
