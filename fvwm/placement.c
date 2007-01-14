/* -*-c-*- */
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This module is all new
 * by Rob Nation
 *
 * This code does smart-placement initial window placement stuff
 *
 * Copyright 1994 Robert Nation. No restrictions are placed on this code,
 * as long as the copyright notice is preserved . No guarantees or
 * warrantees of any sort whatsoever are given or implied or anything.
 */

/* ---------------------------- included header files ---------------------- */
#include "config.h"
#if 1 /*!!!*/
#undef inline
#define inline
#endif

#include <stdio.h>

#include "libs/fvwmlib.h"
#include "libs/FScreen.h"
#include "fvwm.h"
#include "externs.h"
#include "execcontext.h"
#include "cursor.h"
#include "bindings.h"
#include "misc.h"
#include "screen.h"
#include "placement.h"
#include "geometry.h"
#include "update.h"
#include "style.h"
#include "move_resize.h"
#include "virtual.h"
#include "stack.h"
#include "ewmh.h"
#include "icons.h"
#include "add_window.h"

/* ---------------------------- local definitions -------------------------- */

/*
 * CleverPlacement macros
 */
#define CP_GET_NEXT_STEP 5

/* ---------------------------- local macros ------------------------------- */

#ifndef MIN
#define MIN(A,B) ((A)<(B)? (A):(B))
#endif
#ifndef MAX
#define MAX(A,B) ((A)>(B)? (A):(B))
#endif

/* ---------------------------- imports ------------------------------------ */

/* ---------------------------- included code files ------------------------ */

/* ---------------------------- local types -------------------------------- */

typedef enum
{
	PR_POS_NORMAL = 0,
	PR_POS_IGNORE_PPOS,
	PR_POS_USE_PPOS,
	PR_POS_IGNORE_USPOS,
	PR_POS_USE_USPOS,
	PR_POS_PLACE_AGAIN,
	PR_POS_CAPTURE,
	PR_POS_USPOS_OVERRIDE_SOS
} preason_pos_t;

typedef enum
{
	PR_SCREEN_CURRENT = 0,
	PR_SCREEN_STYLE,
	PR_SCREEN_X_RESOURCE_FVWMSCREEN,
	PR_SCREEN_IGNORE_CAPTURE
} preason_screen_t;

typedef enum
{
	PR_PAGE_CURRENT = 0,
	PR_PAGE_STYLE,
	PR_PAGE_X_RESOURCE_PAGE,
	PR_PAGE_IGNORE_CAPTURE,
	PR_PAGE_IGNORE_INVALID,
	PR_PAGE_STICKY
} preason_page_t;

typedef enum
{
	PR_DESK_CURRENT = 0,
	PR_DESK_STYLE,
	PR_DESK_X_RESOURCE_DESK,
	PR_DESK_X_RESOURCE_PAGE,
	PR_DESK_CAPTURE,
	PR_DESK_STICKY,
	PR_DESK_WINDOW_GROUP_LEADER,
	PR_DESK_WINDOW_GROUP_MEMBER,
	PR_DESK_TRANSIENT,
	PR_DESK_XPROP_XA_WM_DESKTOP
} preason_desk_t;

typedef struct
{
	struct
	{
		preason_pos_t reason;
		int x;
		int y;
		int algo;
		unsigned do_not_manual_icon_placement : 1;
		unsigned do_adjust_off_screen : 1;
		unsigned do_adjust_off_page : 1;
		unsigned has_tile_failed : 1;
		unsigned has_manual_failed : 1;
	} pos;
	struct
	{
		preason_screen_t reason;
		int screen;
		rectangle g;
		unsigned was_modified_by_ewmh_workingarea : 1;
	} screen;
	struct
	{
		preason_page_t reason;
		int px;
		int py;
		unsigned do_switch_page : 1;
		unsigned do_honor_starts_on_page : 1;
		unsigned do_ignore_starts_on_page : 1;
	} page;
	struct
	{
		preason_desk_t reason;
		preason_desk_t sod_reason;
		int desk;
		unsigned do_switch_desk : 1;
	} desk;
} placement_reason_t;

typedef struct
{
	int desk;
	int page_x;
	int page_y;
	int screen;
} placement_start_style_t;

typedef struct
{
	unsigned do_forbid_manual_placement : 1;
	unsigned do_honor_starts_on_page : 1;
	unsigned do_honor_starts_on_screen : 1;
	unsigned is_smartly_placed : 1;
	unsigned do_not_use_wm_placement : 1;
} placement_flags_t;

/*
 * CleverPlacement types
 */
typedef enum
{
	CP_LOOP_END,
	CP_LOOP_CONT
} cp_loop_rc_t;

typedef struct
{
	unsigned use_percent : 1;
	unsigned use_ewmh_dynamic_working_areapercent : 1;
} cp_flags_t;

typedef struct
{
	FvwmWindow *place_fw;
	rectangle place_g;
	position place_p2;
	rectangle screen_g;
	position page_p1;
	position page_p2;
	position pdelta_p;
	cp_flags_t flags;
	position best_p;
	float best_penalty;
} cp_arg_t;

/* ---------------------------- forward declarations ----------------------- */

/* ---------------------------- local variables ---------------------------- */

/* ---------------------------- exported variables (globals) --------------- */

/* ---------------------------- local functions (CleverPlacement) ---------- */

static inline int __cp_get_next_x(const cp_arg_t *arg)
{
	FvwmWindow *other_fw;
	int xnew;
	int xtest;
	int stickyx;
	int stickyy;
	int start,i;
	int win_left;
	rectangle g;
	Bool rc;
	int x;
	int y;

	x = arg->place_g.x;
	y = arg->place_g.y;
	if (arg->flags.use_percent == 1)
	{
		start = 0;
	}
	else
	{
		start = CP_GET_NEXT_STEP;
	}

	/* Test window at far right of screen */
	xnew = arg->page_p2.x;
	xtest = arg->page_p2.x - arg->place_g.width;
	if (xtest > x)
	{
		xnew = xtest;
	}
	/* test the borders of the working area */
	xtest = arg->page_p1.x + Scr.Desktops->ewmh_working_area.x;
	if (xtest > x)
	{
		xnew = MIN(xnew, xtest);
	}
	xtest = arg->page_p1.x +
		(Scr.Desktops->ewmh_working_area.x +
		 Scr.Desktops->ewmh_working_area.width) -
		arg->place_g.width;
	if (xtest > x)
	{
		xnew = MIN(xnew, xtest);
	}
	/* Test the values of the right edges of every window */
	for (
		other_fw = Scr.FvwmRoot.next; other_fw != NULL;
		other_fw = other_fw->next)
	{
		if (
			other_fw == arg->place_fw ||
			(other_fw->Desk != arg->place_fw->Desk &&
			 !IS_STICKY_ACROSS_DESKS(other_fw)) ||
			IS_EWMH_DESKTOP(FW_W(other_fw)))
		{
			continue;
		}
		if (IS_STICKY_ACROSS_PAGES(other_fw))
		{
			stickyx = arg->pdelta_p.x;
			stickyy = arg->pdelta_p.y;
		}
		else
		{
			stickyx = 0;
			stickyy = 0;
		}
		if (IS_ICONIFIED(other_fw))
		{
			rc = get_visible_icon_geometry(other_fw, &g);
			if (rc == True && y < g.y + g.height - stickyy &&
			    g.y - stickyy < arg->place_g.height + y)
			{
				win_left = arg->page_p1.x + g.x - stickyx -
					arg->place_g.width;
				for (i = start; i <= CP_GET_NEXT_STEP; i++)
				{
					xtest = win_left + g.width *
						(CP_GET_NEXT_STEP - i) /
						CP_GET_NEXT_STEP;
					if (xtest > x)
					{
						xnew = MIN(xnew, xtest);
					}
				}
				win_left = arg->page_p1.x + g.x - stickyx;
				for (i = start; i <= CP_GET_NEXT_STEP; i++)
				{
					xtest = (win_left) + g.width * i /
						CP_GET_NEXT_STEP;
					if (xtest > x)
					{
						xnew = MIN(xnew, xtest);
					}
				}
			}
		}
		else if (
			y < other_fw->g.frame.height + other_fw->g.frame.y -
			stickyy &&
			other_fw->g.frame.y - stickyy <
			arg->place_g.height + y &&
			arg->page_p1.x < other_fw->g.frame.width +
			other_fw->g.frame.x - stickyx &&
			other_fw->g.frame.x - stickyx < arg->page_p2.x)
		{
			win_left =
				arg->screen_g.x + other_fw->g.frame.x -
				stickyx - arg->place_g.width;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				xtest = win_left + other_fw->g.frame.width *
					(CP_GET_NEXT_STEP - i) /
					CP_GET_NEXT_STEP;
				if (xtest > x)
				{
					xnew = MIN(xnew, xtest);
				}
			}
			win_left = arg->screen_g.x + other_fw->g.frame.x -
				stickyx;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				xtest = win_left + other_fw->g.frame.width *
					i / CP_GET_NEXT_STEP;
				if (xtest > x)
				{
					xnew = MIN(xnew, xtest);
				}
			}
		}
	}

	return xnew;
}

