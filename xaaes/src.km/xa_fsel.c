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

/*
 * FILE SELECTOR IMPLEMENTATION
 * 
 * This give a non-modal windowed file selector for internal
 * and exported use, with slightly extended pattern matching
 * from the basic GEM spec.
 */

#include RSCHNAME

#include "xa_fsel.h"
#include "xa_global.h"

#include "c_window.h"
#include "form.h"
#include "k_main.h"
#include "matchpat.h"
#include "menuwidg.h"
#include "nkcc.h"
#include "obtree.h"
#include "rectlist.h"
#include "scrlobjc.h"
#include "util.h"
#include "widgets.h"

#include "xa_graf.h"
#include "xa_rsrc.h"

#include "mint/pathconf.h"
#include "mint/stat.h"



static char fs_paths[DRV_MAX][NAME_MAX+2];
static char fs_patterns[23][16];


static void
add_pattern(char *pattern)
{
	int i;
	if (pattern && *pattern)
	{
		for (i = 0; i < 23; i++)
		{
			if (fs_patterns[i][0])
			{
				if (!stricmp(pattern, fs_patterns[i]))
				{
					break;
				}
			}
			else
			{
				strncpy(fs_patterns[i], pattern, 16);
				fs_patterns[i][15] = '\0';
				break;
			}
		}
	}
}

void
init_fsel(void)
{
	int i;
	for (i = 0; i < DRV_MAX; i++)
		fs_paths[i][0] = 0;

	for (i = 0; i < 23; i++)
		fs_patterns[i][0] = '\0';

	add_pattern("*");

	for (i = 0; i < 23; i++)
	{
		if (cfg.Filters[i])
			add_pattern(cfg.Filters[i]);
	}
}

static char *
getsuf(char *f)
{
	char *p;

	if ((p = strrchr(f, '.')) != 0L)
	{
		if (strchr(p, '/') == 0L
		    && strchr(p, '\\') == 0L)
			/* didnt ran over slash? */
			return p+1;
	}
	/* no suffix  */
	return 0L;
}

/*
 *  find typed characters in the list
 */
#if 0
/* This one didnt exist.
   It doesnt reinvent the wheel. :-)	*/
static char *
stristr(char *in, char *s)
{
	char xs[32], xin[32];
	char *x;

	if (strlen(s) < 32 && strlen(in) < 32)
	{
		strcpy(xs, s);
		strlwr(xs);
		strcpy(xin, in);
		strlwr(xin);

		x = strstr(xin, xs);
		if (x)
			x = in + (xin - x);
	}
	else
		x = strstr(in, s);

	return x;
}
#endif

static bool
fs_prompt(SCROLL_INFO *list)
{
	struct fsel_data *fs = list->data;
	SCROLL_ENTRY *s;
	bool ret = false;

	s = list->start;

	/* Not if filename empty or list empty */
	if (*fs->file && s)
	{
		struct seget_entrybyarg seget, p;
		SCROLL_ENTRY *old = list->cur;

		seget.e = s;
		seget.arg.flags = ENT_VISIBLE;
		seget.arg.maxlevel = -1;
		seget.arg.curlevel = 0;
		p.idx = 0;
		while (seget.e)
		{
			list->get(list, seget.e, SEGET_TEXTPTR, &p);
			if (p.ret.ptr && match_pattern(p.ret.ptr, fs->file, true))
				break;
			list->get(list, seget.e, SEGET_NEXTENT, &seget);
		}
		list->cur = NULL;
		s = seget.e;
		fs->selected_entry = s;
		if (s)
		{
			list->cur = s;
			list->set(list, NULL, SESET_MULTISEL, 0, NOREDRAW);
			list->set(list, s, SESET_SELECTED, 0, NORMREDRAW);
			list->vis(list, s, NORMREDRAW);
			ret = true;
		}
		else
		{
			list->get(list, NULL, SEGET_SELECTED, &s);
			if (s)
			{
				list->set(list, s, SESET_UNSELECTED, 0, NORMREDRAW);
				ret = true;
			}
		}
		/* prompt changed the selection... */
		fs->clear_on_folder_change |= old == list->cur;
	}
	else
	{
		fs->selected_entry = NULL;
		list->get(list, NULL, SEGET_SELECTED, &s);
		if (s)
		{
			list->set(list, s, SESET_UNSELECTED, 0, NORMREDRAW);
			ret = true;
		}
	}
	return ret;
}

static bool
fs_prompt_refresh(SCROLL_INFO *list)
{
	struct fsel_data *fs = list->data;
	int clear = fs->clear_on_folder_change;
	bool res = fs_prompt(list);
	fs->clear_on_folder_change = clear;
	return res;
}

typedef bool sort_compare(SCROLL_ENTRY *s1, SCROLL_ENTRY *s2);

#define FLAG_FILLED	0x00000001
#define FLAG_DIR	0x00000002
#define FLAG_EXE	0x00000004
#define FLAG_LINK	0x00000008

/*
 * This is called by the slist functions
 */
