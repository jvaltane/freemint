/*
 * $Id$
 * 
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                        2004 F.Naumann & O.Skancke
 *
 * A multitasking AES replacement for FreeMiNT
 *
 * This file is part of XaAES.
 *
 * XaAES is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * XaAES is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XaAES; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include WIDGHNAME

#include "xa_wind.h"
#include "xa_global.h"

#include "app_man.h"
#include "c_window.h"
#include "desktop.h"
#include "menuwidg.h"
#include "obtree.h"
#include "rectlist.h"
#include "version.h"
#include "widgets.h"

#include "xa_form.h"
#include "xa_graf.h"
#include "xa_rsrc.h"


/*
 * AES window handling functions
 * I've tried to support some of the AES4 extensions here as well as the plain
 * single tasking GEM ones.
 */
unsigned long
XA_wind_create(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const RECT r = *((const RECT *)&pb->intin[1]);
	struct xa_window *new_window;
	XA_WIND_ATTR kind = (unsigned short)pb->intin[0];

	CONTROL(5,1,0)
	
	if (pb->control[N_INTIN] >= 6)
	{
		kind |= (long)pb->intin[5] << 16;
	}

	if (!client->options.nohide)
		kind |= HIDE;

	DIAG((D_wind, client, "wind_create: kind=%x, tp=%lx",
		pb->intin[0], kind));

	new_window = create_window(lock,
				   send_app_message,
				   NULL,
				   client,
				   false,
				   kind|BACKDROP,
				   created_for_CLIENT,
				   client->options.thinframe,
				   client->options.thinwork,
				   r,
				   &root_window->wa, //&r,
				   NULL);	

	if (new_window)
		/* Return the window handle in intout[0] */
		pb->intout[0] = new_window->handle;
	else
		/* Fail to create window, return -ve number */
		pb->intout[0] = -1;

	/* Allow the kernal to wake up the client - we've done our bit */
	return XAC_DONE;
}

unsigned long
XA_wind_open(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const RECT r = *((const RECT *)&pb->intin[1]);
	struct xa_window *w;

	CONTROL(5,1,0)

	/* Get the window */
	w = get_wind_by_handle(lock, pb->intin[0]);
	if (!w)
	{
		pb->intout[0] = 0;
	}
	else
	{
		if (w->active_widgets & USE_MAX)
		{
			/* for convenience: adjust max */
			if (r.w > w->max.w)
				w->max.w = r.w;
			if (r.h > w->max.h)
				w->max.h = r.h;
		}	
		pb->intout[0] = open_window(lock, w, r);
	}

	return XAC_DONE;
}

unsigned long
XA_wind_close(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_window *w;

	CONTROL(1,1,0)	

	DIAG((D_wind,client,"XA_wind_close"));

	/* Get the window */
	w = get_wind_by_handle(lock, pb->intin[0]);
	if (w == 0)
	{
		DIAGS(("WARNING:wind_close for %s: Invalid window handle %d", c_owner(client), pb->intin[0]));
		pb->intout[0] = 1;
		return XAC_DONE;
	}

	/* Clients can only close their own windows */
	if (w->owner != client)
	{
		DIAGS(("WARNING: %s cannot close window %d (not owner)", c_owner(client), w->handle));
		pb->intout[0] = 1;
		return XAC_DONE;
	}

	close_window(lock, w);
	pb->intout[0] = 1;

	return XAC_DONE;
}

unsigned long
XA_wind_find(enum locks lock, struct xa_client *client, AESPB *pb)
{
	/* Is there a window under the mouse? */
	struct xa_window *w = find_window(lock, pb->intin[0], pb->intin[1]);

	CONTROL(2,1,0)	

	pb->intout[0] = w ? w->handle : 0;

	DIAG((D_wind, client, "wind_find ---> %d", pb->intout[0]));

	return XAC_DONE;
}

void
top_window(enum locks lock, bool domsg, struct xa_window *w, struct xa_window *old_focus, struct xa_client *desk_menu_owner)
{
	//struct xa_window *old_focus;
	struct xa_client *client = w->owner;

	if (!desk_menu_owner)
		desk_menu_owner = w->owner;

	DIAG((D_wind, client, "top_window %d for %s",  w->handle, c_owner(client)));
	/* Ozk: if -1L passed as old_focus pointer, determine here which is ontop
	 *	before we top 'w'. Else check if old_focus really is ontop.
	 *	This is for redrawing the window exterior to indicate now topped
	 *	is being 'de-topped' :)
	 */
	if (old_focus == (void *)-1L)
		old_focus = is_topped(window_list) ? window_list : NULL;
	else if (old_focus && !is_topped(old_focus))
		old_focus = NULL;

	if (old_focus == root_window)
		old_focus = NULL;

	/* Ozk: If owner of window we're about to top is not infront,
	 *	make it in front and swap to its menu.
	 */
	if (!is_infront(w->owner))
	{
		set_active_client(lock, w->owner);
		swap_menu(lock, w->owner, true, false, 0);
	}
	/* Ozk: Now pull the new topped window to top of list..
	 */
	pull_wind_to_top(lock, w);
	graf_mouse(client->mouse, client->mouse_form, client, false);
	/* Ozk: redisplay title of the window previously on top
	 *	to indicate its not topped anymore
	 */
	if (old_focus && !is_topped(old_focus))
	{
		send_iredraw(lock, old_focus, 0, NULL); //display_window(lock, 40, old_focus, NULL);
		if (domsg) send_untop(lock, old_focus);
	}
	/* Ozk: Make sure the window we should top is not the same as
	 *	the window that was ontop entering here, in wich case
	 *	we dont redraw its exterior (already is ontop)
	 */
	if (!old_focus || (old_focus && old_focus != w))
	{
		if (is_topped(w) && w != root_window)
		{
			send_iredraw(lock, w, 0, NULL); //display_window(lock, 41, w, NULL);
			if (domsg) send_ontop(lock);
		}
	}
	/* Ozk: Set mousecursor to whatever shape the owner of the
	 *	window under mousecursor uses...
	 */
	set_winmouse();
}