static inline int __cp_get_next_y(const cp_arg_t *arg)
{
	FvwmWindow *other_fw;
	int ynew;
	int ytest;
	int stickyy;
	int win_top;
	int start;
	int i;
	rectangle g;
	int y;

	y = arg->place_g.y;
	if (arg->flags.use_percent == 1)
	{
		start = 0;
	}
	else
	{
		start = CP_GET_NEXT_STEP;
	}

	/* Test window at far bottom of screen */
	ynew = arg->page_p2.y;
	ytest = arg->page_p2.y - arg->place_g.height;
	if (ytest > y)
	{
		ynew = ytest;
	}
	/* test the borders of the working area */
	ytest = arg->page_p1.y + Scr.Desktops->ewmh_working_area.y;
	if (ytest > y)
	{
		ynew = MIN(ynew, ytest);
	}
	ytest = arg->screen_g.y +
		(Scr.Desktops->ewmh_working_area.y +
		 Scr.Desktops->ewmh_working_area.height) -
		arg->place_g.height;
	if (ytest > y)
	{
		ynew = MIN(ynew, ytest);
	}
	/* Test the values of the bottom edge of every window */
	for (
		other_fw = Scr.FvwmRoot.next; other_fw != NULL;
		other_fw = other_fw->next)
	{
		if (
			other_fw == arg->place_fw ||
			(
				other_fw->Desk != arg->place_fw->Desk &&
				!IS_STICKY_ACROSS_DESKS(other_fw)) ||
			IS_EWMH_DESKTOP(FW_W(other_fw)))
		{
			continue;
		}

		if (IS_STICKY_ACROSS_PAGES(other_fw))
		{
			stickyy = arg->pdelta_p.y;
		}
		else
		{
			stickyy = 0;
		}

		if (IS_ICONIFIED(other_fw))
		{
			get_visible_icon_geometry(other_fw, &g);
			win_top = g.y - stickyy;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				ytest =
					win_top + g.height * i /
					CP_GET_NEXT_STEP;
				if (ytest > y)
				{
					ynew = MIN(ynew, ytest);
				}
			}
			win_top = g.y - stickyy - arg->place_g.height;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				ytest =
					win_top + g.height *
					(CP_GET_NEXT_STEP - i) /
					CP_GET_NEXT_STEP;
				if (ytest > y)
				{
					ynew = MIN(ynew, ytest);
				}
			}
		}
		else
		{
			win_top = other_fw->g.frame.y - stickyy;;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				ytest =
					win_top + other_fw->g.frame.height *
					i / CP_GET_NEXT_STEP;
				if (ytest > y)
				{
					ynew = MIN(ynew, ytest);
				}
			}
			win_top = other_fw->g.frame.y - stickyy -
				arg->place_g.height;
			for (i = start; i <= CP_GET_NEXT_STEP; i++)
			{
				ytest =
					win_top + other_fw->g.frame.height *
					(CP_GET_NEXT_STEP - i) /
					CP_GET_NEXT_STEP;
				if (ytest > y)
				{
					ynew = MIN(ynew, ytest);
				}
			}
		}
	}

	return ynew;
}

static inline cp_loop_rc_t __cp_get_first_pos(
	position *ret_p, const cp_arg_t *arg)
{
	/* top left corner of page */
	ret_p->x = arg->page_p1.x;
	ret_p->y = arg->page_p1.y;

	return CP_LOOP_CONT;
}

static inline cp_loop_rc_t __cp_get_next_pos(
	position *ret_p, const cp_arg_t *arg)
{
	ret_p->x = arg->place_g.x;
	ret_p->y = arg->place_g.y;
	if (ret_p->x + arg->place_g.width <= arg->page_p2.x)
	{
		/* try next x */
		ret_p->x = __cp_get_next_x(arg);
		ret_p->y = arg->place_g.y;
	}
	if (ret_p->x + arg->place_g.width > arg->page_p2.x)
	{
		/* out of room in x direction. Try next y. Reset x.*/
		ret_p->x = arg->page_p1.x;
		ret_p->y = __cp_get_next_y(arg);
	}
	if (ret_p->y + arg->place_g.height > arg->page_p2.y)
	{
		/* PageBottom */
		return CP_LOOP_END;
	}

	return CP_LOOP_CONT;
}

static inline float __cp_get_avoidance_penalty(
	const cp_arg_t *arg, FvwmWindow *other_fw, const rectangle *other_g)
{
	float anew;
	float avoidance_factor;
	position other_p2;

	other_p2.x = other_g->x + other_g->width;
	other_p2.y = other_g->y + other_g->height;
	{
		long x1 = MAX(arg->place_g.x, other_g->x);
		long x2 = MIN(arg->place_p2.x, other_p2.x);
		long y1 = MAX(arg->place_g.y, other_g->y);
		long y2 = MIN(arg->place_p2.y, other_p2.y);

		/* overlapping area in pixels (windows are guaranteed to
		 * overlap when this function is called) */
		anew = (x2 - x1) * (y2 - y1);
	}
	if (IS_ICONIFIED(other_fw))
	{
		avoidance_factor = ICON_PLACEMENT_PENALTY(other_fw);
	}
	else if (compare_window_layers(other_fw, arg->place_fw) > 0)
	{
		avoidance_factor = ONTOP_PLACEMENT_PENALTY(other_fw);
	}
	else if (compare_window_layers(other_fw, arg->place_fw) < 0)
	{
		avoidance_factor = BELOW_PLACEMENT_PENALTY(other_fw);
	}
	else if (
		IS_STICKY_ACROSS_PAGES(other_fw) ||
		IS_STICKY_ACROSS_DESKS(other_fw))
	{
		avoidance_factor = STICKY_PLACEMENT_PENALTY(other_fw);
	}
	else
	{
		avoidance_factor = NORMAL_PLACEMENT_PENALTY(other_fw);
	}
	if (arg->flags.use_percent == 1)
	{
		float cover_factor;
		long other_area;
		long place_area;

		other_area = other_g->width * other_g->height;
		place_area = arg->place_g.width * arg->place_g.height;
		cover_factor = 0;
		if (other_area != 0 && place_area != 0)
		{
			anew = 100 * MAX(anew / other_area, anew / place_area);
			if (anew >= 99)
			{
				cover_factor = PERCENTAGE_99_PENALTY(other_fw);
			}
			else if (anew > 94)
			{
				cover_factor = PERCENTAGE_95_PENALTY(other_fw);
			}
			else if (anew > 84)
			{
				cover_factor = PERCENTAGE_85_PENALTY(other_fw);
			}
			else if (anew > 74)
			{
				cover_factor = PERCENTAGE_75_PENALTY(other_fw);
			}
		}
		if (avoidance_factor >= 1)
		{
			avoidance_factor += cover_factor;
		}
	}
	if (
		arg->flags.use_ewmh_dynamic_working_areapercent == 1 &&
		DO_EWMH_IGNORE_STRUT_HINTS(other_fw) == 0 &&
		(
			other_fw->dyn_strut.left > 0 ||
			other_fw->dyn_strut.right > 0 ||
			other_fw->dyn_strut.top > 0 ||
			other_fw->dyn_strut.bottom > 0))
	{
		/* if we intersect a window which reserves space */
		avoidance_factor += (avoidance_factor >= 1) ?
			EWMH_STRUT_PLACEMENT_PENALTY(arg->place_fw) : 0;
	}
	anew *= avoidance_factor;

	return anew;
}