static bool
dirflag_name(SCROLL_ENTRY *s1, SCROLL_ENTRY *s2)
{
	unsigned short f1 = s1->c.usr_flags & FLAG_DIR;
	unsigned short f2 = s2->c.usr_flags & FLAG_DIR;

	if (f1 < f2)
	{
		/* folders in front */
		return true;
	}

	if (f1 == f2 && stricmp(s1->c.td.text.text->text, s2->c.td.text.text->text) > 0)
	{
		return true;
	}
	return false;
}

/* yields true if case sensitive */
static bool
inq_xfs(struct fsel_data *fs, char *p, char *dsep)
{	
	long c, t;

	c = fs->fcase = d_pathconf(p, DP_CASE);
	t = fs->trunc = d_pathconf(p, DP_TRUNC);

	DIAG((D_fsel, NULL, "inq_xfs '%s': case = %ld, trunc = %ld", p, c, t));

	if (dsep)
	{
		* dsep    = '\\';
		*(dsep+1) = 0;
	}

	return !(c == 1 && t == 2);
}

static void
add_slash(char *p, char *to)
{
	char *pl = p + strlen(p) - 1;

	if (*pl != '/' && *pl != '\\')
		strcat(p, to);
}

static bool
executable(char *nam)
{
	char *ext = getsuf(nam);

	return (ext
		/* The mintlib does almost the same :-) */
		&& (   !stricmp(ext, "ttp") || !stricmp(ext, "prg")
		    || !stricmp(ext, "tos") || !stricmp(ext, "g")
		    || !stricmp(ext, "sh")  || !stricmp(ext, "bat")
		    || !stricmp(ext, "gtp") || !stricmp(ext, "app")
		    || !stricmp(ext, "acc")));
}

static void
set_file(enum locks lock, struct fsel_data *fs, const char *fn)
{
	XA_TREE *wt = get_widget(fs->wind, XAW_TOOLBAR)->stuff;

	DIAG((D_fsel, NULL, "set_file: fs.file='%s', wh=%d", fn, fs->wind->handle));

	/* fixup the cursor edit position */
#if 0
	/* FIXME: the edit_object() doesn't work in case the client
	 * didn't call the form_do().
	 *
	 * We should call something like the below instead
	 * of direct e.pos adjustments.
	 */
	{
		short newpos;
		edit_object(lock, wt->owner, ED_END, wt, wt->tree, FS_FILE, 0, &newpos);
		strcpy(fs.file, fn); /* set the edit field text */
		edit_object(lock, wt->owner, ED_INIT, wt, wt->tree, FS_FILE, 0, &newpos);
	}
#else
	strcpy(fs->file, fn); /* set the edit field text */
	wt->e.pos = strlen(fn);
#endif

	/* redraw the toolbar file object */
	redraw_toolbar(lock, fs->wind, FS_FILE);
}
#define FSIZE_MAX 20

static void
strins(char *d, const char *s, long here)
{
	long slen = strlen(s);
	long dlen = strlen(d);
	char t[512];
	
	if (here > dlen)
		here = dlen;

	strncpy(t, d + here, sizeof(t));
	strncpy(d + here, s, slen);
	strcpy(d + here + slen, t);
}

