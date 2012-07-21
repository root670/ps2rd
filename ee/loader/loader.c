/*
 * Boot loader (main project file)
 *
 * Copyright (C) 2009-2010 Mathias Lafeldt <misfire@debugon.org>
 *
 * This file is part of PS2rd, the PS2 remote debugger.
 *
 * PS2rd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PS2rd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PS2rd.  If not, see <http://www.gnu.org/licenses/>.
 *
 * $Id$
 */

/*
 *  ____	 ___ |	/ _____ _____
 * |  __	|	|___/	|	 |
 * |___| ___|	|	\ __|__   |	 gsKit Open Source Project.
 * ----------------------------------------------------------------------
 * Copyright 2004 - Chris "Neovanglist" Gilbert <Neovanglist@LainOS.org>
 * Licenced under Academic Free License version 2.0
 * Review gsKit README & LICENSE files for further details.
 *
 * font.c - Example demonstrating ROM Font (font) usage
 */

#include "gsKit.h"
#include "dmaKit.h"
#include "malloc.h"
#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <tamtypes.h>
#include "gsToolkit.h"
#include <unistd.h>
#include "cheatlist.h"

#include <libcheats.h>
#include <libconfig.h>
#include <sbv_patches.h>
#include <string.h>
#include "cheatman.h"
#include "configman.h"
#include "dbgprintf.h"
#include "elfid.h"
#include "erlman.h"
#include "irxman.h"
#include "mycdvd.h"
#include "mypad.h"
#include "myutil.h"

#define PAD_PORT	0
#define PAD_SLOT	0

#define CURSOR_INCREMENT	16
#define CURSOR_TOP 		80

const int MAXIMUM_ITEMS_PER_PAGE = 20;
const int MAXIMUM_GAMES = 512;
const int MAXIMUM_CHEATS = 512;
enum state { LOADING, GAMELIST, CODELIST, NEWGAME_KEYBOARD, BOOTGAME_PROMPT, BOOTGAME, DELETE_CHEAT_PROMPT };

static char _bootpath[FIO_PATH_MAX];
static enum dev_id _bootdev = DEV_UNKN;
static char padBuf[256] __attribute__((aligned(64)));

static inline size_t chr_idx(const char *s, char c)
{
	size_t i = 0;

	while (s[i] && (s[i] != c))
		i++;

	return (s[i] == c) ? i : -1;
}

static inline size_t last_chr_idx(const char *s, char c)
{
	size_t i = strlen(s);

	while (i && s[--i] != c)
		;

	return (s[i] == c) ? i : -1;
}

static int __dirname(char *filename)
{
	int i;

	if (filename == NULL)
		return -1;

	i = last_chr_idx(filename, '/');

	if (i < 0)
	{
		i = last_chr_idx(filename, '\\');

		if (i < 0)
		{
			i = chr_idx(filename, ':');

			if (i < 0)
				return -2;
		}
	}

	filename[i + 1] = '\0';
	return 0;
}

/*
 * Build pathname based on boot device and filename.
 */
static char *__pathname(const char *name)
{
	static char filename[FIO_PATH_MAX];
	enum dev_id dev;

	filename[0] = '\0';
	dev = get_dev(name);

	/* Add boot path if name is relative */
	if (dev == DEV_UNKN)
		strcpy(filename, _bootpath);

	strcat(filename, name);

	if (dev == DEV_CD)
	{
		strupr(filename);
		strcat(filename, ";1");
	}

	return filename;
}

/*
int write_cheat_history(int position, int gameId, int cheatId)
{
	write_text_file(__pathname("cheat_history.txt"), "Test Test Test\n")
}
*/