static inline float __cp_test_fit(const cp_arg_t *arg)
{
	FvwmWindow *other_fw;
	float penalty;

	penalty = 0;
	for (
		other_fw = Scr.FvwmRoot.next; other_fw != NULL;
		other_fw = other_fw->next)
	{
		rectangle other_g;
		Bool rc;

		if (
			arg->place_fw == other_fw ||
			IS_EWMH_DESKTOP(FW_W(other_fw)))
		{
			continue;
		}
		/*  RBW - account for sticky windows...  */
		if (
			other_fw->Desk != arg->place_fw->Desk &&
			IS_STICKY_ACROSS_DESKS(other_fw) == 0)
		{
			continue;
		}
		rc = get_visible_window_or_icon_geometry(other_fw, &other_g);
		if (IS_STICKY_ACROSS_PAGES(other_fw))
		{
			other_g.x -= arg->pdelta_p.x;
			other_g.y -= arg->pdelta_p.y;
		}
		if (
			arg->place_g.x < other_g.x + other_g.width &&
			arg->place_p2.x > other_g.x &&
			arg->place_g.y < other_g.y + other_g.height &&
			arg->place_p2.y > other_g.y)
		{
			float anew;

			anew = __cp_get_avoidance_penalty(
				arg, other_fw, &other_g);
			penalty += anew;
			if (
				penalty > arg->best_penalty &&
				arg->best_penalty != -1)
			{
				/* stop looking; the penalty is too high */
				return penalty;
			}
		}
	}
	/* now handle the working area */
	if (arg->flags.use_ewmh_dynamic_working_areapercent == 1)
	{
		penalty += EWMH_STRUT_PLACEMENT_PENALTY(arg->place_fw) *
			EWMH_GetStrutIntersection(
				arg->place_g.x, arg->place_g.y,
				arg->place_p2.x, arg->place_p2.y,
				arg->flags.use_percent);
	}
	else
	{
		/* EWMH_USE_DYNAMIC_WORKING_AREA, count the base strut */
		penalty +=
			EWMH_STRUT_PLACEMENT_PENALTY(arg->place_fw) *
			EWMH_GetBaseStrutIntersection(
				arg->place_g.x, arg->place_g.y,
				arg->place_p2.x, arg->place_p2.y,
				arg->flags.use_percent);
	}

	return penalty;
}

/* CleverPlacement by Anthony Martin <amartin@engr.csulb.edu>
 * This function will place a new window such that there is a minimum amount
 * of interference with other windows.  If it can place a window without any
 * interference, fine.  Otherwise, it places it so that the area of of
 * interference between the new window and the other windows is minimized */
static void CleverPlacement(
	FvwmWindow *place_fw, style_flags *sflags, rectangle *screen_g,
	int *x, int *y, int pdeltax, int pdeltay, int use_percent)
{
	position next_p;
	/* area of interference */
	float penalty;
	cp_loop_rc_t loop_rc;
	cp_arg_t arg;

	{
		memset(&arg, 0, sizeof(arg));
		arg.place_fw = place_fw;
		arg.place_g = place_fw->g.frame;
		arg.screen_g = *screen_g;
		arg.page_p1.x = arg.screen_g.x - pdeltax;
		arg.page_p1.y = arg.screen_g.y - pdeltay;
		arg.page_p2.x = arg.page_p1.x + screen_g->width;
		arg.page_p2.y = arg.page_p1.y + screen_g->height;
		arg.pdelta_p.x = pdeltax;
		arg.pdelta_p.y = pdeltay;
		arg.flags.use_percent = !!use_percent;
		if (SEWMH_PLACEMENT_MODE(sflags) == EWMH_USE_WORKING_AREA)
		{
			arg.flags.use_ewmh_dynamic_working_areapercent = 1;
		}
		arg.best_penalty = -1.0;
		loop_rc = __cp_get_first_pos(&next_p, &arg);
		arg.place_g.x = next_p.x;
		arg.place_g.y = next_p.y;
		arg.best_p.x = next_p.x;
		arg.best_p.y = next_p.y;
	}
	while (loop_rc != CP_LOOP_END)
	{
		arg.place_p2.x = arg.place_g.x + arg.place_g.width;
		arg.place_p2.y =
			arg.place_g.y + arg.place_g.height;
		penalty = __cp_test_fit(&arg);
		/* I've added +0.0001 because with my machine the < test fail
		 * with certain *equal* float numbers! */
		if (
			penalty >= 0 &&
			(
				arg.best_penalty < 0 ||
				penalty + 0.0001 < arg.best_penalty))
		{
			arg.best_p.x = arg.place_g.x;
			arg.best_p.y = arg.place_g.y;
			arg.best_penalty = penalty;
		}
		if (penalty == 0)
		{
			break;
		}
		loop_rc = __cp_get_next_pos(&next_p, &arg);
		arg.place_g.x = next_p.x;
		arg.place_g.y = next_p.y;
	}
	*x = arg.best_p.x;
	*y = arg.best_p.y;

	return;
}

/* ---------------------------- local functions (SmartPlacement) ----------- */

/* returns -1 if windows do not overlap
 * returns >= 0 (the window's next x position to try) if windows do overlap
 */
static inline int __sp_test_window(
	FvwmWindow *place_fw, FvwmWindow *other_fw,
	const rectangle *place_g, int pdeltax, int pdeltay)
{
	Bool rc;
	rectangle other_g;

	if (place_fw == other_fw || IS_EWMH_DESKTOP(FW_W(other_fw)))
	{
		return -1;
	}
	/*  RBW - account for sticky windows...  */
	if (
		other_fw->Desk != place_fw->Desk &&
		IS_STICKY_ACROSS_DESKS(other_fw) == 0)
	{
		return -1;
	}
	rc = get_visible_window_or_icon_geometry(other_fw, &other_g);
	if (
		rc == True &&
		(PLACEMENT_AVOID_ICON == 0 || !IS_ICONIFIED(other_fw)))
	{
		if (IS_STICKY_ACROSS_PAGES(other_fw))
		{
			other_g.x -= pdeltax;
			other_g.y -= pdeltay;
		}
		if (
			other_g.x < place_g->x + place_g->width  &&
			place_g->x < other_g.x + other_g.width &&
			other_g.y < place_g->y + place_g->height &&
			place_g->y < other_g.y + other_g.height)
		{
			/* window overlaps, look for a different place */
			return other_g.x + other_g.width - 1;
		}
	}

	return -1;
}

static int SmartPlacement(
	FvwmWindow *place_fw, rectangle *screen_g,
	int width, int height, int *x, int *y, int pdeltax, int pdeltay)
{
	int PageLeft   = screen_g->x - pdeltax;
	int PageTop    = screen_g->y - pdeltay;
	int PageRight  = PageLeft + screen_g->width;
	int PageBottom = PageTop + screen_g->height;
	int loc_ok = False;
	rectangle place_g;

	place_g.x = PageLeft;
	place_g.y = PageTop;
	place_g.width = width;
	place_g.height = height;
	for (; place_g.y + place_g.height < PageBottom && loc_ok == False; )
	{
		place_g.x = PageLeft;
		while (
			place_g.x + place_g.width < PageRight &&
			loc_ok == False)
		{
			FvwmWindow *other_fw;

			loc_ok = True;
			for (
				other_fw = Scr.FvwmRoot.next;
				other_fw != NULL && loc_ok == False;
				other_fw = other_fw->next)
			{
				int next_x;

				next_x = __sp_test_window(
					place_fw, other_fw, &place_g, pdeltax,
					pdeltay);
				if (next_x >= 0)
				{
					loc_ok = False;
					place_g.x = next_x;
				}
			}
			if (loc_ok == False)
			{
				place_g.x += 1;
			}
		}
		if (loc_ok == False)
		{
			place_g.y += 1;
		}
	}
	if (loc_ok == True)
	{
		*x = place_g.x;
		*y = place_g.y;
	}

	return loc_ok;
}

/* ---------------------------- local functions ---------------------------- */