static void
read_directory(struct fsel_data *fs, SCROLL_INFO *list, SCROLL_ENTRY *dir_ent)
{
	bool csens;
	OBJECT *obtree = fs->form->tree;
	char nm[NAME_MAX+2 + FSIZE_MAX+2];
	long i, rep;

	if (dir_ent && (dir_ent->xstate & OS_OPENED))
	{
		/*
		 * If this directory is already opened, just close it
		 */
		list->set(list, dir_ent, SESET_OPEN, 0, NORMREDRAW);
		dir_ent = NULL;
	} 
	else
	{
		if (dir_ent)
		{
			long here;
			SCROLL_ENTRY *this;
			struct seget_entrybyarg p;
			
			strcpy(fs->path, fs->root);
			here = strlen(fs->path);
			this = dir_ent;
			p.idx = 0;
			while (this)
			{
				list->get(list, this, SEGET_TEXTPTR, &p);
				strins(fs->path, "\\", here);
				strins(fs->path, p.ret.ptr, here);
				
				this = this->up;
			}
			/* If realtime build, open entry here */
			//list->set(list, dir_ent, SESET_OPEN, 1, true);
			list->get(list, dir_ent, SEGET_USRFLAGS, &here);
			if (here & 1)
			{
				list->set(list, dir_ent, SESET_OPEN, 1, NORMREDRAW);
				return;
			}
		}
		else
			strcpy(fs->path, fs->root);

		csens = inq_xfs(fs, fs->path, fs->fslash);
		i = d_opendir(fs->path, 0);
		
		DIAG((D_fsel, NULL, "Dopendir -> %lx", i));
	
		if (i > 0)
		{
			struct xattr xat;

			while (d_xreaddir(NAME_MAX, i, nm, &xat, &rep) == 0)
			{
				struct scroll_content sc = { 0 };
				char *nam = nm+4;
				bool dir = S_ISDIR(xat.mode);
				bool sln = S_ISLNK(xat.mode);
				bool match = true;

				if (!csens)
					strupr(nam);

				if (sln) 
				{
					char fulln[NAME_MAX+2];

					strcpy(fulln, fs->path);
					strcat(fulln, nam);

					DIAG((D_fsel, NULL, "Symbolic link: Fxattr on '%s'", fulln));
					f_xattr(0, fulln, &xat);
					dir = (xat.mode & 0xf000) == S_IFDIR;

					DIAG((D_fsel, NULL, "After Fxattr dir:%d", dir));
				}

				if (!dir)
					match = match_pattern(nam, fs->fs_pattern, false);
				else if (dir_ent)
				{
					if (!strcmp(nam, ".") || !strcmp(nam, ".."))
						continue;
				}

				if (match)
				{
					OBJECT *icon = NULL;
					char *s = nam + strlen(nam) + 1;
			
					sc.text = nam;
					if (dir)
					{
						sc.usr_flags |= FLAG_DIR;
						icon = obtree + FS_ICN_DIR;
						sc.xstate |= OS_NESTICON;
					}
					else if (executable(nam))
						icon = obtree + FS_ICN_PRG;
					else
						icon = obtree + FS_ICN_FILE;
			
					if (!dir)
					{
						if (xat.size < (1024UL * 1024) )
							sprintf(s, 20, "%ld", xat.size);
						else if (xat.size < (1024UL * 1024 * 10))
							sprintf(s, 20, "%ld KB", xat.size >> 10);
						else
							sprintf(s, 20, "%ld MB", xat.size >> 20);
					}
					else
						sprintf(s, 20, "<dir>");

					sc.icon = icon;
					sc.n_strings = 2;
					list->add(list, dir_ent, dirflag_name, &sc, dir_ent ? SEADD_PRIOR|SEADD_CHILD : SEADD_PRIOR, FLAG_AMAL, NORMREDRAW);
				}
			}
			d_closedir(i);
		}
		if (dir_ent)
		{
			/*
			 * Set bit 0 in user flags to indicate that this directory is read
			 */
			long uf;
			list->get(list, dir_ent, SEGET_USRFLAGS, &uf);
			uf |= FLAG_FILLED;
			list->set(list, dir_ent, SESET_USRFLAGS, uf, 0);
		}
	}
	/* If realtime directory building, disable this */
	if (dir_ent)
	{
		list->set(list, dir_ent, SESET_OPEN, 1, true);
	}
}
/*
 * Re-load a file_selector listbox
 * HR: version without the big overhead of separate dir/file lists
       and 2 quicksorts + the moving around of all that.
       Also no need for the Mintlib. (Mintbind only).
 */

static void
refresh_filelist(enum locks lock, struct fsel_data *fs, SCROLL_ENTRY *dir_ent)
{
	OBJECT *form = fs->form->tree;
	OBJECT *sl;
	SCROLL_INFO *list;

	sl = form + FS_LIST;
	DIAG((D_fsel, NULL, "refresh_filelist: fs = %lx, obtree = %lx, sl = %lx",
		fs, fs->form->tree, sl));
	list = object_get_slist(sl);
	add_slash(fs->root, fs->fslash);

#ifdef FS_DBAR
	{
		TEDINFO *tx;

		tx = ob_spec(form + FS_CASE);
		sprintf(tx->te_ptext, 32, "%ld", fs->fcase);

		tx = ob_spec(form + FS_TRUNC);
		sprintf(tx->te_ptext, 32, "%ld", fs->trunc);

		redraw_toolbar(lock, fs->wind, FS_DBAR);
	}
#endif
	DIAG((D_fsel, NULL, "refresh_filelist: fs.path='%s',fs_pattern='%s'",
		fs->root, fs->fs_pattern));

	/* Clear out current file list contents */
	if (!dir_ent)
	{
		fs->selected_entry = NULL;
		while (list->start)
		{
			list->start = list->del(list, list->start, false);
		}
		list->redraw(list, NULL);
	}
	
	display_widget(lock, list->wi, get_widget(list->wi, XAW_TITLE), list->pw ? list->pw->rect_start : NULL);
	
	graf_mouse(HOURGLASS, NULL, NULL, false);

	read_directory(fs, list, dir_ent);

	graf_mouse(ARROW, NULL, NULL, false);

	fs_prompt_refresh(list);
}

static void
CE_refresh_filelist(enum locks lock, struct c_event *ce, bool cancel)
{
	if (!cancel)
	{
		refresh_filelist(lock, ce->ptr1, NULL);
	}
}

#define DRIVELETTER(i)	(i + ((i < 26) ? 'A' : '1' - 26))
static int
fsel_drives(OBJECT *m, int drive)
{
	int drv = 0, drvs = 0;
	int d = FSEL_DRVA;
	unsigned long dmap = b_drvmap();

	while (dmap)
	{
		if (dmap & 1)
		{
			m[d].ob_state &= ~OS_CHECKED;

			if (drv == drive)
				m[d].ob_state |= OS_CHECKED;

			sprintf(m[d++].ob_spec.free_string, 32, "  %c:", DRIVELETTER(drv));
			drvs++;
		}
		dmap >>= 1;
		drv++;
	}

	if (drvs & 1)
		m[d-1].ob_width = m[FSEL_DRVBOX].ob_width;

	do {
		m[d].ob_flags |= OF_HIDETREE;
		/* prevent finding those. */
		*(m[d].ob_spec.free_string + 2) = '~';
	}
	while (m[d++].ob_next != FSEL_DRVBOX);

	m[FSEL_DRVBOX].ob_height = ((drvs + 1) / 2) * screen.c_max_h;

	sprintf(m[FSEL_DRV].ob_spec.free_string, 32, " %c:", DRIVELETTER(drive));
	return drvs;
}
			