static int __start_elf(const char *boot2)
{
	char elfname[FIO_PATH_MAX];
	enum dev_id dev = DEV_CD;
	char *argv[16];
	int argc = 16;

	if (boot2 == NULL || (boot2 != NULL && (dev = get_dev(boot2)) == DEV_CD))
	{
		_cdStandby(CDVD_NOBLOCK);
		delay(100);
	}

	if (boot2 == NULL)
	{
		if (cdGetElf(elfname) < 0)
		{
			printf("Error: could not get ELF name from SYSTEM.CNF\n");
			_cdStop(CDVD_NOBLOCK);
			return -1;
		}

		boot2 = elfname;
	}

	build_argv(boot2, &argc, argv);

	if (!file_exists(argv[0]))
	{
		printf("Error: ELF %s not found\n", argv[0]);

		if (dev == DEV_CD)
			_cdStop(CDVD_NOBLOCK);

		return -1;
	}

	printf("Starting game...\n");
	printf("%s: running ELF %s ...\n", __FUNCTION__, argv[0]);

	padPortClose(PAD_PORT, PAD_SLOT);
	padReset();

	LoadExecPS2(argv[0], argc - 1, &argv[1]);

	if (dev == DEV_CD)
		_cdStop(CDVD_NOBLOCK);

	padInit(0);
	padPortOpen(PAD_PORT, PAD_SLOT, padBuf);

	printf("Error: could not load ELF %s\n", argv[0]);

	return -1;
}

void RenderPromptBox(GSGLOBAL *gsGlobal, u64 topColor, u64 bottomColor)
{
		gsKit_prim_quad_gouraud(gsGlobal, 100.0f, 100.0f,
										  100.0f, 350.0f,
										  540.0f, 100.0f,
										  540.0f, 350.0f, 3,
		topColor, bottomColor, topColor, bottomColor);
}