static void __place_get_placement_flags(
	placement_flags_t *ret_flags, FvwmWindow *fw, style_flags *sflags,
	initial_window_options_t *win_opts, int mode,
	placement_reason_t *reason)
{
	Bool override_ppos;
	Bool override_uspos;
	Bool has_ppos = False;
	Bool has_uspos = False;

	/* Windows use the position hint if these conditions are met:
	 *
	 *  The program specified a USPosition hint and it is not overridden
	 *  with the No(Transient)USPosition style.
	 *
	 * OR
	 *
	 *  The program specified a PPosition hint and it is not overridden
	 *  with the No(Transient)PPosition style.
	 *
	 * Windows without a position hint are placed using wm placement.
	 */
	if (IS_TRANSIENT(fw))
	{
		override_ppos = SUSE_NO_TRANSIENT_PPOSITION(sflags);
		override_uspos = SUSE_NO_TRANSIENT_USPOSITION(sflags);
	}
	else
	{
		override_ppos = SUSE_NO_PPOSITION(sflags);
		override_uspos = SUSE_NO_USPOSITION(sflags);
	}
	if (fw->hints.flags & PPosition)
	{
		if (!override_ppos)
		{
			has_ppos = True;
			reason->pos.reason = PR_POS_USE_PPOS;
		}
		else
		{
			reason->pos.reason = PR_POS_IGNORE_PPOS;
		}
	}
	if (fw->hints.flags & USPosition)
	{
		if (!override_uspos)
		{
			has_uspos = True;
			reason->pos.reason = PR_POS_USE_USPOS;
		}
		else if (reason->pos.reason != PR_POS_USE_PPOS)
		{
			reason->pos.reason = PR_POS_USE_USPOS;
		}
	}
	if (mode == PLACE_AGAIN)
	{
		ret_flags->do_not_use_wm_placement = False;
		reason->pos.reason = PR_POS_PLACE_AGAIN;
	}
	else if (has_ppos || has_uspos)
	{
		ret_flags->do_not_use_wm_placement = True;
	}
	else if (win_opts->flags.do_override_ppos)
	{
		ret_flags->do_not_use_wm_placement = True;
		reason->pos.reason = PR_POS_CAPTURE;
	}
	else if (!ret_flags->do_honor_starts_on_page &&
		 fw->wmhints && (fw->wmhints->flags & StateHint) &&
		 fw->wmhints->initial_state == IconicState)
	{
		ret_flags->do_forbid_manual_placement = True;
		reason->pos.do_not_manual_icon_placement = 1;
	}

	return;
}

static Bool __place_get_wm_pos(
	const exec_context_t *exc, style_flags *sflags, rectangle *attr_g,
	placement_flags_t flags, rectangle screen_g,
	placement_start_style_t start_style, int mode,
	initial_window_options_t *win_opts, placement_reason_t *reason,
	int pdeltax, int pdeltay)
{
	unsigned int placement_mode = SPLACEMENT_MODE(sflags);
	FvwmWindow *fw = exc->w.fw;
	Bool rc;
	int DragWidth;
	int DragHeight;
	size_borders b;
	int PageLeft;
	int PageTop;
	int PageRight;
	int PageBottom;
	int xl;
	int yt;

	rc = False;
	PageLeft   = screen_g.x - pdeltax;
	PageTop    = screen_g.y - pdeltay;
	PageRight  = PageLeft + screen_g.width;
	PageBottom = PageTop + screen_g.height;
	xl = -1;
	yt = PageTop;
	/* override if Manual placement happen */
	SET_PLACED_BY_FVWM(fw, True);
	if (flags.do_forbid_manual_placement)
	{
		switch (placement_mode)
		{
		case PLACE_MANUAL:
		case PLACE_MANUAL_B:
			placement_mode = PLACE_CASCADE;
			break;
		case PLACE_TILEMANUAL:
			placement_mode = PLACE_TILECASCADE;
			break;
		default:
			break;
		}
	}
	/* first, try various "smart" placement */
	reason->pos.algo = placement_mode;
	switch (placement_mode)
	{
	case PLACE_CENTER:
		attr_g->x = (screen_g.width - fw->g.frame.width) / 2;
		attr_g->y = ((screen_g.height - fw->g.frame.height) / 2);
		/* Don't let the upper left corner be offscreen. */
		if (attr_g->x < PageLeft)
		{
			attr_g->x = PageLeft;
		}
		if (attr_g->y < PageTop)
		{
			attr_g->y = PageTop;
		}
		break;
	case PLACE_TILEMANUAL:
		flags.is_smartly_placed = SmartPlacement(
			fw, &screen_g, fw->g.frame.width, fw->g.frame.height,
			&xl, &yt, pdeltax, pdeltay);
		if (flags.is_smartly_placed)
		{
			break;
		}
		reason->pos.has_tile_failed = 1;
		/* fall through to manual placement */
	case PLACE_MANUAL:
	case PLACE_MANUAL_B:
		/* either "smart" placement fail and the second
		 * choice is a manual placement (TileManual) or we
		 * have a manual placement in any case (Manual) */
		xl = 0;
		yt = 0;
		if (GrabEm(CRS_POSITION, GRAB_NORMAL))
		{
			int mx;
			int my;

			/* Grabbed the pointer - continue */
			MyXGrabServer(dpy);
			if (XGetGeometry(
				    dpy, FW_W(fw), &JunkRoot, &JunkX, &JunkY,
				    &DragWidth, &DragHeight, &JunkBW,
				    &JunkDepth) == 0)
			{
				MyXUngrabServer(dpy);
				UngrabEm(GRAB_NORMAL);
				return False;
			}
			SET_PLACED_BY_FVWM(fw,False);
			MyXGrabKeyboard(dpy);
			DragWidth = fw->g.frame.width;
			DragHeight = fw->g.frame.height;

			if (Scr.SizeWindow != None)
			{
				XMapRaised(dpy, Scr.SizeWindow);
			}
			FScreenGetScrRect(
				NULL, FSCREEN_GLOBAL, &mx, &my, NULL, NULL);
			if (__move_loop(
				    exc, mx, my, DragWidth, DragHeight, &xl,
				    &yt, False))
			{
				/* resize too */
				rc = True;
			}
			else
			{
				rc = False;
			}
			if (Scr.SizeWindow != None)
			{
				XUnmapWindow(dpy, Scr.SizeWindow);
			}
			MyXUngrabKeyboard(dpy);
			MyXUngrabServer(dpy);
			UngrabEm(GRAB_NORMAL);
		}
		else
		{
			/* couldn't grab the pointer - better do something */
			XBell(dpy, 0);
			xl = 0;
			yt = 0;
			reason->pos.has_manual_failed = 1;
		}
		if (flags.do_honor_starts_on_page)
		{
			xl -= pdeltax;
			yt -= pdeltay;
		}
		attr_g->y = yt;
		attr_g->x = xl;
		break;
	case PLACE_MINOVERLAPPERCENT:
		CleverPlacement(
			fw, sflags, &screen_g, &xl, &yt, pdeltax, pdeltay, 1);
		flags.is_smartly_placed = True;
		break;
	case PLACE_TILECASCADE:
		flags.is_smartly_placed = SmartPlacement(
			fw, &screen_g, fw->g.frame.width, fw->g.frame.height,
			&xl, &yt, pdeltax, pdeltay);
		if (flags.is_smartly_placed)
		{
			break;
		}
		reason->pos.has_tile_failed = 1;
		/* fall through to cascade placement */
	case PLACE_CASCADE:
	case PLACE_CASCADE_B:
		/* either "smart" placement fail and the second choice is
		 * "cascade" placement (TileCascade) or we have a "cascade"
		 * placement in any case (Cascade) or we have a crazy
		 * SPLACEMENT_MODE(sflags) value set with the old Style
		 * Dumb/Smart, Random/Active, Smart/SmartOff (i.e.:
		 * Dumb+Random+Smart or Dumb+Active+Smart) */
		if (Scr.cascade_window != NULL)
		{
			Scr.cascade_x += fw->title_thickness;
			Scr.cascade_y += 2 * fw->title_thickness;
		}
		Scr.cascade_window = fw;
		if (Scr.cascade_x > screen_g.width / 2)
		{
			Scr.cascade_x = fw->title_thickness;
		}
		if (Scr.cascade_y > screen_g.height / 2)
		{
			Scr.cascade_y = 2 * fw->title_thickness;
		}
		attr_g->x = Scr.cascade_x + PageLeft;
		attr_g->y = Scr.cascade_y + PageTop;
		/* try to keep the window on the screen */
		get_window_borders(fw, &b);
		if (attr_g->x + fw->g.frame.width >= PageRight)
		{
			attr_g->x = PageRight - attr_g->width -
				b.total_size.width;
			Scr.cascade_x = fw->title_thickness;
		}
		if (attr_g->y + fw->g.frame.height >= PageBottom)
		{
			attr_g->y = PageBottom - attr_g->height -
				b.total_size.height;
			Scr.cascade_y = 2 * fw->title_thickness;
		}

		/* the left and top sides are more important in huge
		 * windows */
		if (attr_g->x < PageLeft)
		{
			attr_g->x = PageLeft;
		}
		if (attr_g->y < PageTop)
		{
			attr_g->y = PageTop;
		}
		break;
	case PLACE_MINOVERLAP:
		CleverPlacement(
			fw, sflags, &screen_g, &xl, &yt, pdeltax, pdeltay, 0);
		flags.is_smartly_placed = True;
		break;
	case PLACE_UNDERMOUSE:
	{
		int mx;
		int my;

		if (
			FQueryPointer(
				dpy, Scr.Root, &JunkRoot, &JunkChild, &mx, &my,
				&JunkX, &JunkY, &JunkMask) == False)
		{
			/* pointer is on a different screen */
			xl = 0;
			yt = 0;
		}
		else
		{
			xl = mx - (fw->g.frame.width / 2);
			yt = my - (fw->g.frame.height / 2);
			if (
				xl + fw->g.frame.width >
				screen_g.x + screen_g.width)
			{
				xl = screen_g.x + screen_g.width -
					fw->g.frame.width;
			}
			if (
				yt + fw->g.frame.height >
				screen_g.y + screen_g.height)
			{
				yt = screen_g.y + screen_g.height -
					fw->g.frame.height;
			}
			if (xl < screen_g.x)
			{
				xl = screen_g.x;
			}
			if (yt < screen_g.y)
			{
				yt = screen_g.y;
			}
		}
		attr_g->x = xl;
		attr_g->y = yt;
		break;
	}
	default:
		/* can't happen */
		break;
	}
	if (flags.is_smartly_placed)
	{
		/* "smart" placement succed, we have done ... */
		attr_g->x = xl;
		attr_g->y = yt;
	}

	return rc;
}