void
bottom_window(enum locks lock, struct xa_window *w)
{
	bool was_top = (is_topped(w) ? true : false);
	struct xa_window *wl = w->next;

	DIAG((D_wind, w->owner, "bottom_window %d", w->handle));

	if (wl == root_window)
		/* Don't do anything if already bottom */
		return;

	/* Send it to the back */
	send_wind_to_bottom(lock, w);

	DIAG((D_wind, w->owner, " - menu_owner %s, w_list_owner %s",
		t_owner(get_menu()), w_owner(window_list)));

	/* Redisplay titles */
	if (was_top)
		send_iredraw(lock, w, 0, NULL); //display_window(lock, 42, w, NULL);
	
	/* Our window is now right above root_window */
	update_windows_below(lock, &w->r, NULL, wl, w);

	if (was_top)
	{
		/*  send WM_ONTOP to just topped window. */
		//if (is_topped(window_list))
		//	send_ontop(lock);
		send_untop(lock, w);
		if (!is_infront(window_list->owner))
		{
			set_active_client(lock, window_list->owner);
			swap_menu(lock, window_list->owner, true, false, 0);
		}
		if (is_topped(window_list))
		{
			send_iredraw(lock, window_list, 0, NULL); //display_window(lock, 43, window_list, NULL);
			send_ontop(lock);
		}
	}
}

#if GENERATE_DIAGS
static char *
setget(int i)
{
	/* Want to see quickly what app's are doing. */
	static char *setget_names[] =
	{
	"0",
	"WF_KIND(1)",
	"WF_NAME(2)",
	"WF_INFO(3)",
	"WF_WORKXYWH(4)",
	"WF_CURRXYWH(5)",
	"WF_PREVXYWH(6)",
	"WF_FULLXYWH(7)",
	"WF_HSLIDE(8)",
	"WF_VSLIDE(9)",
	"WF_TOP(10)",
	"WF_FIRSTXYWH(11)",
	"WF_NEXTXYWH(12)",
	"WF_FIRSTAREAXYWH(13)",
	"WF_NEWDESK(14)",
	"WF_HSLSIZE(15)",
	"WF_VSLSIZE(16)",
	"WF_SCREEN(17)",
	"WF_COLOR(18)",
	"WF_DCOLOR(19)",
	"WF_OWNER(20)",
	"21",
	"22",
	"23",
	"WF_BEVENT(24)",
	"WF_BOTTOM(25)",
	"WF_ICONIFY(26)",
	"WF_UNICONIFY(27)",
	"WF_UNICONIFYXYWH(28)",
	"29",
	"WF_TOOLBAR(30)",
	"WF_FTOOLBAR(31)",
	"WF_NTOOLBAR(32)",
	"WF_MENU(33)",
	"34","35","36","37","38","39",
	"WF_WHEEL(40)",
	"WF_OPTS(41)",
	"      "
	};

	static char unknown[32];

	if (i < 0 || i >= sizeof(setget_names) / sizeof(*setget_names))
	{
		sprintf(unknown, sizeof(unknown), "%d", i);
		return unknown;
	}
	return setget_names[i];
}
#endif /* GENERATE_DIAGS */

