
#include <mintbind.h>
#include <string.h>
#include <support.h>
#include <unistd.h>

#include <cflib.h>

#include "global.h"
#include "av.h"
#include "config.h"
#include "console.h"
#include "environ.h"
#include "event.h"
#include "proc.h"
#include "textwin.h"
#include "toswin2.h"
#include "version.h"
#include "window.h"


extern int __mint;

#ifdef __GNUC__
long _stksize = 32768;
#endif


static void term_tw(int ret_code)
{
	config_term();
	textwin_term();
	window_term();
	av_term();
	global_term();
	exit_app(ret_code);
}


int main(int argc, char *argv[])
{
	char str[25];
	OBJECT *tmp;

	if (getenv("DEBUG") || strcmp(argv[1], "--debug") == 0)
	{
/*		debug_init("ntw", Datei, "i:\\ntw.log");*/
		debug_init("ntw", Con, NULL);
	}

	(void)Pdomain(1);
	init_app(RSCNAME);

	/* Stimmt die RSC-Version? */
	rsrc_gaddr(R_TREE, VERSION, &tmp);
	get_string(tmp, RSC_VERSION, str);
	if (strcmp(str, TWVERSION) != 0)
	{
		do_alert(1, 0, "[3][Falsche RSC-Version!|Wrong RSC version!][Exit]");
		exit_app(1);
	}
	
	global_init();
	if (!__mint || gl_ap_version <= 0x400)
	{
		if (alert(1, 0, NOAES41) == 2)
			term_tw(1);
	}
	shel_write(9, 1, 1, 0L, 0L);	/* wir k�nnen AP_TERM */

	menu_register(-1, TW2NAME);	/* damit tw-call immer f�ndig wird. */
	if (gl_debug)
		menu_register(gl_apid, "  TosWin2 (debug)");
	else	
		menu_register(gl_apid, "  TosWin2");

	config_init();
	proc_init();
	av_init();

	config_load();
	event_init();

	if (gl_con_auto)
		open_console();
	
	if (gl_debug)
	{
		if (gl_magx)
		{
			WINCFG	*cfg;
			TEXTWIN	*t;
			
			cfg = get_wincfg("C:\\TMP\\DUM.TOS");
			t = new_proc("C:\\TMP\\DUM.TOS", "", NULL, "C:\\TMP", cfg, -1);
			if (t)
				open_window(t->win, FALSE);
		}	
		else
			new_shell();	
	}
	
	if (argc >= 2)
	{
		if (argv[1][0] == '-')		/* filter options */
		{
			if (strcmp(argv[1], "-l") == 0)
				new_shell();
		}
		else
		{
			TEXTWIN	*t;
			char filename[80] = "", *p, path[80] = "";
			char *env, arg[125] = "";
			int i;
			WINCFG	*cfg;
			
			unx2dos(argv[1], filename);
			strcpy(path, filename);
			p = strrchr(path, '\\');
			if (p == NULL)
				getcwd(path, (int)sizeof(path));
			else
				*p = '\0';
			cfg = get_wincfg(filename);
			for (i = 2; i < argc; i++)
			{
				strcat(arg, " ");
				strcat(arg, argv[i]);
			}
			arg[0] = strlen(arg);
			env = normal_env(cfg->col, cfg->row, cfg->vt_mode);
			t = new_proc(filename, arg, env, path, cfg, -1);
			if (t)
				open_window(t->win, cfg->iconified);
			free(env);
		}
	}

	event_loop();
	
	term_tw(0);

	return 0;
}