static Bool __place_get_nowm_pos(
	const exec_context_t *exc, style_flags *sflags, rectangle *attr_g,
	placement_flags_t flags, rectangle screen_g,
	placement_start_style_t start_style, int mode,
	initial_window_options_t *win_opts, placement_reason_t *reason,
	int pdeltax, int pdeltay)
{
	FvwmWindow *fw = exc->w.fw;
	size_borders b;

	if (!win_opts->flags.do_override_ppos)
	{
		SET_PLACED_BY_FVWM(fw, False);
	}
	/* the USPosition was specified, or the window is a transient, or it
	 * starts iconic so place it automatically */
	if (SUSE_START_ON_SCREEN(sflags) && flags.do_honor_starts_on_screen)
	{
		fscreen_scr_t mangle_screen;

		/* If StartsOnScreen has been given for a window, translate its
		 * USPosition so that it is relative to that particular screen.
		 *  If we don't do this, then a geometry would completely
		 * cancel the effect of the StartsOnScreen style. However, some
		 * applications try to remember their position.  This would
		 * break if these were translated to screen coordinates.  There
		 * is no reliable way to do it.  Currently, if the desired
		 * place does not intersect the target screen, we assume the
		 * window position must be adjusted to the screen origin. So
		 * there are two ways to get a window to pop up on a particular
		 * Xinerama screen.  1: The intuitive way giving a geometry
		 * hint relative to the desired screen's 0,0 along with the
		 * appropriate StartsOnScreen style (or *wmscreen resource), or
		 * 2: Do NOT specify a Xinerama screen (or specify it to be
		 * 'g') and give the geometry hint in terms of the global
		 * screen. */
		mangle_screen = FScreenFetchMangledScreenFromUSPosHints(
			&(fw->hints));
		if (mangle_screen != FSCREEN_GLOBAL)
		{
			/* whoever set this hint knew exactly what he was
			 * doing; so ignore the StartsOnScreen style */
			flags.do_honor_starts_on_screen = 0;
			reason->pos.reason = PR_POS_USPOS_OVERRIDE_SOS;
		}
		else if (attr_g->x + attr_g->width < screen_g.x ||
			 attr_g->x >= screen_g.x + screen_g.width ||
			 attr_g->y + attr_g->height < screen_g.y ||
			 attr_g->y >= screen_g.y + screen_g.height)
		{
			/* desired coordinates do not intersect the target
			 * screen.  Let's assume the application specified
			 * global coordinates and translate them to the screen.
			 */
			FScreenTranslateCoordinates(
				NULL, start_style.screen, NULL, FSCREEN_GLOBAL,
				&attr_g->x, &attr_g->y);
			reason->pos.do_adjust_off_screen = 1;
		}
	}
	/* If SkipMapping, and other legalities are observed, adjust for
	 * StartsOnPage. */
	if (DO_NOT_SHOW_ON_MAP(fw) && flags.do_honor_starts_on_page &&
	    (!IS_TRANSIENT(fw) || SUSE_START_ON_PAGE_FOR_TRANSIENT(sflags))
#if 0
	    /* dv 08-Jul-2003:  Do not use this.  Instead, force the window on
	     * the requested page even if the application requested a different
	     * position. */
	    && (SUSE_NO_PPOSITION(sflags) || !(fw->hints.flags & PPosition))
	    /* dv 08-Jul-2003:  This condition is always true because we
	     * already checked for flags.do_honor_starts_on_page above. */
	    /*  RBW - allow StartsOnPage to go through, even if iconic. */
	    && ((!(fw->wmhints && (fw->wmhints->flags & StateHint) &&
		   fw->wmhints->initial_state == IconicState))
		|| flags.do_honor_starts_on_page)
#endif
		)
	{
		int old_x;
		int old_y;

		old_x = attr_g->x;
		old_y = attr_g->y;
		/* We're placing a SkipMapping window - either capturing one
		 * that's previously been mapped, or overriding USPosition - so
		 * what we have here is its actual untouched coordinates. In
		 * case it was a StartsOnPage window, we have to 1) convert the
		 * existing x,y offsets relative to the requested page (i.e.,
		 * as though there were only one page, no virtual desktop),
		 * then 2) readjust relative to the current page. */
		if (attr_g->x < 0)
		{
			attr_g->x += Scr.MyDisplayWidth;
		}
		attr_g->x %= Scr.MyDisplayWidth;
		attr_g->x -= pdeltax;
		/* Noticed a quirk here. With some apps (e.g., xman), we find
		 * the placement has moved 1 pixel away from where we
		 * originally put it when we come through here.  Why is this
		 * happening? Probably attr_backup.border_width, try xclock
		 * -borderwidth 100 */
		if (attr_g->y < 0)
		{
			attr_g->y += Scr.MyDisplayHeight;
		}
		attr_g->y %= Scr.MyDisplayHeight;
		attr_g->y -= pdeltay;
		if (attr_g->x != old_x || attr_g->y != old_y)
		{
			reason->pos.do_adjust_off_page = 1;
		}
	}
	/* put it where asked, mod title bar */
	/* if the gravity is towards the top, move it by the title height */
	{
		rectangle final_g;
		int gravx;
		int gravy;

		gravity_get_offsets(fw->hints.win_gravity, &gravx, &gravy);
		final_g.x = attr_g->x + gravx * fw->attr_backup.border_width;
		final_g.y = attr_g->y + gravy * fw->attr_backup.border_width;
		/* Virtually all applications seem to share a common bug: they
		 * request the top left pixel of their *border* as their origin
		 * instead of the top left pixel of their client window, e.g.
		 * 'xterm -g +0+0' creates an xterm that tries to map at (0 0)
		 * although its border (width 1) would not be visible if it ran
		 * under plain X.  It should have tried to map at (1 1)
		 * instead.  This clearly violates the ICCCM, but trying to
		 * change this is like tilting at windmills.  So we have to add
		 * the border width here. */
		final_g.x += fw->attr_backup.border_width;
		final_g.y += fw->attr_backup.border_width;
		final_g.width = 0;
		final_g.height = 0;
		if (mode == PLACE_INITIAL)
		{
			get_window_borders(fw, &b);
			gravity_resize(
				fw->hints.win_gravity, &final_g,
				b.total_size.width, b.total_size.height);
		}
		attr_g->x = final_g.x;
		attr_g->y = final_g.y;
	}

	return False;
}

/* Handles initial placement and sizing of a new window
 *
 * Return value:
 *
 *   0 = window lost
 *   1 = OK
 *   2 = OK, window must be resized too */