unsigned long
XA_wind_set(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_window *w;
	int wind = pb->intin[0];
	int cmd = pb->intin[1];

	CONTROL(6,1,0)	

	w = get_wind_by_handle(lock, wind);

	DIAG((D_wind, client, "wind_set for %s  w%lx, h%d, %s", c_owner(client),
		w, w ? w->handle : -1, setget(cmd)));

	/* wind 0 is the root window */
	if (!w || wind == 0)
	{
		switch(cmd)
		{
		case WF_TOP:			/* MagiC 5: wind_get(-1, WF_TOP) */
		case WF_NEWDESK:		/* Some functions may be allowed without a real window handle */
		case WF_WHEEL:
		case WF_OPTS:
			break;
		default:
			DIAGS(("WARNING:wind_set for %s: Invalid window handle %d", c_owner(client), w));
			pb->intout[0] = 0;	/* Invalid window handle, return error */
			return XAC_DONE;
		}
	}
	/*
	 * Ozk: These things one app can do to other processes windows
	 */
	else if (w->owner != client && w != root_window)
	{
		switch(cmd)
		{
			case WF_TOP:
			case WF_BOTTOM:
			{
				break;
			}
			default:
			{
				pb->intout[0] = 0;
				return XAC_DONE;
			}
		}
	}
	else if (w->owner != client		/* Clients can only change their own windows */
		 && (w != root_window || cmd != WF_NEWDESK))
	{
		DIAGS(("WARNING: %s cannot change window %d (not owner)", c_owner(client), w->handle));
		pb->intout[0] = 0;		/* Invalid window handle, return error */
		return XAC_DONE;
	}

	pb->intout[0] = 1;

	switch(cmd)
	{
	default:
	{
		pb->intout[0] = 0;
		break;
	}

	/* */
	case WF_WHEEL:
	{
		long o = 0, om = ~(XAWO_WHEEL);
		short mode = -1;
		
		if (pb->intin[2])
		{
			o |= XAWO_WHEEL;
			mode = pb->intin[3];
			if (mode < 0 || mode > MAX_WHLMODE)
				mode = DEF_WHLMODE;
		}
		
		if (wind == 0)
		{
			client->options.wind_opts &= om;
			client->options.wind_opts |= o;
			if (mode != -1)
				client->options.wheel_mode = mode;	
		}
		else if (w)
		{
			w->opts &= om;
			w->opts |= o;
			if (mode != -1)
				w->wheel_mode = mode;
		}
		
		if (mode != -1)
			client->options.app_opts |= XAPP_XT_WF_SLIDE;

		break;
	}

	/* */
	case WF_HSLIDE:
	{
		XA_WIDGET *widg;

		widg = get_widget(w, XAW_HSLIDE);
		if (widg->stuff)
		{
			XA_SLIDER_WIDGET *slw = widg->stuff;
			slw->position = bound_sl(pb->intin[2]);
			if (client->options.app_opts & XAPP_XT_WF_SLIDE)
				slw->rpos = bound_sl(pb->intin[3]);
			display_widget(lock, w, widg);
		}
		break;
	}

	/* */
	case WF_VSLIDE:
	{
		XA_WIDGET *widg;

		widg = get_widget(w, XAW_VSLIDE);
		if (widg->stuff)
		{
			XA_SLIDER_WIDGET *slw = widg->stuff;
			slw->position = bound_sl(pb->intin[2]);
			if (client->options.app_opts & XAPP_XT_WF_SLIDE)
				slw->rpos = bound_sl(pb->intin[3]);
			display_widget(lock, w, widg);
		}
		break;
	}

	/* */
	case WF_HSLSIZE:
	{
		XA_WIDGET *widg;

		widg = get_widget(w, XAW_HSLIDE);
		if (widg->stuff)
		{
			XA_SLIDER_WIDGET *slw = widg->stuff;
			slw->length = bound_sl(pb->intin[2]);
			display_widget(lock, w, widg);
		}
		break;
	}

	/* */
	case WF_VSLSIZE:
	{
		XA_WIDGET *widg;

		widg = get_widget(w, XAW_VSLIDE);
		if (widg->stuff)
		{
			XA_SLIDER_WIDGET *slw = widg->stuff;
			slw->length = bound_sl(pb->intin[2]);
			display_widget(lock, w, widg);
		}
		break;
	}

	/* set window name line */
	case WF_NAME:
	{
		set_window_title(w, *(const char **)(pb->intin+2));
		break;
	}

	/* set window info line */
	case WF_INFO:
	{
		set_window_info(w, *(const char **)(pb->intin+2));
		break;
	}
	/* Move a window, check sizes */
	/* Ozk: We support WF_FULLXYWH and WF_PREVXYWH in XaAES.
	 *
	 */
	case WF_FULLXYWH:
	case WF_PREVXYWH:
	case WF_CURRXYWH:
	{
		bool blit = false;
		bool move = (pb->intin[2] == -1 && pb->intin[3] == -1 &&
			     pb->intin[4] == -1 && pb->intin[5] == -1) ? true : false;
		RECT *ir;
		short mx, my, mw, mh;
		short status = -1, msg = -1;

		if (cmd == WF_PREVXYWH)
		{
			DIAGS(("wind_set: WF_PREVXYWH"));
			ir = (RECT *)&w->pr;
			status = ~XAWS_FULLED;
		}
		else if (cmd == WF_FULLXYWH)
		{
			DIAGS(("wind_set: WF_FULLXYWH"));
			ir = (RECT *)&w->max;
			status = XAWS_FULLED;
		}
		else
		{
			/* A WF_CURRXYWH call with all coords set to -1 makes no sense
			 */
			if (move)
				ir = NULL;
			else
			{
				(const RECT *)ir = (const RECT *)&pb->intin[2];
				move = true;
			}
			DIAGS(("wind_set: WF_CURRXYWH - (%d/%d/%d/%d) blit=%s, ir=%lx",
				(const RECT *)(pb->intin+2), blit?"yes":"no", ir));

			blit = true;
		}

		if (!move)
		{
			/* Not all coords have a value of -1 meaning we should
			 * modify some (or all) of the coordinates..
			 */
			if (ir)
			{
				if (pb->intin[2] != -1)
					ir->x = pb->intin[2];
				if (pb->intin[3] != -1)
					ir->y = pb->intin[3];
				if (pb->intin[4] != -1)
					ir->w = pb->intin[4];
				if (pb->intin[5] != -1)
					ir->h = pb->intin[5];
			}
		}
		else if (ir)
		{
			mx = ir->x;
			my = ir->y;
			mw = ir->w;
			mh = ir->h;
			ir = (RECT *)&w->rc;

			if ( (mw != w->rc.w && (w->opts & XAWO_NOBLITW)) ||
			     (mh != w->rc.h && (w->opts & XAWO_NOBLITH)))
				blit = false;
			
			DIAGS(("wind_set: move to %d/%d/%d/%d for %s",
				mx, my, mw, mh, client->name));

			if (!mh)
			{
				if (!(w->window_status & XAWS_SHADED))
				{
					DIAGS(("wind_set: zero heigh, shading window %d for %s",
						w->handle, client->name));

					status = XAWS_SHADED|XAWS_ZWSHADED;
					msg = WM_SHADED;
				}
				else
				{
					DIAGS(("wind_set: zero heigh, window %d already shaded for %s",
						w->handle, client->name));
				}
				mh = w->sh;
			}
			else
			{
				if (!(w->window_status & XAWS_SHADED) && status == -1)
				{
					if (w->max.w && w->max.h &&
					    mx == w->max.x &&
					    my == w->max.y &&
					    mw == w->max.w &&
					    mh == w->max.h)
					{
						status = XAWS_FULLED;
					}
					else
					{
						if (w->window_status & XAWS_FULLED)
							status = ~XAWS_FULLED;
						/* Ozk: I really, really dont like to do this, but it is
						 *	how N.AES does it; Send a full redraw whenever either
						 *	x,y or w,h both changes.
						 *	We do it slightly different using X1/Y1 & X2/Y2 instead
						 *	so we dont render resizing using left/top border useless!
						 *	Hopefully this will be good enough...
						 */
						if (blit && w->opts & XAWO_FULLREDRAW)
						{
							if (mx != w->rc.x && my != w->rc.y &&
							    mw != w->rc.w && mh != w->rc.h)
								blit = false;
						}
					}
				}
				else if ((w->window_status & XAWS_ZWSHADED) && mh != w->sh)
				{
					DIAGS(("wind_set: window %d for %s shaded - unshade by different height",
						w->handle, client->name));

					status = ~(XAWS_SHADED|XAWS_ZWSHADED);
					msg = WM_UNSHADED;
				}
			}
			if (w->max.w && mw > w->max.w)
				mw = w->max.w;
			if (w->max.w && mh > w->max.h)
				mh = w->max.h;

			DIAGS(("wind_set: WF_CURRXYWH %d/%d/%d/%d, status = %x", *(const RECT *)&pb->intin[2], status));
		
			move_window(lock, w, blit, status, pb->intin[2], pb->intin[3], mw, mh);

			if (msg != -1 && w->send_message)
				w->send_message(lock, w, NULL, AMQ_NORM, QMF_CHKDUP, msg, 0, 0, w->handle, 0,0,0,0);
		}

		if (pb->control[N_INTOUT] >= 5)
		{
			if (!ir)
				ir = (RECT *)&w->rc;
			*(RECT *)(pb->intout + 1) = *ir;
		}

		break;
	}
	/* */
	case WF_BEVENT:
	{
		if (pb->intin[2] & 1)
			w->active_widgets |= NO_TOPPED;
		else
			w->active_widgets &= ~NO_TOPPED;
		break;
	}

	/* Extension, send window to the bottom */
	case WF_BOTTOM:
	{
		if (w != root_window && (w->window_status & (XAWS_OPEN|XAWS_HIDDEN)) == XAWS_OPEN)
			bottom_window(lock, w);
		break;
	}

	/* Top the window */
	case WF_TOP:
	{
		if (w == 0)
		{
			if (wind == -1)
			{
				struct xa_client *target;

				if (pb->intin[2] > 0)
					target = pid2client(pb->intin[2]);
				else
					target = focus_owner();

				app_in_front(lock|winlist, target);
				DIAG((D_wind, NULL, "-1,WF_TOP: Focus to %s", c_owner(target)));
			}
		}
		else if (w != root_window && (w->window_status & (XAWS_OPEN|XAWS_HIDDEN)) == XAWS_OPEN)
		{
			if ( !is_topped(w))
			{
				top_window(lock|winlist, true, w, (void *)-1L, NULL);
			}
		}
		else
			pb->intout[0] = 0;

		break;
	}

	/* Set a new desktop object tree */
	case WF_NEWDESK:
	{
		short obptr[2] = { pb->intin[2], pb->intin[3] };
		OBJECT *ob = *(OBJECT **)&obptr;

		if (ob)
		{
			XA_TREE *wt = obtree_to_wt(client, ob);

			if (!wt || (wt && wt != client->desktop))
			{
				if (!wt)
					wt = new_widget_tree(client, ob);

				if (wt)
				{
					DIAGS(("  desktop for %s to (%d/%d,%d/%d)", 
						c_owner(client), ob->ob_x, ob->ob_y, ob->ob_width, ob->ob_height));

					client->desktop = wt;
					set_desktop(wt);
				}
				else
				{
					struct xa_client *new = find_desktop(lock);
					DIAGS(("  desktop for %s failed!", client->name)); 
					set_desktop(new->desktop);
					client->desktop = NULL;
				}
			}
		}
		else
		{
			if (client->desktop)
			{
				struct xa_client *new;
				/* find a prev app's desktop. */
				client->desktop = NULL;
				new = find_desktop(lock);
				set_desktop(new->desktop);
				DIAGS(("  desktop for %s removed", c_owner(client)));
			}
		}
		break;
	}
	/* Ozk: According to 'AES.TXT' in the english MagiC documentation,
	 *	wind_set() should return error if attempting to (un)iconfify
	 *	and already (un)iconified window. so we check and return error
	 *	accordingly...
	 */
	case WF_ICONIFY:
	{
		if (w->window_status & XAWS_ICONIFIED)
			pb->intout[0] = 0;
		else
		{
			RECT in = *((const RECT *)(pb->intin+2));
			iconify_window(lock, w, &in);
		}
		break;
	}

	/* Un-Iconify a window */
	case WF_UNICONIFY:
	{
		if (!(w->window_status & XAWS_ICONIFIED))
			pb->intout[0] = 0;
		else
		{
			RECT in;
			if (pb->intin[4] == -1 || pb->intin[5] == -1 || !pb->intin[4] || !pb->intin[5])
				in = w->ro;
			else
				in = *((const RECT *)(pb->intin+2));
			
			uniconify_window(lock, w, &in);
		}
		break;
	}

	case WF_UNICONIFYXYWH:
	{
		w->ro = *((const RECT *)(pb->intin+2));
		break;
	}
	/* */
	case WF_TOOLBAR:
	{
		short obptr[2] = { pb->intin[2], pb->intin[3] };
		OBJECT *ob;
		XA_WIDGET *widg = get_widget(w, XAW_TOOLBAR);
		XA_TREE *wt;

		ob = *(OBJECT **)&obptr;

		DIAGS(("  wind_set(WF_TOOLBAR): obtree=%lx, current wt=%lx",
			ob, widg->stuff));
		if (ob)
		{
			wt = obtree_to_wt(client, ob);
			
			if (wt && wt == widg->stuff)
			{
				DIAGS((" --- Same toolbar installed"));
				if ((w->window_status & XAWS_OPEN))
				{
					widg->start = pb->intin[4];
					wt->e.obj = pb->intin[5];
					display_widget(lock, w, widg);
					widg->start = 0;
				}
			}
			else if (!widg->stuff)
			{
				DIAGS(("  --- Set new toolbar"));
				wt = set_toolbar_widget(lock, w, client, ob, pb->intin[5], 0, NULL);
				rp_2_ap_cs(w, widg, NULL);
				if (wt && wt->tree)
				{
					wt->tree->ob_x = w->wa.x;
					wt->tree->ob_y = w->wa.y;
					if (!wt->zen)
					{
						wt->tree->ob_x += wt->ox;
						wt->tree->ob_y += wt->oy;
					}
					w->active_widgets |= TOOLBAR;
				}
				w->dial |= created_for_TOOLBAR;
			}
		}
		else if (widg->stuff)
		{
			DIAGS(("  --- Remove toolbar"));
			remove_widget(lock, w, XAW_TOOLBAR);
		}
		if ((w->window_status & (XAWS_OPEN|XAWS_SHADED|XAWS_HIDDEN)) == XAWS_OPEN)
		{
			DIAGS(("  --- send WM_REDRAW"));
			generate_redraws(lock, w, &w->r, RDRW_EXT);
		}
		DIAGS(("  wind_set(WF_TOOLBAR) done"));
		break;
	}

	/* */
	case WF_MENU:
	{
		if (/*(w->window_status & XAWS_OPEN)*/
		    w->handle != 0
		    && (w->active_widgets & XaMENU) != 0)
		{
			short obptr[2] = { pb->intin[2], pb->intin[3] };
			OBJECT *ob;
			XA_WIDGET *widg = get_widget(w, XAW_MENU);
			XA_TREE *wt;

			ob = *(OBJECT **)&obptr;

			DIAGS(("  wind_set(WF_MENU) obtree=%lx, current wt=%lxfor %s",
				ob, widg->stuff, client->name));

			if (ob)
			{
				wt = obtree_to_wt(client, ob);

				if (!wt || (wt && wt != widg->stuff))
				{
					DIAGS(("  --- install new menu"));
					fix_menu(client, ob, false);
					if (!wt)
						wt = new_widget_tree(client, ob);
					set_menu_widget(w, client, wt);
					rp_2_ap_cs(w, widg, NULL);
					if (wt && wt->tree)
					{
						wt->tree->ob_x = w->wa.x;
						wt->tree->ob_y = w->wa.y;
						if (!wt->zen)
						{
							wt->tree->ob_x += wt->ox;
							wt->tree->ob_y += wt->oy;
						}
					}
				}
			}
			else if (widg->stuff)
			{
				DIAGS(("  --- Remove menu"));
				remove_widget(lock, w, XAW_MENU);
				w->active_widgets &= ~XaMENU;
			}
			DIAGS(("  wind_set(WF_MENU) done"));
		}
		break;
	}
	case WF_MINXYWH:
	{
		/* XXX Ozk: Add some boundary checks...?
		*/
		w->min = *((const RECT *)pb->intin + 2);
		break;
	}
	case WF_SHADE:
	{
		short status = -1, msg = -1;

		if (pb->intin[2] == 1)
		{
			if (!(w->window_status & XAWS_SHADED))
			{
				status = XAWS_SHADED;
				msg = WM_SHADED;
			}
		}
		else if (pb->intin[2] == 0 )
		{
			if (w->window_status & XAWS_SHADED)
			{
				status = ~XAWS_SHADED;
				msg = WM_UNSHADED;
			}
		}
		else if (pb->intin[2] == -1 )
		{
			if ( (w->window_status & XAWS_SHADED) )
			{
				status = ~XAWS_SHADED;
				msg = WM_UNSHADED;
			}
			else
			{
				status = XAWS_SHADED;
				msg = WM_SHADED;
			}
		}
	
		DIAGS(("wind_set: WF_SHADE, wind %d, status %x for %s",
			w->handle, status, client->name));

		if (msg != -1)
		{
			if (w->send_message)
				w->send_message(lock, w, NULL, AMQ_CRITICAL, QMF_CHKDUP,
					msg, 0, 0, w->handle, 0,0,0,0);

			move_window(lock, w, true, status, w->rc.x, w->rc.y, w->rc.w, w->rc.h);
		}
		break;
	}
	case WF_OPTS:
	{
		long  opts, *optr;
		short mode;

		if (w)
			optr = &w->opts;
		else
			optr = &client->options.wind_opts;

		mode = pb->intin[2];
 		opts = (long)pb->intin[3] << 16 | pb->intin[4];

		if (!mode)
			*optr &= ~opts;
		else if (mode == 1)
			*optr |= opts;

		break;
	}

	}
	return XAC_DONE;
}

