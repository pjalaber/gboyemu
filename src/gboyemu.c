#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <SDL.h>
#include "gboyemu.h"
#include "rom.h"
#include "z80.h"
#include "mmu.h"
#include "gpu.h"
#include "divider.h"
#include "interrupt.h"
#include "timer.h"
#include "joypad.h"
#include "sound.h"
#include "serial.h"

#define CONF_DIR ".gboyemu"
#define DUMP_DIR "dump"
#define SYNC_PERIOD_MS ((GPU_CYCLES_FULL * 1000) / CLOCK_SPEED_HZ)

static char dump_dir[PATH_MAX];

static struct
{
	uint32_t accurate;
	uint32_t time;
	uint32_t disassemble;
	uint32_t sync_cycles;
	uint32_t delayed;
} gboyemu;

static int32_t create_dir(const char *dir)
{
	struct stat buf;
	int32_t ret;
	ret = stat(dir, &buf);
	if ( ret < 0 && errno == ENOENT )
	{
		if ( mkdir(dir, S_IRWXU) < 0 )
			return -1;

		return 0;
	}
	else if ( ret == 0 )
		return 0;

	return -1;
}

static void build_path(char *buf, size_t buf_size, const char *path, const char *subpath, const char *ext)
{
	if ( ext == NULL )
		snprintf(buf, buf_size, "%s/%s", path, subpath);
	else
		snprintf(buf, buf_size, "%s/%s%s", path, subpath, ext);
	buf[buf_size - 1] = '\0';
}

static int32_t check_conf_dir(void)
{
	struct passwd *pwd;
	char conf_dir[PATH_MAX];

	/* get home directory
	 */
	pwd = getpwuid(getuid());
	if ( pwd == NULL )
		return -1;

	build_path(conf_dir, sizeof(conf_dir), pwd->pw_dir, CONF_DIR, NULL);
	if ( create_dir(conf_dir) < 0 )
	{
		fprintf(stderr, "Can't create directory %s\n", conf_dir);
		return -1;
	}

	build_path(dump_dir, sizeof(dump_dir), conf_dir, DUMP_DIR, NULL);
	if ( create_dir(dump_dir) < 0 )
	{
		fprintf(stderr, "Can't create directory %s\n", dump_dir);
		return -1;
	}

	return 0;
}


static int32_t gboyemu_dump(void)
{
	FILE *file;
	int32_t ret;
	char filename[PATH_MAX];

	build_path(filename, sizeof(filename), dump_dir, rom_get_title(), ".dump");
	ret = -1;
	file = fopen(filename, "w");

	if ( file == NULL )
	{
		fprintf(stderr, "Count not create dump file %s\n", filename);
		goto error1;
	}

	if ( rom_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping rom state\n");
		goto error2;
	}

	if ( z80_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping z80 state\n");
		goto error2;
	}

	if ( interrupt_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping interrupt state\n");
		goto error2;
	}

	if ( timer_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping timer state\n");
		goto error2;
	}

	if ( divider_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping divider state\n");
		goto error2;
	}

	if ( mmu_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping mmu state\n");
		goto error2;
	}

	if ( gpu_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping gpu state\n");
		goto error2;
	}

	if ( joypad_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping joypad state\n");
		goto error2;
	}

	if ( sound_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping sound state\n");
		goto error2;
	}

	if ( serial_dump(file) < 0 )
	{
		fprintf(stderr, "Failed dumping serial state\n");
		goto error2;
	}

	ret = 0;
	fprintf(stderr, "Successfuly wrote %s\n", filename);
  error2:
	fclose(file);
  error1:
	return ret;
}

static int32_t gboyemu_restore(void)
{
	FILE *file;
	int32_t ret;
	char filename[PATH_MAX];

	build_path(filename, sizeof(filename), dump_dir, rom_get_title(), ".dump");
	ret = -1;
	file = fopen(filename, "r");

	if ( file == NULL )
	{
		fprintf(stderr, "Count not open dump file %s\n", filename);
		goto error1;
	}

	if ( rom_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring rom state\n");
		goto error2;
	}

	if ( z80_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring z80 state\n");
		goto error2;
	}

	if ( interrupt_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring interrupt state\n");
		goto error2;
	}

	if ( timer_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring timer state\n");
		goto error2;
	}

	if ( divider_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring divider state\n");
		goto error2;
	}

	if ( mmu_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring mmu state\n");
		goto error2;
	}

	if ( gpu_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring gpu state\n");
		goto error2;
	}

	if ( joypad_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring joypad state\n");
		goto error2;
	}

	if ( sound_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring sound state\n");
		goto error2;
	}

	if ( serial_restore(file) < 0 )
	{
		fprintf(stderr, "Failed restoring serial state\n");
		goto error2;
	}

	ret = 0;
	fprintf(stderr, "Successfuly read %s\n", filename);
  error2:
	fclose(file);
  error1:
	return ret;
}

static inline void busy_wait(uint32_t msecs)
{
	uint32_t time, time2reach;
	time = SDL_GetTicks();
	time2reach = time + msecs;
	while ( time < time2reach )
	{
		time = SDL_GetTicks();
	}
}

static uint32_t gboyemu_accurate_delays(void)
{
	uint32_t ticks1, ticks2;
	uint32_t accurate;

	/* Force a task switch now, so we have a longer timeslice afterwards */
	SDL_Delay(10);

	ticks1 = SDL_GetTicks();
	SDL_Delay(1);
	ticks2 = SDL_GetTicks();

	/* If the delay took longer than 10ms, we are on an inaccurate system! */
	accurate = ((ticks2 - ticks1) < 9);

	if (accurate)
		fprintf(stderr, "Accurate delays: %u ms\n", ticks2 - ticks1);
	else
		fprintf(stderr, "No accurate delays: %u ms\n", ticks2 - ticks1);

	return accurate;
}