static Bool __place_window(
	const exec_context_t *exc, style_flags *sflags, rectangle *attr_g,
	placement_start_style_t start_style, int mode,
	initial_window_options_t *win_opts, placement_reason_t *reason)
{
	FvwmWindow *t;
	int px = 0;
	int py = 0;
	int pdeltax = 0;
	int pdeltay = 0;
	rectangle screen_g;
	Bool rc = False;
	placement_flags_t flags;
	extern Bool Restarting;
	FvwmWindow *fw = exc->w.fw;

	memset(&flags, 0, sizeof(flags));

	/* Select a desk to put the window on (in list of priority):
	 * 1. Sticky Windows stay on the current desk.
	 * 2. Windows specified with StartsOnDesk go where specified
	 * 3. Put it on the desk it was on before the restart.
	 * 4. Transients go on the same desk as their parents.
	 * 5. Window groups stay together (if the KeepWindowGroupsOnDesk style
	 * is used). */

	/* Let's get the StartsOnDesk/Page tests out of the way first. */
	if (SUSE_START_ON_DESK(sflags) || SUSE_START_ON_SCREEN(sflags))
	{
		flags.do_honor_starts_on_page = 1;
		flags.do_honor_starts_on_screen = 1;
		/*
		 * Honor the flag unless...
		 * it's a restart or recapture, and that option's disallowed...
		 */
		if (win_opts->flags.do_override_ppos &&
		    (Restarting || (Scr.flags.are_windows_captured)) &&
		    !SRECAPTURE_HONORS_STARTS_ON_PAGE(sflags))
		{
			flags.do_honor_starts_on_page = 0;
			flags.do_honor_starts_on_screen = 0;
			reason->page.reason = PR_PAGE_IGNORE_CAPTURE;
			reason->page.do_ignore_starts_on_page = 1;
			reason->screen.reason = PR_PAGE_IGNORE_CAPTURE;
		}
		/*
		 * it's a cold start window capture, and that's disallowed...
		 */
		if (win_opts->flags.do_override_ppos &&
		    (!Restarting && !(Scr.flags.are_windows_captured)) &&
		    !SCAPTURE_HONORS_STARTS_ON_PAGE(sflags))
		{
			flags.do_honor_starts_on_page = 0;
			flags.do_honor_starts_on_screen = 0;
			reason->page.reason = PR_PAGE_IGNORE_CAPTURE;
			reason->page.do_ignore_starts_on_page = 1;
			reason->screen.reason = PR_PAGE_IGNORE_CAPTURE;
		}
		/*
		 * it's ActivePlacement and SkipMapping, and that's disallowed.
		 */
		if (!win_opts->flags.do_override_ppos &&
		    (DO_NOT_SHOW_ON_MAP(fw) &&
		     (SPLACEMENT_MODE(sflags) == PLACE_MANUAL ||
		      SPLACEMENT_MODE(sflags) == PLACE_MANUAL_B ||
		      SPLACEMENT_MODE(sflags) == PLACE_TILEMANUAL) &&
		     !SMANUAL_PLACEMENT_HONORS_STARTS_ON_PAGE(sflags)))
		{
			flags.do_honor_starts_on_page = 0;
			reason->page.reason = PR_PAGE_IGNORE_INVALID;
			reason->page.do_ignore_starts_on_page = 1;
			fvwm_msg(
				WARN, "__place_window",
				"invalid style combination used: StartsOnPage"
				"/StartsOnDesk and SkipMapping don't work with"
				" ManualPlacement and TileManualPlacement."
				" Putting window on current page, please use"
				" another placement style or"
				" ActivePlacementHonorsStartsOnPage.");
		}
	}
	/* get the screen coordinates to place window on */
	if (SUSE_START_ON_SCREEN(sflags))
	{
		if (flags.do_honor_starts_on_screen)
		{
			/* use screen from style */
			FScreenGetScrRect(
				NULL, start_style.screen, &screen_g.x,
				&screen_g.y, &screen_g.width, &screen_g.height);
			reason->screen.screen = start_style.screen;
		}
		else
		{
			/* use global screen */
			FScreenGetScrRect(
				NULL, FSCREEN_GLOBAL, &screen_g.x, &screen_g.y,
				&screen_g.width, &screen_g.height);
			reason->screen.screen = FSCREEN_GLOBAL;
		}
	}
	else
	{
		/* use current screen */
		FScreenGetScrRect(
			NULL, FSCREEN_CURRENT, &screen_g.x, &screen_g.y,
			&screen_g.width, &screen_g.height);
		reason->screen.screen = FSCREEN_CURRENT;
	}

	if (SPLACEMENT_MODE(sflags) != PLACE_MINOVERLAPPERCENT &&
	    SPLACEMENT_MODE(sflags) != PLACE_MINOVERLAP)
	{
		EWMH_GetWorkAreaIntersection(
			fw, &screen_g.x, &screen_g.y, &screen_g.width,
			&screen_g.height, SEWMH_PLACEMENT_MODE(sflags));
		reason->screen.was_modified_by_ewmh_workingarea = 1;
	}
	reason->screen.g = screen_g;
	/* Don't alter the existing desk location during Capture/Recapture.  */
	if (!win_opts->flags.do_override_ppos)
	{
		fw->Desk = Scr.CurrentDesk;
		reason->desk.reason = PR_DESK_CURRENT;
	}
	else
	{
		reason->desk.reason = PR_DESK_CAPTURE;
	}
	if (S_IS_STICKY_ACROSS_DESKS(SFC(*sflags)))
	{
		fw->Desk = Scr.CurrentDesk;
		reason->desk.reason = PR_DESK_STICKY;
	}
	else if (SUSE_START_ON_DESK(sflags) && start_style.desk &&
		 flags.do_honor_starts_on_page)
	{
		fw->Desk = (start_style.desk > -1) ?
			start_style.desk - 1 : start_style.desk;
		reason->desk.reason = reason->desk.sod_reason;
	}
	else
	{
		if ((DO_USE_WINDOW_GROUP_HINT(fw)) &&
		    (fw->wmhints) && (fw->wmhints->flags & WindowGroupHint)&&
		    (fw->wmhints->window_group != None) &&
		    (fw->wmhints->window_group != Scr.Root))
		{
			/* Try to find the group leader or another window in
			 * the group */
			for (t = Scr.FvwmRoot.next; t != NULL; t = t->next)
			{
				if (FW_W(t) == fw->wmhints->window_group)
				{
					/* found the group leader, break out */
					fw->Desk = t->Desk;
					reason->desk.reason =
						PR_DESK_WINDOW_GROUP_LEADER;
					break;
				}
				else if (t->wmhints &&
					 (t->wmhints->flags &
					  WindowGroupHint) &&
					 (t->wmhints->window_group ==
					  fw->wmhints->window_group))
				{
					/* found a window from the same group,
					 * but keep looking for the group
					 * leader */
					fw->Desk = t->Desk;
					reason->desk.reason =
						PR_DESK_WINDOW_GROUP_MEMBER;
				}
			}
		}
		if ((IS_TRANSIENT(fw))&&(FW_W_TRANSIENTFOR(fw)!=None)&&
		    (FW_W_TRANSIENTFOR(fw) != Scr.Root))
		{
			/* Try to find the parent's desktop */
			for (t = Scr.FvwmRoot.next; t != NULL; t = t->next)
			{
				if (FW_W(t) == FW_W_TRANSIENTFOR(fw))
				{
					fw->Desk = t->Desk;
					reason->desk.reason = PR_DESK_TRANSIENT;
					break;
				}
			}
		}

		{
			/* migo - I am not sure this block is ever needed */

			Atom atype;
			int aformat;
			unsigned long nitems, bytes_remain;
			unsigned char *prop;

			if (
				XGetWindowProperty(
					dpy, FW_W(fw), _XA_WM_DESKTOP, 0L, 1L,
					True, _XA_WM_DESKTOP, &atype, &aformat,
					&nitems, &bytes_remain, &prop) ==
				Success)
			{
				if (prop != NULL)
				{
					fw->Desk = *(unsigned long *)prop;
					XFree(prop);
					reason->desk.reason =
						PR_DESK_XPROP_XA_WM_DESKTOP;
				}
			}
		}
	}
	reason->desk.desk = fw->Desk;
	/* I think it would be good to switch to the selected desk
	 * whenever a new window pops up, except during initialization */
	/*  RBW - 11/02/1998  --  I dont. */
	if (!win_opts->flags.do_override_ppos && !DO_NOT_SHOW_ON_MAP(fw))
	{
		if (Scr.CurrentDesk != fw->Desk)
		{
			reason->desk.do_switch_desk = 1;
		}
		goto_desk(fw->Desk);
	}
	/* Don't move viewport if SkipMapping, or if recapturing the window,
	 * adjust the coordinates later. Otherwise, just switch to the target
	 * page - it's ever so much simpler. */
	if (S_IS_STICKY_ACROSS_PAGES(SFC(*sflags)))
	{
		reason->page.reason = PR_PAGE_STICKY;
	}
	else if (SUSE_START_ON_DESK(sflags))
	{
		if (start_style.page_x != 0 && start_style.page_y != 0)
		{
			px = start_style.page_x - 1;
			py = start_style.page_y - 1;
			reason->page.reason = PR_PAGE_STYLE;
			px *= Scr.MyDisplayWidth;
			py *= Scr.MyDisplayHeight;
			if (!win_opts->flags.do_override_ppos &&
			    !DO_NOT_SHOW_ON_MAP(fw))
			{
				MoveViewport(px,py,True);
				reason->page.do_switch_page = 1;
			}
			else if (flags.do_honor_starts_on_page)
			{
				/*  Save the delta from current page */
				pdeltax = Scr.Vx - px;
				pdeltay = Scr.Vy - py;
				reason->page.do_honor_starts_on_page = 1;
			}
		}
	}

	/* pick a location for the window. */
	__place_get_placement_flags(&flags, fw, sflags, win_opts, mode, reason);
	if (flags.do_not_use_wm_placement)
	{
		rc = __place_get_nowm_pos(
			exc, sflags, attr_g, flags, screen_g, start_style,
			mode, win_opts, reason, pdeltax, pdeltay);
	}
	else
	{
		rc = __place_get_wm_pos(
			exc, sflags, attr_g, flags, screen_g, start_style,
			mode, win_opts, reason, pdeltax, pdeltay);
	}
	reason->pos.x = attr_g->x;
	reason->pos.y = attr_g->y;

	return rc;
}