/* Convert GEM widget numbers to XaAES widget indices. */
/* -1 means it is not a GEM object. */
static short GtoX[] =
{
	-1,		/* W_BOX,	*/
	-1, 		/* W_TITLE,	*/
	WIDG_CLOSER,	/* W_CLOSER,	*/
	-1,		/* W_NAME,	*/
	WIDG_FULL,	/* W_FULLER,	*/
	-1,		/* W_INFO,	*/
	-1,		/* W_DATA,	*/
	-1,		/* W_WORK,	*/
	WIDG_SIZE,	/* W_SIZER,	*/
	-1,		/* W_VBAR,	*/
	WIDG_UP,	/* W_UPARROW,	*/
	WIDG_DOWN,	/* W_DNARROW,	*/
	-1,		/* W_VSLlDE,	*/
	-1,		/* W_VELEV,	*/
	-1,		/* W_HBAR,	*/
	WIDG_LEFT,	/* W_LFARROW,	*/
	WIDG_RIGHT,	/* W_RTARROW,	*/
	-1,		/* W_HSLIDE,	*/
	-1,		/* W_HELEV,	*/
	WIDG_ICONIFY,	/* W_SMALLER,	*/
	WIDG_HIDE	/* W_BOTTOMER(W_HIDER?) */
};