static void
fsel_filters(OBJECT *m, char *pattern)
{
	char p[16];
	int d = FSEL_PATA;
	int i = 0;

	add_pattern(pattern);

	if (pattern && *pattern && fs_patterns[0][0])
	{
		while (i < 23 && fs_patterns[i][0])
		{
			//fs_patterns[i][15] = 0;
			m[d].ob_state &= ~OS_CHECKED;
			if (stricmp(pattern, fs_patterns[i]) == 0)
			{
				m[d].ob_state |= OS_CHECKED;
			}
			sprintf(m[d++].ob_spec.free_string, 128, "  %s", fs_patterns[i++]);
		}
	
		do
		{
			m[d].ob_flags |= OF_HIDETREE;
		}
		while (m[d++].ob_next != FSEL_PATBOX);

		m[FSEL_PATBOX].ob_height = i * screen.c_max_h;
	}
	strncpy(p, pattern, 15);
	p[15] = 0;
	sprintf(m[FSEL_FILTER].ob_spec.free_string, 128, " %s", p);
}

/* HR: a little bit more exact. */
/* Dont need form as long as everything is global 8-} */
static void
fs_updir(enum locks lock, struct fsel_data *fs)
{
	int drv;

	if (*fs->root)
	{
		int s = strlen(fs->root) - 1;

		if (fs->root[s] == '/' || fs->root[s] == '\\')
			s--;

		DIAG((D_fsel, NULL, "fs_updir '%s'", fs->root));	

		while (  s
		      && fs->root[s] != ':'
		      && fs->root[s] != '/'
		      && fs->root[s] != '\\')
			s--;

		if (fs->root[s] == ':')
			fs->root[++s] = *fs->fslash;

		fs->root[++s] = 0;
		DIAG((D_fsel,NULL,"   -->   '%s'", fs->root));
	}

	if ((drv = get_drv(fs->root)) >= 0)
		strcpy(fs_paths[drv], fs->root);

	refresh_filelist(fsel, fs, NULL);
}

static void
fs_closer(struct scroll_info *list, bool rdrw)
{
	struct fsel_data *fs = list->data;

	if ( fs->clear_on_folder_change )
		set_file(list->lock, fs, "");

	fs_updir(list->lock, fs);
}

static int
fs_item_action(enum locks lock, struct scroll_info *list, OBJECT *form, int objc)
{
	struct fsel_data *fs = list->data;
	struct seget_entrybyarg p;

	DIAG((D_fsel, NULL, "fs_item_action %lx, fs=%lx", list->cur, fs));
	if (list->cur)
	{
		long uf;

		list->get(list, list->cur, SEGET_USRFLAGS, &uf);

		if (!(uf & FLAG_DIR))
		{
			DIAG((D_fsel, NULL, " --- nodir '%s'", list->cur->c.td.text.text->text));
			/* file entry action */
			p.idx = 0;
			p.e = list->cur;
			p.arg.typ.txt = fs->file;
			list->get(list, list->cur, SEGET_TEXTCPY, &p);
			fs->selected_entry = list->cur;
		}
		else
		{			
			/* folder entry action */
			DIAG((D_fsel, NULL, " --- folder '%s'", list->cur->c.td.text.text->text));
			p.idx = 0;
			p.arg.typ.txt = "..";
			
			if (list->get(list, list->cur, SEGET_TEXTCMP, &p) && !p.ret.ret)
			{
				if ( fs->clear_on_folder_change )
					set_file(lock, fs, "");

				/* cur on double dot folder line */
				fs_updir(lock, fs);
				return true;
			}
			p.arg.typ.txt = ".";
			if (list->get(list, list->cur, SEGET_TEXTCMP, &p) && p.ret.ret)
			{
				int drv;
				add_slash(fs->root, fs->fslash);
				if (!fs->treeview)
				{
					p.idx = 0;
					list->get(list, list->cur, SEGET_TEXTPTR, &p);
					strcat(fs->root, p.ret.ptr);
				}	
				if ((drv = get_drv(fs->root)) >= 0)
					strcpy(fs_paths[drv], fs->root);
				if (fs->clear_on_folder_change)
					set_file(lock, fs, "");
				refresh_filelist(fsel, fs, fs->treeview ? list->cur : NULL);
#if 0
				/* cur on common folder line (NON dot) */
				int drv;
				add_slash(fs->root, fs->fslash);
				strcat(fs->root, list->cur->c.td.text.text->text);
				if ((drv = get_drv(fs->root)) >= 0)
					strcpy(fs_paths[drv], fs->root);
				if ( fs->clear_on_folder_change )
					set_file(lock, fs, "");
				refresh_filelist(fsel, fs, list->cur);
#endif
				return true;
			}
		}
	}
	DIAG((D_fsel, NULL, "fs_item_action: %s%s", fs->root, fs->file));

	
	strcpy(fs->path, fs->root);
	if (fs->selected_entry)
	{
		struct scroll_entry *this = fs->selected_entry->up;
		long len = strlen(fs->root);
		
		p.idx = 0;		
		while (this)
		{
			if (list->get(list, this, SEGET_TEXTPTR, &p))
			{
				strins(fs->path, "\\", len);
				strins(fs->path, p.ret.ptr, len);
			}
			this = this->up;
		}
	}
	if (fs->selected)	
		fs->selected(lock, fs, fs->path, fs->file);
	return true;
}