static void __place_handle_x_resources(
	FvwmWindow *fw, window_style *pstyle, placement_reason_t *reason)
{
	int client_argc = 0;
	char **client_argv = NULL;
	XrmValue rm_value;
	/* Used to parse command line of clients for specific desk requests. */
	/* Todo: check for multiple desks. */
	XrmDatabase db = NULL;
	static XrmOptionDescRec table [] = {
		/* Want to accept "-workspace N" or -xrm "fvwm*desk:N" as
		 * options to specify the desktop. I have to include dummy
		 * options that are meaningless since Xrm seems to allow -w to
		 * match -workspace if there would be no ambiguity. */
		{"-workspacf", "*junk", XrmoptionSepArg, (caddr_t) NULL},
		{"-workspace", "*desk", XrmoptionSepArg, (caddr_t) NULL},
		{"-xrn", NULL, XrmoptionResArg, (caddr_t) NULL},
		{"-xrm", NULL, XrmoptionResArg, (caddr_t) NULL},
	};
	int t1 = -1, t2 = -1, t3 = -1, spargs = 0;

	/* Find out if the client requested a specific desk on the command
	 * line.
	 *  RBW - 11/20/1998 - allow a desk of -1 to work.  */
	if (XGetCommand(dpy, FW_W(fw), &client_argv, &client_argc) == 0)
	{
		return;
	}
	if (client_argc <= 0 || client_argv == NULL)
	{
		return;
	}
	/* Get global X resources */
	MergeXResources(dpy, &db, False);
	/* command line takes precedence over all */
	MergeCmdLineResources(
		&db, table, 4, client_argv[0], &client_argc, client_argv, True);
	/* parse the database values */
	if (GetResourceString(db, "desk", client_argv[0], &rm_value) &&
	    rm_value.size != 0)
	{
		SGET_START_DESK(*pstyle) = atoi(rm_value.addr);
		/*  RBW - 11/20/1998  */
		if (SGET_START_DESK(*pstyle) > -1)
		{
			SSET_START_DESK(
				*pstyle, SGET_START_DESK(*pstyle) + 1);
		}
		reason->desk.sod_reason = PR_DESK_X_RESOURCE_DESK;
		pstyle->flags.use_start_on_desk = 1;
	}
	if (GetResourceString(db, "fvwmscreen", client_argv[0], &rm_value) &&
	    rm_value.size != 0)
	{
		SSET_START_SCREEN(
			*pstyle, FScreenGetScreenArgument(rm_value.addr, 'c'));
		reason->screen.reason = PR_SCREEN_X_RESOURCE_FVWMSCREEN;
		reason->screen.screen = SGET_START_SCREEN(*pstyle);
		pstyle->flags.use_start_on_screen = 1;
	}
	if (GetResourceString(db, "page", client_argv[0], &rm_value) &&
	    rm_value.size != 0)
	{
		spargs = sscanf(
			rm_value.addr, "%d %d %d", &t1, &t2, &t3);
		switch (spargs)
		{
		case 1:
			pstyle->flags.use_start_on_desk = 1;
			SSET_START_DESK(*pstyle, (t1 > -1) ? t1 + 1 : t1);
			reason->desk.sod_reason = PR_DESK_X_RESOURCE_PAGE;
			break;
		case 2:
			pstyle->flags.use_start_on_desk = 1;
			SSET_START_PAGE_X(*pstyle, (t1 > -1) ? t1 + 1 : t1);
			SSET_START_PAGE_Y(*pstyle, (t2 > -1) ? t2 + 1 : t2);
			reason->page.reason = PR_PAGE_X_RESOURCE_PAGE;
			reason->page.px = SGET_START_PAGE_X(*pstyle);
			reason->page.py = SGET_START_PAGE_Y(*pstyle);
			break;
		case 3:
			pstyle->flags.use_start_on_desk = 1;
			SSET_START_DESK(*pstyle, (t1 > -1) ? t1 + 1 : t1);
			reason->desk.sod_reason =
				PR_DESK_X_RESOURCE_PAGE;
			SSET_START_PAGE_X(*pstyle, (t2 > -1) ? t2 + 1 : t2);
			SSET_START_PAGE_Y(*pstyle, (t3 > -1) ? t3 + 1 : t3);
			reason->page.reason = PR_PAGE_X_RESOURCE_PAGE;
			reason->page.px = SGET_START_PAGE_X(*pstyle);
			reason->page.py = SGET_START_PAGE_Y(*pstyle);
			break;
		default:
			break;
		}
	}
	XFreeStringList(client_argv);
	XrmDestroyDatabase(db);

	return;
}