/*
 * AES wind_get() function.
 * This includes support for most of the AES4 / AES4.1 extensions,
 */

unsigned long
XA_wind_get(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_window *w;
	XA_SLIDER_WIDGET *slw;
	short *o = pb->intout;
	RECT *ro = (RECT *)&pb->intout[1];
	int wind = pb->intin[0];
	int cmd = pb->intin[1];

	CONTROL(2,5,0)	

	w = get_wind_by_handle(lock, wind);

	DIAG((D_wind, client, "wind_get for %s  w=%lx, h:%d, %s",
		c_owner(client), w,wind, setget(cmd)));

	if (w == 0)
	{
		switch (cmd)
		{
			case WF_BOTTOM: /* Some functions may be allowed without a real window handle */
			case WF_FULLXYWH:
			case WF_TOP:
			case WF_NEWDESK:
			case WF_SCREEN:
			case WF_WHEEL:
			case WF_OPTS:
			case WF_XAAES: /* 'XA', idee stolen from WINX */
				break;
			default:
				DIAGS(("WARNING:wind_get for %s: Invalid window handle %d", c_owner(client), wind));
				o[0] = 0;	/* Invalid window handle, return error */
				o[1] = 0;	/* HR 020402: clear args */
				o[2] = 0;	/*            Prevents FIRST/NEXTXYWH loops with
						      invalid handle from looping forever. */
				o[3] = 0;
				o[4] = 0;

				return XAC_DONE;
		}
	}

	o[0] = 1;

	switch(cmd)
	{
	case WF_KIND:		/* new since N.Aes */
	{
		o[1] = w->active_widgets;
		break;
	}
	case WF_NAME:		/* new since N.Aes */
	{
		/* -Wcast-qual doesn't work as expected in gcc 2.95.x
		 * the worning is not valid here (seems to be fixed in 3.3.2 at least) 
		 * warns here: char *s = *(char **)&pb->intin[2];
		 */
		if (w->active_widgets & NAME)
			get_window_title(w, (char *)((long)pb->intin[2] << 16 | pb->intin[3]));
		else
			o[0] = 0;

		break;
	}
	case WF_INFO:		/* new since N.Aes */
	{
		if (w->active_widgets & INFO)
			get_window_info(w, (char *)((long)pb->intin[2] << 16 | pb->intin[3]));
		else
			o[0] = 0;
		break;
	}
	case WF_WHEEL:
	{
		long opt = w ? w->opts : client->options.wind_opts;
		short mode = w ? w->wheel_mode : client->options.wheel_mode;
		
		o[1] = (opt & XAWO_WHEEL) ? 1 : 0;
		o[2] = mode;
		break;
	}
	case WF_FIRSTAREAXYWH:
	{
		struct xa_rect_list *rl;

		DIAG((D_wind, client, "wind_xget: N_INTIN=%d, (%d/%d/%d/%d) on wind=%d for %s",
			pb->control[N_INTIN], *(const RECT *)(pb->intin+2), w->handle, client->name));
				
		w->rl_clip = *(const RECT *)(pb->intin + 2);
		w->use_rlc = true;
		make_rect_list(w, true, RECT_OPT);
		rl = rect_get_optimal_first(w);
		
		if (rl)
			*ro = rl->r;
		else
			ro->x = ro->y = ro->w = ro->h = 0;
		
		break;
	}		
	case WF_FTOOLBAR:	/* suboptimal, but for the moment it is more important that it van be used. */
	case WF_FIRSTXYWH:	/* Generate a rectangle list and return the first entry */
	{
		w->use_rlc = false;

		if (!get_rect(w, &w->wa, true, ro))
		{
			ro->x = w->r.x;
			ro->y = w->r.y;
			ro->w = ro->h = 0;
		}
		break;
	}
	case WF_NTOOLBAR:		/* suboptimal, but for the moment it is more
					   important that it van be used. */
	case WF_NEXTXYWH:		/* Get next entry from a rectangle list */
	{
		if (w->use_rlc)
		{
			struct xa_rect_list *rl = rect_get_optimal_next(w);
			
			if (rl)
				*ro = rl->r;
			else
				ro->x = ro->y = ro->w = ro->h = 0;
		}
		else if (!get_rect(w, &w->wa, false, ro))
		{
			ro->x = w->r.x;
			ro->y = w->r.y;
			ro->w = ro->h = 0;
		}
		break;
	}
	case WF_TOOLBAR:
	{
		XA_TREE *wt = get_widget(w, XAW_TOOLBAR)->stuff;
		OBJECT **have = (OBJECT **)&pb->intout[1];

		if (wt)
			*have = wt->tree;
		else
			*have = NULL;

		break;
	}
	case WF_MENU:
	{
		XA_TREE *wt = get_widget(w, XAW_MENU)->stuff;
		OBJECT **have = (OBJECT **)&pb->intout[1];

		if (wt)
			*have = wt->tree;
		else
			*have = NULL;
		break;
	}
	/*
	 * The info given by these function is very important to app's
         * hence the DIAG's
	 */

	/*
	 * Get the current coords of the window
	 */
	case WF_CURRXYWH:
	{
		*ro = w->rc;
		DIAG((D_w, w->owner, "get curr for %d: %d/%d,%d/%d",
			wind ,ro->x,ro->y,ro->w,ro->h));
		break;
	}
	/*
	 * Get the current coords of the window's user work area
	 */
	case WF_WORKXYWH:
	{
		*ro = w->wa;
		if (w == root_window && !taskbar(client))
			ro->h -= 24;
		DIAG((D_w, w->owner, "get work for %d: %d/%d,%d/%d",
			wind ,ro->x,ro->y,ro->w,ro->h));
		break;
	}
	/*
	 * Get previous window position
	 */
	case WF_PREVXYWH:
	{
		*ro = w->pr;
		DIAG((D_w, w->owner, "get prev for %d: %d/%d,%d/%d",
			wind ,ro->x,ro->y,ro->w,ro->h));
		break;			
	}
	/*
	 * Get maximum window dimensions specified in wind_create() call
	 */
	case WF_FULLXYWH:
	{
		/* This is the way TosWin2 uses the feature.
		 * Get the info and behave accordingly.
		 * Not directly under AES control.
		 */
		if (!w || w == root_window)
		{
			/* Ensure the windows don't overlay the menu bar */
			ro->x = root_window->r.x; 
			ro->y = root_window->wa.y;
			ro->w = root_window->r.w;
			ro->h = root_window->wa.h;
			if (!taskbar(client))
				ro->h -= 24;
			DIAG((D_w, NULL, "get max full: %d/%d,%d/%d",
				ro->x,ro->y,ro->w,ro->h));
		}
		else
		{
			*ro = w->max;
			DIAG((D_w, w->owner, "get full for %d: %d/%d,%d/%d",
				wind ,ro->x,ro->y,ro->w,ro->h));
		}
		break;
	}
	/*
	 * Return supported window features.
	 */
	case WF_BEVENT:
	{
		o[1] = ((w->active_widgets & NO_TOPPED) != 0) & 1;
		o[2] = o[3] = o[4] = 0;
		break;
	}
	/* Extension, gets the bottom window */
	case WF_BOTTOM:
	{
		if ((w = root_window->prev))
		{
			o[1] = w->handle;
			o[2] = w->owner->p->pid;
		}
		else
		{
			o[1] = -1;
			o[2] = 0;
		}
		break;
	}
	case WF_TOP:
	{
		w = window_list;

		o[1] = o[2] = 0;
		o[3] = o[4] = -1;

		//while (is_hidden(w))
		//	w = w->next;
		if (w)
		{
			if (w != root_window)
			{
				if (wind_has_focus(w))
				{
					DIAG((D_w, client, " -- top window=%d, owner='%s' has focus", w->handle, w->owner->name));
					o[1] = w->handle;
					o[2] = w->owner->p->pid;
				}
#if GENERATE_DIAGS
				else
					DIAG((D_w, client, " -- top window=%d, owner='%s' NOT in focus", w->handle, w->owner->name));
#endif
				if (w->next && w->next != root_window)
				{
					o[3] = w->next->handle;
					o[4] = w->next->owner->p->pid;
				}
				else
					o[3] = -1, o[4] = 0;
			}
		}
		else
		{
			DIAGS(("ERROR: empty window list!!"));
			o[1] = 0;		/* No windows open - return an error */
			o[0] = 0;
		}
		break;
	}
	/* AES4 compatible stuff */
	case WF_OWNER:
	{
		if (w == root_window)
			/* If it is the root window, things arent that easy. */
			o[1] = get_desktop()->owner->p->pid;
		else
			/* The window owners AESid (==app_id == MiNT pid) */
			o[1] = w->owner->p->pid;
		
		/* Is the window open? */
		o[2] = (w->window_status & XAWS_OPEN) ? 1 : 0;
		
		if (w->prev)	/* If there is a window above, return its handle */
			o[3] = w->prev->handle;
		else
			o[3] = -1;		/* HR: dont return a valid window handle (root window is 0) */
							/*     Some programs (Multistrip!!) get in a loop */
		if (w->next)	/* If there is a window below, return its handle */
			o[4] = w->next->handle;
		else
			o[4] = -1;

		DIAG((D_wind, client, "  --  owner: %d(%d), above: %d, below: %d", o[1], o[2], o[3], o[4]));
		break;
	}
	case WF_VSLIDE:
	{
		if (w->active_widgets & VSLIDE)
		{
			slw = get_widget(w, XAW_VSLIDE)->stuff;
			o[1] = slw->position;
			if (client->options.app_opts & XAPP_XT_WF_SLIDE)
				o[2] = slw->rpos;
		}
		else
			o[0] = o[1] = 0;
		break;
	}	
	case WF_HSLIDE:
	{
		if (w->active_widgets & HSLIDE)
		{
			slw = get_widget(w, XAW_HSLIDE)->stuff;
			o[1] = slw->position;
			if (client->options.app_opts & XAPP_XT_WF_SLIDE)
				o[2] = slw->rpos;
		}
		else
			o[0] = o[1] = 0;
		break;
	}
	case WF_HSLSIZE:
	{
		if (w->active_widgets&HSLIDE)
		{
			slw = get_widget(w, XAW_HSLIDE)->stuff;
			o[1] = slw->length;
		}
		else
			o[0] = o[1] = 0;
		break;
	}
	case WF_VSLSIZE:
	{
		if (w->active_widgets & VSLIDE)
		{
			slw = get_widget(w, XAW_VSLIDE)->stuff;
			o[1] = slw->length;
		}
		else
			o[0] = o[1] = 0;
		break;
	}
	case WF_SCREEN:
	{
		/* HR return a very small area :-) hope app's      */
		/*    then decide to allocate a buffer themselves. */
		/*    This worked for SELECTRIC.  */
		/*   Alas!!!! not PS_CONTRL.ACC	  */
		/* So now its all become official */

		/* HR: make the quarter screen buffer for wind_get(WF_SCREEN) :-( :-( */
		/*    What punishment would be adequate for the bloke (or bird) who invented this? */

		if (client->half_screen_buffer == NULL)
		{
			long sc = ((640L * 400 / 2) * screen.planes) / 8;

			DIAGS(("sc: %ld, cl: %ld", sc, client->options.half_screen));

			if (client->options.half_screen)
				/* Use if specified. whether greater or smaller. */
				client->half_screen_size = client->options.half_screen;
			else
				client->half_screen_size = sc;

			client->half_screen_buffer = umalloc(client->half_screen_size);
			DIAGS(("half_screen_buffer for %s: 0x%lx size %ld use %ld",
				c_owner(client),
				client->half_screen_buffer,
				client->half_screen_size,
				client->options.half_screen));
		}

		*(char  **)&o[1] = client->half_screen_buffer;
		*(size_t *)&o[3] = client->half_screen_size;
		break;
	}
	case WF_DCOLOR:
	{
		DIAGS(("WF_DCOLOR %d for %s, %d,%d", o[1], c_owner(client),o[2],o[3]));
		goto oeps;
	}
	case WF_COLOR:
	{
		DIAGS(("WF_COLOR %d for %s", o[1], c_owner(client)));
	oeps:
		if (o[1] <= W_BOTTOMER /*valid widget id*/)
		{
			OBJECT *widg = get_widgets();
			int i = GtoX[o[1]];
			BFOBSPEC c;

			if (i > 0 && (widg[i].ob_type & 0xff) == G_BOXCHAR)
			{
				/* c = get_ob_spec(widg + i)->this.colours; */
				c = object_get_spec(widg + i)->obspec;
			}
			else
			{
				c.framecol = screen.dial_colours.border_col;
				c.textcol = G_BLACK;
				c.fillpattern = IP_SOLID;
				if (o[1] == W_VELEV || o[1] == W_HELEV)
					c.interiorcol = screen.dial_colours.shadow_col;
				else
					c.interiorcol = screen.dial_colours.bg_col;
				c.textmode = 0;	/* transparant */
			}

			o[2] = ((short *)&c)[1];
		}
		else
		{
			o[2] = 0x1178;	/* CAB hack */
		}
		o[3] = o[2];
		o[4] = 0xf0f; 	/* HR 010402 mask and flags 3d */
		DIAGS(("   --   %04x", o[2]));
		break;
	}
	case WF_NEWDESK:
	{
		Sema_Up(desk);

		*(OBJECT **)&o[1] = get_desktop()->tree;

		Sema_Dn(desk);
		break;
	}
	case WF_ICONIFY:
	{
		o[1] = (w->window_status & XAWS_ICONIFIED) ? 1 : 0;
		o[2] = C.iconify.w;
		o[3] = C.iconify.h;
		break;
	}
	case WF_UNICONIFY:
	{
		*ro = (w->window_status & XAWS_ICONIFIED) ? w->ro : w->r;
		break;
	}
	case WF_SHADE:
	{
		*o = (w->window_status & XAWS_SHADED) ? 1 : 0;
		o[1] = *o;
		break;
	}
	case WF_XAAES: /* 'XA' */
	{
		o[0] = WF_XAAES;
		o[1] = HEX_VERSION;
		DIAGS(("hex_version = v%04x",o[1]));
		break;
	}
	case WF_OPTS:
	{
		long *optr = (w ? &w->opts : &client->options.wind_opts);
		
		o[1] = *optr >> 16;
		o[2] = *optr;
		break;
	}
	default:
	{
		DIAG((D_wind,client,"wind_get: %d",cmd));
		o[0] = 0;
	}
	}

	return XAC_DONE;
}