static int
fs_dclick(enum locks lock, struct scroll_info *list, OBJECT *form, int objc)
{
	struct fsel_data *fs = list->data;
	/* since mouse click we should _not_ clear the file field */
	fs->clear_on_folder_change = 0;

	return fs_item_action(lock, list, form, objc);
}

/* converted */
static int
fs_click(enum locks lock, struct scroll_info *list, OBJECT *form, int objc)
{
	struct fsel_data *fs = list->data;

	/* since mouse click we should _not_ clear the file field */
	fs->clear_on_folder_change = 0;

	if (list->cur)
	{
		struct seget_entrybyarg p;
		long uf;
		
		list->get(list, list->cur, SEGET_USRFLAGS, &uf);
		p.idx = 0;
		list->get(list, list->cur, SEGET_TEXTPTR, &p);
		if ( ! (uf & FLAG_DIR))
		{
			set_file(lock, fs, p.ret.ptr);
		}
		else if (strcmp(p.ret.ptr, ".") == 0)
		{
			set_file(lock, fs, "");
		}
		else
		{
			fs_item_action(lock, list, form, objc);
		}
	}
	return true;
}

/*
 * FormExit()
 */
static void
fileselector_form_exit(struct xa_client *client,
		       struct xa_window *wind,
		       XA_TREE *wt,
		       struct fmd_result *fr)
{
	enum locks lock = 0;
	OBJECT *obtree = wt->tree;
	struct scroll_info *list = object_get_slist(obtree + FS_LIST);
	struct fsel_data *fs = list->data;
#ifdef FS_FILTER
	TEDINFO *filter = object_get_tedinfo(obtree + FS_FILTER);
#endif
	
	/* Ozk:
	 * Dont know exactly what 'which' and 'current' does yet...
	 * ... now I do ;-)
	 */
	wt->which = 0;
	wt->current = fr->obj;
	
	switch (fr->obj)
	{
	/*
	 * 
	 */
	case FS_LIST:
	{
		short obj = fr->obj;

		DIAGS(("filesel_form_exit: Moved the shit out of form_do() to here!"));
		if ( fr->md && ((obtree[obj].ob_type & 0xff) == G_SLIST))
		{
			if (fr->md->clicks > 1)
				dclick_scroll_list(lock, obtree, obj, fr->md);
			else
				click_scroll_list(lock, obtree, obj, fr->md);
		}
		break;
	}
#ifdef FS_UPDIR
	/* Go up a level in the filesystem */
	case FS_UPDIR:
	{
		fs_updir(lock, fs);
		break;
	}
#endif
	/* Accept current selection - do the same as a double click */
	case FS_OK:
	{
		object_deselect(wt->tree + FS_OK);
		redraw_toolbar(lock, wind, FS_OK);
#ifdef FS_FILTER
		if (strcmp(filter->te_ptext, fs->fs_pattern) != 0)
		{
			/* changed filter */
			strcpy(fs->fs_pattern, filter->te_ptext);
			refresh_filelist(fsel, fs, NULL);
		}
		else
#endif
		{
			fs_item_action(lock, list, wt->tree, FS_LIST);
		}
		break;
	}
	/* Cancel this selector */
	case FS_CANCEL:
	{
		object_deselect(wt->tree + FS_CANCEL);
		redraw_toolbar(lock, wind, FS_CANCEL);
		fs->selected_entry = NULL;
		if (fs->canceled)
	 		fs->canceled(lock, fs, fs->root, "");
		else
			close_fileselector(lock, fs);
		break;
	}
	}
}


static int
find_drive(int a, struct fsel_data *fs)
{
	XA_TREE *wt = get_widget(fs->wind, XAW_MENU)->stuff;
	OBJECT *m = wt->tree;
	int d = FSEL_DRVA;

	do {
		int x = tolower(*(m[d].ob_spec.free_string + 2));
		if (x == '~')
			break;
		if (x == a)
			return d;
	} while (m[d++].ob_next != FSEL_DRVA - 1);

	ping();

	return -1;
}