int32_t gboyemu_init(void)
{
	if ( check_conf_dir() < 0 )
	{
		fprintf(stderr, "Could not check configuration directory\n");
		return -1;
	}

	if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0 )
	{
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	if ( z80_init() < 0 )
	{
		fprintf(stderr, "Could not initialize z80. exiting.\n");
		return -1;
	}

	if ( mmu_init() < 0 )
	{
		fprintf(stderr, "Could not initialize mmu. exiting.\n");
		return -1;
	}

	if ( gpu_init(0) < 0 )
	{
		fprintf(stderr, "Could not initialize gpu. exiting.\n");
		return -1;
	}

	if ( sound_init() < 0 )
	{
		fprintf(stderr, "Could not initialize sound. exiting.\n");
		return -1;
	}

	if ( divider_init() < 0 )
	{
		fprintf(stderr, "Could not initialize divider. exiting.\n");
		return -1;
	}

	if ( interrupt_init() < 0 )
	{
		fprintf(stderr, "Could not initialize interrupt. exiting.\n");
		return -1;
	}

	if ( timer_init() < 0 )
	{
		fprintf(stderr, "Could not initialize timer. exiting.\n");
		return -1;
	}

	if ( joypad_init() < 0 )
	{
		fprintf(stderr, "Could not initialize joypad. exiting.\n");
		return -1;
	}

	if ( serial_init() < 0 )
	{
		fprintf(stderr, "Could not initialize serial. exiting.\n");
		return -1;
	}

	memset(&gboyemu, 0, sizeof(gboyemu));
	gboyemu.accurate = gboyemu_accurate_delays();
	gboyemu.disassemble = 0;
	sound_start();

	return 0;
}

void gboyemu_cleanup(void)
{
	sound_stop();

	SDL_Quit();
	fprintf(stderr, "GoodBye!\n");
}

int32_t gboyemu_load_rom(const char *rom_filename)
{
	char title[64];
	if ( rom_load(rom_filename) < 0 )
	{
		fprintf(stderr, "Could not load rom. exiting.\n");
		return -1;
	}

	snprintf(title, sizeof(title), "GBOYEMU - %s", rom_get_title());
	title[sizeof(title) - 1] = '\0';
	SDL_WM_SetCaption(title, NULL);

	return 0;
}

int32_t main(int32_t argc, const char **argv)
{
        SDL_Event event;
	uint32_t cycles;
	uint32_t i, frame_skip;
	uint32_t run = 1;
	uint32_t delay, time2sleep;

	if ( argc != 2 )
	{
		fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
		return -1;
	}

	if ( gboyemu_init() < 0 )
		return -1;

	if ( gboyemu_load_rom(argv[1]) < 0 )
		return -1;

	gboyemu.time = SDL_GetTicks();

	while ( run )
	{
		if ( z80_stopped() == 0 )
		{
			for ( i = 0; i < 10; i++ )
			{
				interrupt_run();

				cycles = z80_next_opcode(gboyemu.disassemble);
				gboyemu.sync_cycles += cycles;

				timer_update(cycles);
				divider_update(cycles);
				sound_run(cycles);
				if ( gboyemu.delayed >= SYNC_PERIOD_MS )
					frame_skip = 1;
				else
					frame_skip = 0;
				gpu_run(cycles, frame_skip);
			}

			if ( gboyemu.sync_cycles >= (CLOCK_SPEED_HZ / 1000) * SYNC_PERIOD_MS )
			{
				delay = SDL_GetTicks() - gboyemu.time;
				if ( delay < SYNC_PERIOD_MS )
				{
					time2sleep = SYNC_PERIOD_MS - delay;
					if ( gboyemu.delayed > 0 )
					{
						if ( gboyemu.delayed > time2sleep )
						{
							gboyemu.delayed -= time2sleep;
							time2sleep = 0;
						}
						else
						{
							time2sleep -= gboyemu.delayed;
							gboyemu.delayed = 0;
						}
					}

					if ( time2sleep > 0 )
						SDL_Delay(time2sleep);
				}
				else
				{
					gboyemu.delayed += delay - SYNC_PERIOD_MS;
				}

				gboyemu.time = SDL_GetTicks();
				gboyemu.sync_cycles = 0;
			}
		}

		while ( SDL_PollEvent(&event) )
		{
			switch ( event.type )
			{
				case SDL_QUIT:
				run = 0;
				break;

				case SDL_KEYDOWN:
				if ( event.key.keysym.sym == SDLK_F10 )
				{
					gboyemu.disassemble = !gboyemu.disassemble;
				}
				else if ( event.key.keysym.sym == SDLK_F1 )
				{
					gboyemu_restore();
				}
				else if ( event.key.keysym.sym == SDLK_F2 )
				{
					gboyemu_dump();
				}
				else if ( event.key.keysym.sym == SDLK_KP_PLUS )
				{
					if ( gpu_get_zoom() < GPU_ZOOM_MAX )
					gpu_set_zoom(gpu_get_zoom() + 1);
				}
				else if ( event.key.keysym.sym == SDLK_KP_MINUS )
				{
					if ( gpu_get_zoom() > 1 )
						gpu_set_zoom(gpu_get_zoom() - 1);
				}
				else
				{
					if ( joypad_handle_key(event.key.keysym.sym, 1) )
						z80_resume_stop();
				}
				break;

				case SDL_KEYUP:
				joypad_handle_key(event.key.keysym.sym, 0);
				break;

				default:
				break;
			}
		}

	}

	return 0;
}