unsigned long
XA_wind_delete(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_window *w;

	CONTROL(1,1,0)	

	w = get_wind_by_handle(lock, pb->intin[0]);
	if (w && !(w->window_status & XAWS_OPEN))
	{
		delete_window(lock, w);
		pb->intout[0] = 1;
	}
	else
		pb->intout[0] = 0;

	return XAC_DONE;
}

/*
 * Go through and check that all windows belonging to this client are
 * closed and deleted
 */

/* HR: This is an exemple of the use of the lock system.
 *     The function can be called by both server and client.
 * 
 *     If called by the server, lock will be preset to winlist,
 *     resulting in no locking (not needed).
 *     
 *     If called by the signal handler, it is unclear, so the
 *     lock is applied. (doesnt harm).
 */
static void
rw(enum locks lock, struct xa_window *wl, struct xa_client *client)
{
	struct xa_window *nwl;

	while (wl)
	{
		DIAGS(("-- RW: %lx, next=%lx, prev=%lx",
			wl, wl->next, wl->prev));

		nwl = wl->next;
		if (wl != root_window)
		{
			if (!client || (client && wl->owner == client))
			{
				if ((wl->window_status & XAWS_OPEN))
				{
					DIAGS(("-- RW: closing %lx, client=%lx", wl, client));
					close_window(lock, wl);
				}
				DIAGS(("-- RW: deleting %lx", wl));
				delete_window(lock, wl);
			}
		}
#if GENERATE_DIAGS
		else
			DIAGS((" -- RW: skipping root window"));
#endif
		wl = nwl;
	}
}