static void
fs_change(enum locks lock, struct fsel_data *fs, OBJECT *m, int p, int title, int d, char *t)
{
	XA_WIDGET *widg = get_widget(fs->wind, XAW_MENU);
	int bx = d - 1;
	
	do
		m[d].ob_state &= ~OS_CHECKED;
	while (m[d++].ob_next != bx);

	m[p].ob_state |= OS_CHECKED;
	sprintf(m[title].ob_spec.free_string, 128, " %s", m[p].ob_spec.free_string + 2);
	widg->start = 0;
	m[title].ob_state &= ~OS_SELECTED;
	display_widget(lock, fs->wind, widg, NULL);
	strcpy(t, m[p].ob_spec.free_string + 2);
}

/*
 * FormKeyInput()
 */
static bool
fs_key_form_do(enum locks lock,
	       struct xa_client *client,
	       struct xa_window *wind,
	       XA_TREE *wt,
	       const struct rawkey *key)
{
	unsigned short keycode = key->aes;
	unsigned short nkcode = key->norm;
	struct scroll_info *list = object_get_slist(((XA_TREE *)get_widget(wind, XAW_TOOLBAR)->stuff)->tree + FS_LIST);
	struct fsel_data *fs = list->data;
	SCROLL_ENTRY *old_entry = list->cur;

	/* HR 310501: ctrl|alt + letter :: select drive */
	if ((key->raw.conin.state & (K_CTRL|K_ALT)) != 0)
	{
		ushort nk;

		if (nkcode == 0)
			nkcode = nkc_tconv(key->raw.bcon);

		nk = tolower(nkcode & 0xff);
		if (   (nk >= 'a' && nk <= 'z')
		    || (nk >= '0' && nk <= '9'))
		{
			int drive_object_index = find_drive(nk, fs);
			if (drive_object_index >= FSEL_DRVA)
				wind->send_message(lock, wind, NULL, AMQ_NORM, QMF_CHKDUP,
						   MN_SELECTED, 0, 0, FSEL_DRV,
						   drive_object_index, 0, 0, 0);
		}

		/* ctrl + backspace :: fs_updir() */
		if ((nkcode & NKF_CTRL) && nk == NK_BS)
		{
			if (fs->clear_on_folder_change)
				set_file(lock, fs, "");

			fs_updir(lock, fs);
		}
		if ((nkcode & NKF_CTRL) && nk == '*')
		{
			/* change the filter to '*'
			 * - this should always be the FSEL_PATA entry IIRC
			 */
			fs_change(lock, fs, fs->menu->tree,
					FSEL_PATA, FSEL_FILTER, FSEL_PATA, fs->fs_pattern);
			/* apply the change to the filelist */
			refresh_filelist(fsel, fs, NULL);
		}
	}
	else
	/*  If anything in the list and it is a cursor key */
	if (scrl_cursor(list, keycode) != -1)
	{
		if (list->cur)
		{
			long uf;
			list->get(list, list->cur, SEGET_USRFLAGS, &uf);
			if (!(uf & FLAG_DIR))
			{
				struct seget_entrybyarg p;
				p.idx = 0;
				if (old_entry != list->cur)
					fs->clear_on_folder_change = 1;
				list->get(list, list->cur, SEGET_TEXTPTR, &p);
				set_file(lock, fs, p.ret.ptr);
			}
		}
	}
	else
	{
		char old[NAME_MAX+2];
		strcpy(old, fs->file);
		/* HR 290501: if !discontinue */
		if (Key_form_do(lock, client, wind, wt, key))
		{
			/* something typed in there? */
			if (strcmp(old, fs->file) != 0)
			{
				fs_prompt(list);
			}
		}
	}
	return true;
}

/* HR: make a start */
/* dest_pid, msg, source_pid, mp3, mp4,  ....    */
static void
fs_msg_handler(
	struct xa_window *wind,
	struct xa_client *to,
	short amq, short qmf,
	short *msg)
{
	enum locks lock = 0;
	struct scroll_info *list = object_get_slist(((XA_TREE *)get_widget(wind, XAW_TOOLBAR)->stuff)->tree + FS_LIST);
	struct fsel_data *fs = list->data;

	switch (msg[0])
	{
	case MN_SELECTED:
	{
		if (msg[3] == FSEL_FILTER)
			fs_change(lock, fs, fs->menu->tree, msg[4], FSEL_FILTER, FSEL_PATA, fs->fs_pattern);
		else if (msg[3] == FSEL_DRV)
		{
			int drv;
			fs_change(lock, fs, fs->menu->tree, msg[4], FSEL_DRV, FSEL_DRVA, fs->root);
			inq_xfs(fs, fs->root, fs->fslash);
			add_slash(fs->root, fs->fslash);
			drv = get_drv(fs->root);
			if (fs_paths[drv][0])
				strcpy(fs->root, fs_paths[drv]);
			else
				strcpy(fs_paths[drv], fs->root);
			/* remove the name from the edit field on drive change */
			if ( fs->clear_on_folder_change )
				set_file(lock, fs, "");
		}
		refresh_filelist(lock, fs, NULL);
		break;
	}
	case WM_MOVED:
	{
		msg[6] = fs->wind->r.w, msg[7] = fs->wind->r.h;
		/* fall through */
	}
	default:
	{
		do_formwind_msg(wind, to, amq, qmf, msg);
	}
	}
}