static void __explain_placement(FvwmWindow *fw, placement_reason_t *reason)
{
	char explanation[2048];
	char *r;
	char *s;
	char t[32];
	int do_show_page;
	int is_placed_by_algo;

	*explanation = 0;
	s = explanation;
	strcat(s, "placed new window 0x%x '%s':\n");
	s += strlen(s);
	sprintf(
		s, "  initial size %dx%d\n", fw->g.frame.width,
		fw->g.frame.height);
	s += strlen(s);
	switch (reason->desk.reason)
	{
	case PR_DESK_CURRENT:
		r = "current desk";
		break;
	case PR_DESK_STYLE:
		r = "specified by style";
		break;
	case PR_DESK_X_RESOURCE_DESK:
		r = "specified by 'desk' X resource";
		break;
	case PR_DESK_X_RESOURCE_PAGE:
		r = "specified by 'page' X resource";
		break;
	case PR_DESK_CAPTURE:
		r = "window was (re)captured";
		break;
	case PR_DESK_STICKY:
		r = "window is sticky";
		break;
	case PR_DESK_WINDOW_GROUP_LEADER:
		r = "same desk as window group leader";
		break;
	case PR_DESK_WINDOW_GROUP_MEMBER:
		r = "same desk as window group member";
		break;
	case PR_DESK_TRANSIENT:
		r = "transient window placed on same desk as parent";
		break;
	case PR_DESK_XPROP_XA_WM_DESKTOP:
		r = "specified by _XA_WM_DESKTOP property";
		break;
	default:
		r = "bug";
		break;
	}
	sprintf(s, "  desk %d (%s)\n", reason->desk.desk, r);
	s += strlen(s);
	if (reason->desk.do_switch_desk == 1)
	{
		sprintf(s, "    (switched to desk)\n");
		s += strlen(s);
	}
	/* page */
	do_show_page = 1;
	switch (reason->page.reason)
	{
	case PR_PAGE_CURRENT:
		do_show_page = 0;
		r = "current page";
		break;
	case PR_PAGE_STYLE:
		r = "specified by style";
		break;
	case PR_PAGE_X_RESOURCE_PAGE:
		r = "specified by 'page' X resource";
		break;
	case PR_PAGE_IGNORE_CAPTURE:
		r = "window was (re)captured";
		break;
	case PR_PAGE_IGNORE_INVALID:
		r = "requested page ignored because of invalid style"
			" combination";
		break;
	case PR_PAGE_STICKY:
		do_show_page = 0;
		r = "current page (window is sticky)";
		break;
	default:
		r = "bug";
		break;
	}
	if (do_show_page == 0)
	{
		sprintf(s, "  %s\n", r);
	}
	else
	{
		sprintf(
			s, "  page %d %d (%s)\n", reason->page.px - 1,
			reason->page.py - 1, r);
	}
	s += strlen(s);
	if (reason->page.do_switch_page == 1)
	{
		sprintf(s, "    (switched to page)\n");
		s += strlen(s);
	}
	if (reason->page.do_ignore_starts_on_page == 1)
	{
		sprintf(s, "    (possibly ignored StartsOnPage)\n");
		s += strlen(s);
	}
	/* screen */
	if (FScreenIsEnabled() == True || FScreenIsSLSEnabled() == True)
	{
		switch (reason->screen.reason)
		{
		case PR_SCREEN_CURRENT:
			r = "current screen";
			break;
		case PR_SCREEN_STYLE:
			r = "specified by style";
			break;
		case PR_SCREEN_X_RESOURCE_FVWMSCREEN:
			r = "specified by 'fvwmscreen' X resource";
			break;
		case PR_SCREEN_IGNORE_CAPTURE:
			r = "window was (re)captured";
			break;
		default:
			r = "bug";
			break;
		}
		FScreenSpecToString(t, 32, reason->screen.screen);
		sprintf(
			s, "  screen: %s: %d %d %dx%d (%s)\n",
			t, reason->screen.g.x, reason->screen.g.y,
			reason->screen.g.width, reason->screen.g.height, r);
		s += strlen(s);
		if (reason->screen.was_modified_by_ewmh_workingarea == 1)
		{
			sprintf(
				s, "    (screen area modified by EWMH working"
				" area)\n");
			s += strlen(s);
		}
	}
	/* position */
	is_placed_by_algo = 0;
	switch (reason->pos.reason)
	{
	case PR_POS_NORMAL:
		is_placed_by_algo = 1;
		r = "normal placement";
		break;
	case PR_POS_IGNORE_PPOS:
		is_placed_by_algo = 1;
		r = "ignored program specified position";
		break;
	case PR_POS_USE_PPOS:
		r = "used program specified position";
		break;
	case PR_POS_IGNORE_USPOS:
		is_placed_by_algo = 1;
		r = "ignored user specified position";
		break;
	case PR_POS_USE_USPOS:
		r = "used user specified position";
		break;
	case PR_POS_PLACE_AGAIN:
		is_placed_by_algo = 1;
		r = "by PlaceAgain command";
		break;
	case PR_POS_CAPTURE:
		r = "window was (re)captured";
		break;
	case PR_POS_USPOS_OVERRIDE_SOS:
		r = "StartsOnPage style overridden by application via USPos";
		break;
	default:
		r = "bug";
		break;
	}
	sprintf(s, "  position %d %d", reason->pos.x, reason->pos.y);
	s += strlen(s);
	if (is_placed_by_algo == 1)
	{
		char *a;

		switch (reason->pos.algo)
		{
		case PLACE_CENTER:
			a = "Center";
			break;
		case PLACE_TILEMANUAL:
			a = "TileManual";
			break;
		case PLACE_MANUAL:
		case PLACE_MANUAL_B:
			a = "Manual";
			break;
		case PLACE_MINOVERLAPPERCENT:
			a = "MinOverlapPercent";
			break;
		case PLACE_TILECASCADE:
			a = "TileCascade";
			break;
		case PLACE_CASCADE:
		case PLACE_CASCADE_B:
			a = "Cascade";
			break;
		case PLACE_MINOVERLAP:
			a = "MinOverlap";
			break;
		case PLACE_UNDERMOUSE:
			a = "UnderMouse";
			break;
		default:
			a = "bug";
			break;
		}
		sprintf(s, ", placed by fvwm (%s)\n", r);
		s += strlen(s);
		sprintf(s, "    placement method: %s\n", a);
		s += strlen(s);
		if (reason->pos.do_not_manual_icon_placement == 1)
		{
			sprintf(s, "    (icon not placed manually)\n");
			s += strlen(s);
		}
		if (reason->pos.has_tile_failed == 1)
		{
			sprintf(s, "    (tile placement failed)\n");
			s += strlen(s);
		}
		if (reason->pos.has_manual_failed == 1)
		{
			sprintf(s, "    (manual placement failed)\n");
			s += strlen(s);
		}
	}
	else
	{
		sprintf(s, "  (%s)\n", r);
		s += strlen(s);
	}
	if (reason->pos.do_adjust_off_screen == 1)
	{
		sprintf(s, "    (adjusted to force window on screen)\n");
		s += strlen(s);
	}
	if (reason->pos.do_adjust_off_page == 1)
	{
		sprintf(s, "    (adjusted to force window on page)\n");
		s += strlen(s);
	}
	fvwm_msg(
		INFO, "__explain_placement", explanation, (int)FW_W(fw),
		fw->name.name);

	return;
}

/* ---------------------------- interface functions ------------------------ */

Bool setup_window_placement(
	FvwmWindow *fw, window_style *pstyle, rectangle *attr_g,
	initial_window_options_t *win_opts, placement_mode_t mode)
{
	Bool rc;
	const exec_context_t *exc;
	exec_context_changes_t ecc;
	placement_reason_t reason;
	placement_start_style_t start_style;

	memset(&reason, 0, sizeof(reason));
	if (pstyle->flags.use_start_on_desk)
	{
		reason.desk.sod_reason = PR_DESK_STYLE;
		reason.page.px = SGET_START_PAGE_X(*pstyle);
		reason.page.py = SGET_START_PAGE_Y(*pstyle);
	}
	if (pstyle->flags.use_start_on_screen)
	{
		reason.screen.reason = PR_SCREEN_STYLE;
		reason.screen.screen = SGET_START_SCREEN(*pstyle);
	}
	__place_handle_x_resources(fw, pstyle, &reason);
	if (pstyle->flags.do_start_iconic)
	{
		win_opts->initial_state = IconicState;
	}
	ecc.type = EXCT_NULL;
	ecc.w.fw = fw;
	exc = exc_create_context(&ecc, ECC_TYPE | ECC_FW);
	start_style.desk = SGET_START_DESK(*pstyle);
	start_style.page_x = SGET_START_PAGE_X(*pstyle);
	start_style.page_y = SGET_START_PAGE_Y(*pstyle);
	start_style.screen = SGET_START_SCREEN(*pstyle);
	rc = __place_window(
		exc, &pstyle->flags, attr_g, start_style, mode, win_opts,
		&reason);
	exc_destroy_context(exc);
	if (Scr.bo.do_explain_window_placement == 1)
	{
		__explain_placement(fw, &reason);
	}

	return rc;
}

/* ---------------------------- builtin commands --------------------------- */

void CMD_PlaceAgain(F_CMD_ARGS)
{
	int old_desk;
	char *token;
	float noMovement[1] = {1.0};
	float *ppctMovement = noMovement;
	rectangle attr_g;
	XWindowAttributes attr;
	Bool do_move_animated = False;
	Bool do_place_icon = False;
	FvwmWindow * const fw = exc->w.fw;

	if (!XGetWindowAttributes(dpy, FW_W(fw), &attr))
	{
		return;
	}
	while ((token = PeekToken(action, &action)) != NULL)
	{
		if (StrEquals("Anim", token))
		{
			ppctMovement = NULL;
			do_move_animated = True;
		}
		else if (StrEquals("icon", token))
		{
			do_place_icon = True;
		}
	}
	old_desk = fw->Desk;
	if (IS_ICONIFIED(fw) && !do_place_icon)
	{
		return;
	}
	if (IS_ICONIFIED(fw) && do_place_icon)
	{
		rectangle new_g;
		rectangle old_g;

		if (IS_ICON_SUPPRESSED(fw))
		{
			return;
		}
		fw->Desk = Scr.CurrentDesk;
		get_icon_geometry(fw, &old_g);
		SET_ICON_MOVED(fw, 0);
		AutoPlaceIcon(fw, NULL, False);
		get_icon_geometry(fw, &new_g);
		__move_icon(
			fw, new_g.x, new_g.y, old_g.x, old_g.y,
			do_move_animated, False);
	}
	else
	{
		window_style style;
		initial_window_options_t win_opts;

		memset(&win_opts, 0, sizeof(win_opts));
		lookup_style(fw, &style);
		attr_g.x = attr.x;
		attr_g.y = attr.y;
		attr_g.width = attr.width;
		attr_g.height = attr.height;

		setup_window_placement(
			exc->w.fw, &style, &attr_g, &win_opts, PLACE_AGAIN);
		AnimatedMoveFvwmWindow(
			fw, FW_W_FRAME(fw), -1, -1, attr_g.x, attr_g.y, False,
			-1, ppctMovement);
	}
	if (fw->Desk != old_desk)
	{
		int new_desk = fw->Desk;

		fw->Desk = old_desk;
		do_move_window_to_desk(fw, new_desk);
	}

	return;
}