void
remove_windows(enum locks lock, struct xa_client *client)
{
	DIAG((D_wind,client,"remove_windows on open_windows list for %s", c_owner(client)));
	rw(lock, window_list, client);
	DIAG((D_wind,client,"remove_windows on closed_windows list for %s", c_owner(client)));
	rw(lock, S.closed_windows.first, client);
}

void
remove_all_windows(enum locks lock, struct xa_client *client)
{
	DIAG((D_wind, client,"remove_all_windows for %s", c_owner(client)));

	remove_windows(lock, client);
	DIAG((D_wind, client, "remove_all_windows on open_nlwindows for %s", c_owner(client)));
	rw(lock, S.open_nlwindows.first, client);
	DIAG((D_wind, client, "remove_all_windows on closed_nlwindows for %s", c_owner(client)));
	rw(lock, S.closed_nlwindows.first, client);
}

unsigned long
XA_wind_new(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(0,0,0)	

	remove_windows(lock, client);

	return XAC_DONE;
}


/*
 * wind_calc
 * HR: embedded function calc_window()
 */
unsigned long
XA_wind_calc(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(6,5,0)	
	
	DIAG((D_wind, client, "wind_calc: req=%d, kind=%d",
		pb->intin[0], pb->intin[1]));

	*(RECT *) &pb->intout[1] =
		calc_window(lock,
			    client,
			    pb->intin[0],		/* request */
			    (unsigned short)pb->intin[1],		/* widget mask */
			    client->options.thinframe,
			    client->options.thinwork,
			    *(const RECT *)&pb->intin[2]);

	pb->intout[0] = 1;
	return XAC_DONE;
}