static int
fs_destructor(enum locks lock, struct xa_window *wind)
{
	DIAG((D_fsel,NULL,"fsel destructed"));
	return true;
}

static bool
open_fileselector1(enum locks lock, struct xa_client *client, struct fsel_data *fs,
		   const char *path, const char *file, const char *title,
		   fsel_handler *s, fsel_handler *c)
{
	int dh;			/* HR variable height for fileselector :-) */
	bool nolist;
	XA_WIND_ATTR kind;
	OBJECT *form = NULL, *menu = NULL;
	struct xa_window *dialog_window = NULL;
	XA_TREE *wt;
	char *pat,*pbt;
	struct scroll_info *list;
	RECT remember = {0,0,0,0};

	DIAG((D_fsel,NULL,"open_fileselector for %s on '%s', fn '%s', '%s', %lx,%lx)",
			c_owner(client), path, file, title, s, c));

	if (fs)
	{
		bzero(fs, sizeof(*fs));

		fs->owner = client;
		
		*fs->fslash = '\\';
		
		form = duplicate_obtree(C.Aes, ResourceTree(C.Aes_rsc, FILE_SELECT), 0);
		if (!form)
			goto memerr;
		fs->form = new_widget_tree(client, form);
		if (!fs->form)
			goto memerr;
		fs->form->flags |= WTF_TREE_ALLOC | WTF_AUTOFREE;

		menu = duplicate_obtree(C.Aes, ResourceTree(C.Aes_rsc, FSEL_MENU), 0);
		if (!menu)
			goto memerr;
		fs->menu = new_widget_tree(client, menu);
		if (!fs->menu)
			goto memerr;
		fs->menu->flags |= WTF_TREE_ALLOC | WTF_AUTOFREE;
				
		fs->selected = s;
		fs->canceled = c;
		if (file && *file)
		{
			strncpy(fs->file, file, sizeof(fs->file) - 1);
			*(fs->file + sizeof(fs->file) - 1) = 0;
		}

		object_get_spec(form + FS_FILE)->tedinfo->te_ptext = fs->file;
		form[FS_ICONS].ob_flags |= OF_HIDETREE;
		
		dh = root_window->wa.h - 7*screen.c_max_h - form->ob_height;
		form->ob_height += dh;
		form[FS_LIST ].ob_height += dh;
		form[FS_UNDER].ob_y += dh;

		/* Work out sizing */
		if (!remember.w)
		{
			form_center(form, 2*ICON_H);
			remember =
			calc_window(lock, client, WC_BORDER,
				    XaMENU|NAME,
				    C.Aes->options.thinframe,
				    C.Aes->options.thinwork,
				    *(RECT*)&form->ob_x);
		}

		strcpy(fs->root, path);

		/* Strip out the pattern description */

		fs->fs_pattern[0] = '*';
		fs->fs_pattern[1] = '\0';
		pat = strrchr(fs->root, '\\');
		pbt = strrchr(fs->root, '/');
		if (!pat) pat = pbt;
		if (pat)
		{
			strcpy(fs->fs_pattern, pat + 1);
			*(pat + 1) = 0;
			if (strcmp(fs->fs_pattern, "*.*") == 0)
				*(fs->fs_pattern + 1) = 0;
		}

		{
			int drv = get_drv(fs->root);
			if (drv >= 0)
				strcpy(fs_paths[drv], fs->root);
		}
		
		strcpy(fs->path, fs->root);

		kind = (XaMENU|NAME|TOOLBAR);
		if (C.update_lock == client->p ||
		    C.mouse_lock  == client->p)
		{
			nolist = true;
			kind |= STORE_BACK;
		}
		else
		{
			kind |= hide_move(&(client->options));
			nolist = false;
		}

		/* Create the window */
		dialog_window = create_window(  lock,
						do_winmesag,
						fs_msg_handler,
						client,
						nolist,
						kind,
						created_for_AES,
						C.Aes->options.thinframe,
						C.Aes->options.thinwork,
						remember,
						NULL,
						NULL); //&remember);
		
		if (!dialog_window)
			goto memerr;

		/* Set the window title */
		/* XXX - pointer into user space, correct here? */
		/*   ozk: no, absolutely not right here! */
		set_window_title(dialog_window, title, true);

		set_menu_widget(dialog_window, client, fs->menu);

		fs->drives = fsel_drives(fs->menu->tree,
					*(fs->root+1) == ':' ? tolower(*fs->root) - 'a' : d_getdrv());
		
		fsel_filters(fs->menu->tree, fs->fs_pattern);

		fs->clear_on_folder_change = 0;
		strcpy(fs->file, file); /* fill in the file edit field */

		wt = set_toolbar_widget(lock, dialog_window, client, form, FS_FILE, WIP_NOTEXT, NULL);
		/* This can be of use for drawing. (keep off border & outline :-) */
		wt->zen = true;
		wt->exit_form = fileselector_form_exit;

		/* HR: We need to do some specific things with the key's,
		 *     so we supply our own handler,
		 */
		dialog_window->keypress = fs_key_form_do; //fs_key_handler;

		/* HR: set a scroll list object */
		list = set_slist_object(lock,
				 wt,
				 dialog_window,
				 FS_LIST,
				 SIF_SELECTABLE|SIF_ICONINDENT|SIF_AUTOSELECT|SIF_TREEVIEW,
				 fs_closer, NULL,
				 fs_dclick, fs_click,
				 NULL, NULL, NULL, NULL/*free_scrollist*/,
				 fs->root, NULL, fs, 30);

		{
			struct seset_txttab tab;
			RECT r;
			short w;
			
			list->get(list, NULL, SEGET_LISTXYWH, &r);

			w = r.w >> 2;
			
			tab.index = 0;
			list->get(list, NULL, SEGET_TEXTTAB, &tab);
			tab.r.w = w * 3;
			list->set(list, NULL, SESET_TEXTTAB, (long)&tab, false);
			
			tab.index = 1;
			list->get(list, NULL, SEGET_TEXTTAB, &tab);
			tab.r.w = w;
			tab.flags |= SETAB_RJUST;
			list->set(list, NULL, SESET_TEXTTAB, (long)&tab, false);
		}
		/* HR: after set_menu_widget (fs_destructor must cover what is in menu_destructor())
		 *     Set the window destructor
		 */
		dialog_window->destructor = fs_destructor;

		strcpy(fs->filter, fs->fs_pattern);
		fs->wind = dialog_window;
		fs->treeview = true;
		open_window(lock, dialog_window, dialog_window->r);

		/* HR: after set_slist_object() & opwn_window */
		//refresh_filelist(lock, fs, 5);
		/* we post this as a client event, so it does not happend before the fsel is drawn... */
		post_cevent(client, CE_refresh_filelist, fs, NULL, 0, 0, NULL, NULL);

		DIAG((D_fsel,NULL,"done."));

		return true;
	}
	else
	{
memerr:
		if (fs)
		{
			if (fs->form)
			{
				remove_wt(fs->form, true);
				fs->form = NULL;
			}
			else if (form)
				free_object_tree(client, form);
			if (fs->menu)
			{
				remove_wt(fs->menu, true);
				fs->menu = NULL;
			}
			else if (menu)
				free_object_tree(client, form);
		}
		return false;
	}
}