int main(int argc, char *argv[])
{
	char *gameTitles[MAXIMUM_GAMES];
	int numberOfGameTitles = 0;
	char *cheatTitles[MAXIMUM_GAMES];
	int enabledCheats[MAXIMUM_CHEATS];
	cheat_t* cheat = NULL;
	config_t config;
	cheats_t cheats;
	game_t *tempGame = NULL;
	engine_t engine;
	const char *cheatfile = NULL;
	const char *boot2 = NULL;
	struct padButtonStatus btn;
	u32 old_pad = 0;

	SifInitRpc(0);

	strcpy(_bootpath, argv[0]);
	__dirname(_bootpath);
	_bootdev = get_dev(_bootpath);

	GSGLOBAL *gsGlobal = gsKit_init_global();

	GSFONTM *gsFont = gsKit_init_fontm();
	//GSFONT *gsFont = gsKit_init_font(GSKIT_FTYPE_BMP_DAT, __pathname("dejavu.bmp"));
	//GSFONT *gsFont = gsKit_init_font( GSKIT_FTYPE_PNG_DAT, __pathname("dejavu.png") );

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
				D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

	// Initialize the DMAC
	dmaKit_chan_init(DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
	dmaKit_chan_init(DMA_CHANNEL_TOSPR);

	// Background colors
	u64 DarkSlateBlue = GS_SETREG_RGBAQ(0x48, 0x3D, 0x8B, 0x00, 0x00);

	// Font Colors
	u64 WhiteFont = GS_SETREG_RGBAQ(0x90, 0x90, 0x90, 0x80, 0x00);
	u64 RedFont = GS_SETREG_RGBAQ(0xFF, 0x10, 0x10, 0x80, 0x00);
	u64 GreenFont = GS_SETREG_RGBAQ(0x20, 0xFF, 0x20, 0x80, 0x00);
	u64 YellowFont = GS_SETREG_RGBAQ(0xFF, 0xFF, 0x00, 0x80, 0x00);
	
	// Transparent Colors
	u64 BlackTran = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x25, 0x00);
	u64 BlueTran = GS_SETREG_RGBAQ(0x00, 0x00, 0xFF, 0x25, 0x00);

	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

	gsKit_init_screen( gsGlobal );

	gsKit_mode_switch( gsGlobal, GS_ONESHOT );

	//gsKit_font_upload( gsGlobal, gsFont );
	gsKit_fontm_upload( gsGlobal, gsFont );

	gsFont->Spacing = 0.65f;

	gsKit_mode_switch( gsGlobal, GS_ONESHOT );

	printf("Welcome to PS2rd %s\n", PS2RD_VERSION);
	printf("Cheating GUI by root670\n");
	printf("Build date: %s %s\n", __DATE__, __TIME__);
	printf("Booting from: %s\n", _bootpath);

	printf("Initializing...\n");

	printf("* Reading config...\n");
	config_build(&config);

	if ( config_read_file ( &config, __pathname( CONFIG_FILE ) ) != CONFIG_TRUE )
		printf("config error: %s\n", config_error_text(&config));

	if ( _bootdev != DEV_HOST && config_get_bool(&config, SET_IOP_RESET))
		reset_iop("rom0:UDNL rom0:EELOADCNF");

	if (config_get_bool(&config, SET_SBV_PATCHES))
	{
		printf("* Applying SBV patches...\n");
		sbv_patch_enable_lmb();
		sbv_patch_disable_prefix_check();
		sbv_patch_user_mem_clear(0x00100000);
	}

	if (load_modules(&config) < 0)
	{
		printf("Error: failed to load IRX modules\n");
		goto end;
	}

	cdInit(CDVD_INIT_NOCHECK);
	_cdStop(CDVD_NOBLOCK);

	padInit(0);
	padPortOpen(PAD_PORT, PAD_SLOT, padBuf);
	padWaitReady(PAD_PORT, PAD_SLOT);
	padSetMainMode(PAD_PORT, PAD_SLOT, PAD_MMODE_DIGITAL, PAD_MMODE_LOCK);

	if (install_erls(&config, &engine) < 0)
	{
		printf("Error: failed to install ERLs\n");
		goto end;
	}

	install_modules(&config);

	cheats_init(&cheats);
	if( !config_get_bool( &config, SET_PATH_IS_ABSOLUTE ) )
		cheatfile = __pathname(config_get_string(&config, SET_CHEATS_FILE));
	else
		cheatfile = config_get_string(&config, SET_CHEATS_FILE);

	printf("Ready.\n");

	printf( "%d game(s) found in cheat file.\n", numberOfGameTitles );

	// storage for use when changing between game list and code list
	int gameCursorY = CURSOR_TOP;
	int gameStartingItem = 0;
	int oldPage;
	
	int doneLoadingCheats = 0;
	int numberOfGameCheats = 0;
	int numberOfEnabledCheats = 0;
	int enabledGame = 0; // game to use with codes
	int y; // position of title to be rendered
	int cursorY = CURSOR_TOP;
	int selectedGame = 0;
	int selectedCheat = 0;
	int cheatPage = 1;
	int oldCheatPage;
	int page = 1;
	int pageStartingItem = 0;
	int n; // temp. variable
	int h = 0; // temp. variable
	//int curState = GAMELIST;
	int curState = LOADING;
	int helpTick = 0;
	int frame = 0;
	int codeListModified = 0;

	while( 1 )
	{
		u32 paddata, new_pad;
		y = CURSOR_TOP;
		oldPage = page;
		oldCheatPage = cheatPage;
		
		gsKit_clear( gsGlobal, DarkSlateBlue );

		if( curState == BOOTGAME )
		{
			RenderPromptBox(gsGlobal, BlackTran, BlueTran);
			if( (numberOfEnabledCheats > 0) || (codeListModified) )
			{
				if (codeListModified)
				{
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 170, 200, 3, 1.0f, YellowFont, "Saving Code List...");
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 130, 240, 3, 0.75f, RedFont, "Do NOT remove the memory card!");
				} else
				{
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 185, 200, 3, 1.0f, YellowFont, "Starting Game...");
				}
			} else
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 175, 215, 3, 1.0f, YellowFont, "No cheats enabled");
			}
		}

		padWaitReady(PAD_PORT, PAD_SLOT);

		if (!padRead(PAD_PORT, PAD_SLOT, &btn))
			continue;

		paddata = 0xffff ^ btn.btns;
		new_pad = paddata & ~old_pad;
		old_pad = paddata;

		// Read pad input
		if( new_pad & PAD_UP )
		{
			switch( curState )
			{
				case GAMELIST:
					if( selectedGame != 0 ) // Move arrow up if we're not at the beginning of the list
					{
						selectedGame--;
						cursorY -= CURSOR_INCREMENT;
					}

					if((cursorY < CURSOR_TOP) & (page > 1)) // if we're at the top of a page differant than page 1...
					{
						page--;
						cursorY = CURSOR_TOP + (( MAXIMUM_ITEMS_PER_PAGE - 1) * CURSOR_INCREMENT);
					}

					break;

				case CODELIST:
					if( selectedCheat != 0 )
					{
						selectedCheat--;
						cursorY -= CURSOR_INCREMENT;
					}

					if(( cursorY < CURSOR_TOP ) & ( cheatPage > 1 ))
					{
						cheatPage--;
						cursorY = CURSOR_TOP + (( MAXIMUM_ITEMS_PER_PAGE - 1) * CURSOR_INCREMENT);
					}

					break;

				default:
					break;
			}
		}

		if( new_pad & PAD_LEFT )
		{
			if( curState == CODELIST ) // Switch to game list
			{
				// Restore values that were saved previously.
				cursorY = gameCursorY;
				pageStartingItem = gameStartingItem;
				numberOfGameCheats = 0;
				selectedCheat = 0;
				helpTick = 0;
				curState = GAMELIST;
			}
		}

		if( new_pad & PAD_DOWN )
		{
			switch( curState )
			{
				case GAMELIST:
					if( selectedGame < numberOfGameTitles - 1 )
					{
						if( cursorY >= ( CURSOR_TOP + ( ( MAXIMUM_ITEMS_PER_PAGE - 1 ) * CURSOR_INCREMENT) ) ) // if we've reached the bottom of a page...
						{
							page++;
							selectedGame++;
							cursorY = CURSOR_TOP;
						}
						else // normal page navigation
						{
							selectedGame++;
							cursorY += CURSOR_INCREMENT;
						}
					}

					break;

				case CODELIST:
					if( selectedCheat < numberOfGameCheats - 1 )
					{
						if( cursorY >= (CURSOR_TOP + ( (MAXIMUM_ITEMS_PER_PAGE - 1) * CURSOR_INCREMENT) ) )
						{
							cheatPage++;
							selectedCheat++;
							cursorY = CURSOR_TOP;
						}
						else
						{
							selectedCheat++;
							cursorY += CURSOR_INCREMENT;
						}
					}

					break;

				default:
					break;
			}
		}

		if( new_pad & PAD_RIGHT )
		{
			if( curState == GAMELIST ) // Switch to cheat list
			{
				// Clear titles of cheats
				h = 0;
				for( h = 0; h < MAXIMUM_CHEATS; h++ )
				{
					cheatTitles[h] = NULL;
				}
				
				// Store current values so we can restore them when going back to the game list
				gameCursorY = cursorY;
				gameStartingItem = pageStartingItem;
				
				cheatPage = 1;
				doneLoadingCheats = 0;
				pageStartingItem = 0;
				cursorY = CURSOR_TOP;
				oldCheatPage = 1;
				selectedCheat = 0;
				helpTick = 0;
				curState = CODELIST;
			}
		}

		if( new_pad & PAD_START ) // Boot a game
		{
			if( curState == GAMELIST )
			{
				curState = BOOTGAME_PROMPT;
			}
		}

		if( new_pad & PAD_CROSS )
		{
			if( curState == CODELIST ) // Enable/Disable selected cheat
			{
				if( selectedGame != enabledGame )
				{
					// Reset enabled cheats
					int h = 0;
					for( h = 0; h < MAXIMUM_CHEATS; h++ )
						enabledCheats[h] = 0;
						
					numberOfEnabledCheats = 0;
				}

				enabledGame = selectedGame;

				if( enabledCheats[selectedCheat] != 1 ) // if cheat is currently disabled, enable it.
				{
					enabledCheats[selectedCheat] = 1;
					numberOfEnabledCheats++;
				}
				else   // if cheat is currently enabled, disable it.
				{
					enabledCheats[selectedCheat] = 0;
					numberOfEnabledCheats--;
				}

				printf("\n--------Enabled Cheats--------\n");
				int h = 0;
				for( h = 0; h < MAXIMUM_CHEATS; h++ )
				{
					if( enabledCheats[h] == 1 )
					{
						printf("%d ", h);
					}
				}
				printf("\n");
			} else if( curState == BOOTGAME_PROMPT && _cdDiskReady( CDVD_BLOCK ) ) // boot game
			{
				gsKit_clear( gsGlobal, DarkSlateBlue );
				curState = BOOTGAME;
			}
			
			else if( curState == DELETE_CHEAT_PROMPT )
			{
				tempGame = find_game_by_title( gameTitles[selectedGame], &cheats.games );

				if( tempGame != NULL )
				{
					cheat_t *cheat;

					int badVar1 = 0;
					int badVar2 = 0;
					CHEATS_FOREACH(cheat, &tempGame->cheats)
					{
						if(badVar1 == selectedCheat)
						{
							remove_cheat(&tempGame->cheats, cheat, 1);
							free(cheat);
						}
						badVar1++;
					}
					
					for(badVar2 = selectedCheat; badVar2 < numberOfGameCheats - selectedCheat; badVar2++) // Shift enabled code ids to reflect one less cheat on the list
						enabledCheats[badVar2-1] = enabledCheats[badVar2];
						
					codeListModified = 1;
					doneLoadingCheats = 0; // Reload cheats to reflect changes
				}
				curState = CODELIST;
			}
		}

		if( new_pad & PAD_TRIANGLE )
		{
			if( curState == CODELIST )
			{
				if( numberOfGameCheats > 0)
					curState = DELETE_CHEAT_PROMPT;
			}

			else if( curState == DELETE_CHEAT_PROMPT ) // Go back
			{
				curState = CODELIST;
			} 
			
			else if( curState == BOOTGAME_PROMPT ) // go back
			{
				curState = GAMELIST;
			}
		}

		if( new_pad & PAD_CIRCLE )
		{
			if( curState == GAMELIST )
			{
				// Not yet implemented
				//curState = NEWGAME_KEYBOARD;
			}
		}
		
		if( new_pad & PAD_R1 ) // Jump to next page
		{
			if( curState == GAMELIST )
			{
				if( ( MAXIMUM_ITEMS_PER_PAGE * page ) < numberOfGameTitles )
				{
					selectedGame = ( MAXIMUM_ITEMS_PER_PAGE * page );
					cursorY = CURSOR_TOP;
					page++;
				}
			}
			else if( curState == CODELIST )
			{
				if( ( ( MAXIMUM_ITEMS_PER_PAGE) * cheatPage )  < numberOfGameCheats )
				{
					selectedCheat = ( (MAXIMUM_ITEMS_PER_PAGE) * cheatPage );
					cursorY = CURSOR_TOP;
					cheatPage++;
				}
			}
		}
		
		if( new_pad & PAD_L1 ) // Jump to previous page
		{
			if( curState == GAMELIST )
			{
				if( ( MAXIMUM_ITEMS_PER_PAGE * ( page - 1) ) > ( numberOfGameTitles - ( MAXIMUM_ITEMS_PER_PAGE * ( page - 1) ) ) )
				{
					selectedGame = ( MAXIMUM_ITEMS_PER_PAGE * ( page - 2 ) );
					cursorY = CURSOR_TOP;
					page--;
				}
			}
			else if( curState == CODELIST )
			{
				if((( MAXIMUM_ITEMS_PER_PAGE ) * ( cheatPage - 1 )) > 0 )
				{
					selectedCheat = ( (MAXIMUM_ITEMS_PER_PAGE) * ( cheatPage - 2 ) );
					cursorY = CURSOR_TOP;
					cheatPage--;
				}
			}
		}

		// for logging
		if( new_pad )
		{
			/*
			printf("page: %d\n", page);
			printf("selectedGame: %d\n", selectedGame);
			printf("selectedCheat: %d\n", selectedCheat);
			printf("cursorY: %d\n", cursorY);
			printf("curState: %d\n", curState);
			printf("pageStartingItem: %d\n", pageStartingItem);
			printf("numberOfGameCheats: %d\n", numberOfGameCheats);
			printf("doneLoadingCheats: %d\n", doneLoadingCheats);
			printf("cheatPage: %d\n", cheatPage);
			printf("oldCheatPage: %d\n", oldCheatPage);
			printf("numberOfEnabledCheats: %d\n\n", numberOfEnabledCheats);
			*/
		}

		if( ( curState ==  GAMELIST ) || ( curState == BOOTGAME_PROMPT ) )
		{
			// Set the first item (game title) to appear at the top of the page
			if(page > oldPage)
				pageStartingItem += MAXIMUM_ITEMS_PER_PAGE;
			else if (( page < oldPage ) & ( page > 0 ))
				pageStartingItem -= MAXIMUM_ITEMS_PER_PAGE;

			// Render game list
			for( n = pageStartingItem; n < (MAXIMUM_ITEMS_PER_PAGE * page); n++ )
			{
				if( gameTitles[n] != NULL ) // Prevent TBL overflow
				{
					if(( n == enabledGame ) & ( numberOfEnabledCheats > 0 ))
						gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, y, 3, 0.6f, GreenFont, gameTitles[n]);
					else
						gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, y, 3, 0.6f, WhiteFont, gameTitles[n]);

					y += CURSOR_INCREMENT; // Create space between each title
				}
			}

			// Render arrow graphic
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 10, cursorY, 3, 0.6f, YellowFont, "\efright");

			// Render help text
			if( helpTick < 128 )
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press \efright to see codes.");
			else if( helpTick < 256)
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press START to boot game.");
			}
			else if( helpTick < 384 )
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press R1 or L1 to jump between pages.");
			}

			if( helpTick >= 383 )
				helpTick = 0;

			helpTick++;
		} else if (( curState == CODELIST ) || ( curState == DELETE_CHEAT_PROMPT ))
		{
			if( !doneLoadingCheats ) // Load cheats for currently selected game
			{
				tempGame = find_game_by_title( gameTitles[selectedGame], &cheats.games );
				numberOfGameCheats = 0;

				if( tempGame != NULL )
				{
					CHEATS_FOREACH( cheat, &tempGame->cheats )
					{
						if ( cheat != NULL )
						{
							if( cheat->desc != "" )
							{
								cheatTitles[numberOfGameCheats] = cheat->desc;
								numberOfGameCheats++;
							}
						}
					}
				}				
				doneLoadingCheats = 1;
			}

			// Render game name
			char *newGameTitle = NULL;
			sprintf(newGameTitle, "%s (%d cheats)", gameTitles[selectedGame], numberOfGameCheats);
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 20, 3, .75f, YellowFont, newGameTitle );
			y = CURSOR_TOP;

			if(cheatPage > oldCheatPage)
				pageStartingItem += MAXIMUM_ITEMS_PER_PAGE;
			else if (cheatPage < oldCheatPage)
				pageStartingItem -= MAXIMUM_ITEMS_PER_PAGE;

			// Render cheat list
			if (numberOfGameCheats == 0)// Game has no cheats available
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, y, 3, 0.6f, GreenFont, "No codes were found for this game!");
			} else
			{
				for( n = pageStartingItem; n < ((MAXIMUM_ITEMS_PER_PAGE) * cheatPage); n++ )
				{
					if (cheatTitles[n] != NULL)
					{
						if(( enabledCheats[n] == 1 ) & ( selectedGame == enabledGame ))
						{
							gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, y, 3, 0.6f, GreenFont, cheatTitles[n]);
						}
						else
						{
							gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, y, 3, 0.6f, WhiteFont, cheatTitles[n]);
						}
						y += CURSOR_INCREMENT;
					}
				}

				// Render arrow graphic
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 10, cursorY, 3, 0.6f, YellowFont, "\efright");

				// Render help text
				if( helpTick < 128 )
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press X to enable cheat.");
				else if( helpTick < 256)
				{
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press \efleft to go to the game list.");
				}
				else if( helpTick < 384)
				{
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press TRIANGLE to delete a cheat.");
				}
				else if( helpTick < 512)
				{
					gsKit_fontm_print_scaled(gsGlobal, gsFont, 30, 420, 3, .60f, GreenFont, "Press R1 or L1 to jump between pages.");
				}

				if( helpTick >= 511 )
					helpTick = 0;

				helpTick++;
			}
		}
		
		if ( curState == BOOTGAME_PROMPT )
		{
			RenderPromptBox(gsGlobal, BlackTran, BlueTran);
			if( _cdDiskReady( CDVD_BLOCK ) )
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 205, 220, 3, .60f, YellowFont, "Press X to start game");
			} else
			{
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 180, 220, 3, .60f, YellowFont, "Please insert a game disk");
			}
				gsKit_fontm_print_scaled(gsGlobal, gsFont, 185, 235, 3, .60f, YellowFont, "Press TRIANGLE to cancel.");
		}
		
		else if( curState == DELETE_CHEAT_PROMPT )
		{
			RenderPromptBox( gsGlobal, BlackTran, BlueTran );
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 205, 220, 3, .60f, YellowFont, "Delete selected cheat?");
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 220, 240, 3, .60f, YellowFont, "Press X to confirm.");
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 190, 255, 3, .60f, YellowFont, "Press TRIANGLE to cancel.");
		}
		
		else if ( curState == BOOTGAME )
		{
			if ( frame >= 3 ) // has to be 3 for everything to render first.
			{
				if( numberOfEnabledCheats > 0 ) // Don't try to load cheats if none are availible
				{
					if (codeListModified) // save modifications to code list
						cheats_write_file( &cheats, cheatfile );
					
					int cheatCount = 0;
					int nextCodeCanBeHook = 1;
					cheat_t *cheat = NULL;
					code_t *code = NULL;

					reset_cheats( &engine );
					
					printf("Enabled Codes:\n");
					CHEATS_FOREACH( cheat, &tempGame->cheats )
					{
						if( enabledCheats[cheatCount] > 0 )
						{
							printf( "[Code %d]\n", ( cheatCount + 1 ) );
							CODES_FOREACH( code, &cheat->codes )
							{
								printf( "%08X %08X\n", code->addr, code->val );
								
								/* TODO improve check for hook */
								if (((code->addr & 0xfe000000) == 0x90000000) && nextCodeCanBeHook == 1)
								{
									engine.add_hook(code->addr, code->val);
									printf("Hook installed at %08X\n", code->addr);
								}
								else
									engine.add_code(code->addr, code->val);
								
								// Discard any false positives from being hook candidates
								if((code->addr & 0xf0000000) == 0x40000000 || 0x30000000)
									nextCodeCanBeHook = 0;
								else
									nextCodeCanBeHook = 1;
							}
						}
						cheatCount++;
					}
				}
				printf( "Booting game...\n" );
				__start_elf( boot2 );
			}
			frame++;
		}
		
		else if ( curState == LOADING )
		{
			RenderPromptBox( gsGlobal, BlackTran, BlueTran );
			gsKit_fontm_print_scaled(gsGlobal, gsFont, 205, 220, 3, .60f, YellowFont, "Loading cheats...");
			
			if( frame >= 3 )
			{
				if (load_cheats(cheatfile, &cheats) < 0)
					printf("Error: failed to load cheats from %s\n", cheatfile);

				printf("Ready.\n");

				// Initialize arrays to prevent memory corruption
				int h = 0;
				for( h = 0; h < MAXIMUM_GAMES; h++ )
				{
					gameTitles[h] = NULL;
				}

				h = 0;

				for( h = 0; h < MAXIMUM_CHEATS; h++ )
				{
					enabledCheats[h] = 0;
				}

				game_t *tempGame;

				// Fill list of games
				GAMES_FOREACH( tempGame, &cheats.games )
				{
					if ( tempGame != NULL )
					{
						gameTitles[numberOfGameTitles] = tempGame->title;
						numberOfGameTitles++;
					}
				}

				free(tempGame);
				curState = GAMELIST;
			}
			frame++;
		}
		gsKit_sync_flip( gsGlobal );
		gsKit_queue_exec( gsGlobal );
	}

end:
	printf("Exit...\n");

	config_destroy(&config);
	cheats_destroy(&cheats);
	uninstall_erls();
	SleepThread();

	return 1;
}