/*
 * Wind_update handling
 * This handles locking for the update and mctrl flags.
 *
 * The fmd.lock may only be updated in the real wind_update,
 * otherwise screen writing AES functions that use the internal
 * lock/unlock_screen functions spoil the state.
 * This repairs zBench dialogues.
 * Also changed fmd.lock usage to additive flags.
 */

/*
 * Ozk: XA_wind_update() may be called by processes not yet called
 * appl_init(). So, it must not depend on client being valid!
 */

unsigned long
XA_wind_update(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct proc *p;

	short op = pb->intin[0];
	bool try = (op & 0x100) ? true : false; /* Test for check-and-set mode */

	CONTROL(1,1,0)

	DIAG((D_sema, NULL, "XA_wind_update for %s %s (%d)",
		c_owner(client), try == true ? "" : "RESPONSE", pb->intin[0]));

	pb->intout[0] = 1;

	if (client)
		p = client->p;
	else
		p = get_curproc();

	switch (op & 0xff)
	{

	/* Grab the update lock */
	case BEG_UPDATE:
	{
		DIAG((D_sema, NULL, "'%s' BEG_UPDATE", p->name));

		if (lock_screen(p, try, pb->intout, 1))
		{
			if (client)
				client->fmd.lock |= SCREEN_UPD;
			C.update_lock = p;
			C.updatelock_count++;
		}
		DIAG((D_sema, NULL, " -- count %d for %s",
			C.updatelock_count, C.update_lock->name));

		break;
	}
	/* Release the update lock */
	case END_UPDATE:
	{
		if (unlock_screen(p, 1))
		{
			if (client)
				client->fmd.lock &= ~SCREEN_UPD;
		}
		if (C.update_lock == p)
		{
			C.updatelock_count--;
			if (!C.updatelock_count)
				C.update_lock = NULL;
		}

		DIAG((D_sema, NULL, "'%s' END_UPDATE", p->name));
		DIAG((D_sema, NULL, " -- count %d for %s",
			C.updatelock_count, p->name));
		break;
	}

	/* Grab the mouse lock */
	case BEG_MCTRL:
	{
		DIAG((D_sema, NULL, "'%s' BEG_MCTRL", client->name));

		if (lock_mouse(p, try, pb->intout, 1))
		{
			if (client)
				client->fmd.lock |= MOUSE_UPD;
			C.mouse_lock = p;
			C.mouselock_count++;
		}

		DIAG((D_sema, NULL, " -- count %d for %s",
			C.mouselock_count, C.mouse_lock->name));
		break;
	}
	/* Release the mouse lock */
	case END_MCTRL:
	{
		
		if (unlock_mouse(p, 1))
		{
			if (client)
				client->fmd.lock &= ~MOUSE_UPD;
		}
		if (C.mouse_lock == p)
		{
			C.mouselock_count--;
			if (!C.mouselock_count)
				C.mouse_lock = NULL;
		}

		DIAG((D_sema, NULL, "'%s' END_MCTRL", p->name));

		DIAG((D_sema, NULL, " -- count %d for %s",
			C.mouselock_count, p->name));
		break;
	}

	default:
		DIAG((D_sema, NULL, "WARNING! Invalid opcode for wind_update: 0x%04x", op));
	}

	return XAC_DONE;
}