void
close_fileselector(enum locks lock, struct fsel_data *fs)
{
	close_window(lock, fs->wind);
	delayed_delete_window(lock, fs->wind);
	fs->wind = NULL;
	fs->menu = NULL;
	fs->form = NULL;
	fs->selected = NULL;
	fs->canceled = NULL;
}

static void
handle_fsel(enum locks lock, struct fsel_data *fs, const char *path, const char *file)
{
	DIAG((D_fsel, NULL, "fsel OK: path=%s, file=%s", path, file));

	//display("hfsel '%s' '%s'", path, file);
	close_fileselector(lock, fs);
	fs->ok = 1;
	fs->done = 1;
	fs->owner->usr_evnt = 1;
}

static void
cancel_fsel(enum locks lock, struct fsel_data *fs, const char *path, const char *file)
{
	DIAG((D_fsel, NULL, "fsel CANCEL: path=%s, file=%s", path, file));

	close_fileselector(lock, fs);
	fs->ok = 0;
	fs->done = 1;
	fs->owner->usr_evnt = 1;
}

static int locked = 0;
void
open_fileselector(enum locks lock, struct xa_client *client, struct fsel_data *fs,
		  const char *path, const char *file, const char *title,
		  fsel_handler *s, fsel_handler *c)
{
	if (!locked)
	{
		open_fileselector1(lock, client, fs, path, file, title, s, c);
	}
}

/*
 * File selector interface routines
 */
static void
do_fsel_exinput(enum locks lock, struct xa_client *client, AESPB *pb, const char *text)
{
	char *path = (char *)(pb->addrin[0]);
	char *file = (char *)(pb->addrin[1]);
	struct fsel_data *fs;

	pb->intout[0] = 0;

	fs = kmalloc(sizeof(*fs));
	
	if (fs)
	{
		DIAG((D_fsel, NULL, "fsel_(ex)input: title=%s, path=%s, file=%s, fs=%lx",
			text, path, file, fs));
			
		if (open_fileselector1( lock|fsel,
					client,
					fs,
					path,
					file,
					text,
					handle_fsel,
					cancel_fsel))
		{
			client->status |= CS_FSEL_INPUT;
			Block(client, 21);
			client->status &= ~CS_FSEL_INPUT;
			pb->intout[0] = 1;
			if ((pb->intout[1] = fs->ok))
			{
				strcpy(path, fs->path);
				strcat(path, fs->fs_pattern);
				strcpy(file, fs->file);
			}
		}
		kfree(fs);
	}
}

unsigned long
XA_fsel_input(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(0,2,2)

	do_fsel_exinput(lock, client, pb, "");

	return XAC_DONE;
}

unsigned long
XA_fsel_exinput(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const char *t = (const char *)(pb->addrin[2]);

	CONTROL(0,2,3)

	if (pb->control[3] <= 2 || t == NULL)
		t = "";

	do_fsel_exinput(lock, client, pb, t);

	return XAC_DONE;
}
